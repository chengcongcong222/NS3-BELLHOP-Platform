#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/routing.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/topology.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/candidate_receiver_resolver.hpp"
#include "internal/commit_service.hpp"
#include "internal/communication_id_allocator.hpp"
#include "internal/cycle_coordinator.hpp"
#include "internal/cycle_signal_runtime.hpp"
#include "internal/cycle_working_state.hpp"
#include "internal/event_dispatcher.hpp"
#include "internal/fifo_packet_selector.hpp"
#include "internal/in_flight_signal_ledger.hpp"
#include "internal/ns3_kernel_gateway.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/plan_bound_tx_runtime.hpp"
#include "internal/receiver_processor.hpp"
#include "internal/reception_result_accumulator.hpp"
#include "internal/transmission_executor.hpp"
#include "internal/transmission_session_event_sink.hpp"
#include "internal/tx_preparation.hpp"
#include "internal/world_state_store.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::kernel::internal;
using namespace ns3_factory::runtime::internal;

namespace {

constexpr auto Seconds(std::int64_t value) -> SimTime {
  return SimTime::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto DurationSeconds(std::int64_t value) -> SimDuration {
  return SimDuration::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto Node(std::uint64_t id) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, -10.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto MakeSnapshot() -> Result<WorldSnapshot> {
  return WorldSnapshot::Create(
      SnapshotVersion{0},
      SimTime::Zero(),
      {Node(4), Node(2), Node(1), Node(3)});
}

auto MakePlan(bool broadcast) -> Result<ProtocolCyclePlan> {
  auto timing = CycleTiming::Create(PlanningCycleId{0},
                                    SnapshotVersion{0},
                                    SimTime::Zero(),
                                    Seconds(10));
  if(!timing) return std::unexpected(timing.error());
  const std::vector<TxOpportunity> opportunities{
      TxOpportunity{NodeId{2}, Seconds(1)}};
  if(broadcast) {
    return ProtocolCyclePlan::Create(*timing, opportunities);
  }

  const std::vector<NodeId> nodes{
      NodeId{1}, NodeId{2}, NodeId{3}, NodeId{4}};
  auto graph = ConnectivityGraph::Create(
      {DirectedLink{NodeId{2}, NodeId{3}}}, nodes);
  if(!graph) return std::unexpected(graph.error());
  auto topology = LogicalTopology::Create(
      {LogicalLink{NodeId{2}, NodeId{3}}}, nodes, *graph);
  if(!topology) return std::unexpected(topology.error());
  auto roles = RoleTable::Create({}, nodes);
  if(!roles) return std::unexpected(roles.error());
  auto structure = StructureSnapshot::Create(PlanningCycleId{0},
                                             SnapshotVersion{0},
                                             std::move(*roles),
                                             std::move(*graph),
                                             std::move(*topology));
  if(!structure) return std::unexpected(structure.error());
  auto routing = RoutingPlan::Create(
      {RouteEntry{NodeId{2}, NodeId{3}, NodeId{3}}}, *structure);
  if(!routing) return std::unexpected(routing.error());
  auto mac = MacPlan::Create(*timing, opportunities);
  if(!mac) return std::unexpected(mac.error());
  return ProtocolCyclePlan::Create(
      std::move(*routing), *timing, std::move(*mac));
}

class CountingSelector final : public IPacketSelector {
 public:
  auto Select(NodeId sender, const PacketQueueStore& queues) const
      -> Result<std::optional<SelectedPacket>> override {
    ++count;
    return fifo_.Select(sender, queues);
  }

  mutable std::size_t count{0};

 private:
  FifoPacketSelector fifo_;
};

class MockTxPhy final : public ITxPhy {
 public:
  auto Encode(const DigitalPacket& packet,
              const TxEncodeRequest& request) const
      -> Result<TxEmission> override {
    ++count;
    return TxEmission::Create(request.transmission_id,
                              packet.packet_id,
                              request.sender_node_id,
                              request.started_at,
                              DurationSeconds(1),
                              25'000.0,
                              4'000.0,
                              180.0);
  }
  mutable std::size_t count{0};
};

class MockChannel final : public IChannelFieldProvider {
 public:
  explicit MockChannel(SimDuration delay) noexcept : delay_(delay) {}
  auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldOutcome> override {
    ++count;
    receivers.push_back(query.receiver_node_id());
    return ChannelFieldResponse::Create(query.transmission_id(),
                                        query.receiver_node_id(),
                                        70.0,
                                        delay_,
                                        {});
  }
  mutable std::size_t count{0};
  mutable std::vector<NodeId> receivers;
 private:
  SimDuration delay_;
};

class MockNoise final : public INoiseFieldProvider {
 public:
  auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    ++count;
    return NoiseObservation::Create(query.receiver_node_id(),
                                    query.observed_from(),
                                    query.observed_until(),
                                    query.lower_frequency_hz(),
                                    query.upper_frequency_hz(),
                                    45.0);
  }
  mutable std::size_t count{0};
};

class MockRxPhy final : public IRxPhy {
 public:
  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    ++count;
    const auto& signal = request.receiver_window().desired_signal();
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  DecodeOutcome::kDecoded);
  }
  mutable std::size_t count{0};
};

class DispatcherSessionSink final : public ITransmissionSessionEventSink {
 public:
  DispatcherSessionSink(EventDispatcher& dispatcher,
                        CycleSignalRuntime& runtime) noexcept
      : dispatcher_(dispatcher), runtime_(runtime) {}

  auto Publish(const TransmissionSession& session) -> Status override {
    ++publish_count;
    received_signal_count += session.received_signals().size();
    for(const auto& signal : session.received_signals()) {
      auto arrival = dispatcher_.Schedule(SignalArrivalEvent{
          signal.first_arrival_at(),
          [this, signal]() -> Status {
            const auto now = dispatcher_.PlatformNow();
            if(!now) return std::unexpected(now.error());
            ++arrival_count;
            return runtime_.HandleSignalArrival(*now, signal);
          }});
      if(!arrival) return std::unexpected(arrival.error());
      auto finalize = dispatcher_.Schedule(SessionFinalizeEvent{
          signal.last_effect_end_at(),
          [this, signal]() -> Status {
            const auto now = dispatcher_.PlatformNow();
            if(!now) return std::unexpected(now.error());
            ++finalize_count;
            return runtime_.HandleSessionFinalize(*now, signal);
          }});
      if(!finalize) return std::unexpected(finalize.error());
    }
    return {};
  }

  std::size_t publish_count{0};
  std::size_t received_signal_count{0};
  std::size_t arrival_count{0};
  std::size_t finalize_count{0};

 private:
  EventDispatcher& dispatcher_;
  CycleSignalRuntime& runtime_;
};

class PlanBoundHook final : public IPlanExecutionHook {
 public:
  PlanBoundHook(PlanBoundTxRuntime& tx_runtime,
                CycleSignalRuntime& signal_runtime) noexcept
      : tx_runtime_(tx_runtime), signal_runtime_(signal_runtime) {}

  auto OnTxStart(const TxOpportunity& opportunity,
                 SimTime now) -> Status override {
    ++tx_start_count;
    auto outcome = tx_runtime_.HandleTxStart(opportunity, now);
    if(!outcome) return std::unexpected(outcome.error());
    if(std::holds_alternative<ExecutedTxStart>(*outcome)) ++executed_count;
    if(std::holds_alternative<UnusedNoPacketTxStart>(*outcome)) {
      ++no_packet_count;
    }
    if(std::holds_alternative<UnusedNoRouteTxStart>(*outcome)) {
      ++no_route_count;
    }
    return {};
  }

  auto OnCycleClose(const CycleTiming& timing,
                    SimTime now) -> Status override {
    ++cycle_close_count;
    if(now != timing.closes_at()) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition, "close time mismatch"});
    }
    return signal_runtime_.HandleCycleClose(now);
  }

  std::size_t tx_start_count{0};
  std::size_t executed_count{0};
  std::size_t no_packet_count{0};
  std::size_t no_route_count{0};
  std::size_t cycle_close_count{0};

 private:
  PlanBoundTxRuntime& tx_runtime_;
  CycleSignalRuntime& signal_runtime_;
};

auto RunCycle(bool broadcast, SimDuration delay, bool expect_failure)
    -> bool {
  auto snapshot = MakeSnapshot();
  auto plan_result = MakePlan(broadcast);
  if(!snapshot || !plan_result) return false;
  std::optional<ProtocolCyclePlan> plan{std::move(*plan_result)};
  WorldStateStore world{*snapshot};
  auto working_result = CycleWorkingState::Create(
      world.current_snapshot(), PlanningCycleId{0}, SimTime::Zero());
  auto queue_result = PacketQueueStore::Create(
      {NodeId{4}, NodeId{2}, NodeId{1}, NodeId{3}});
  if(!working_result || !queue_result) return false;
  auto working = std::move(*working_result);
  auto queues = std::move(*queue_result);
  DigitalPacket packet{PacketId{77},
                       NodeId{2},
                       broadcast ? PacketDestination{BroadcastDestination{}}
                                 : PacketDestination{
                                       UnicastDestination{NodeId{3}}},
                       {std::byte{0x5A}}};
  if(!queues.Enqueue(NodeId{2}, std::move(packet))) return false;

  CommunicationIdAllocator ids{TransmissionId{50}, ReceptionId{100}};
  MockTxPhy tx_phy;
  MockChannel channel{delay};
  MockNoise noise;
  MockRxPhy rx_phy;
  TransmissionExecutor executor{ids, tx_phy, channel};
  ReceiverProcessor receiver{ids, noise, rx_phy};
  CommitService commit{world};
  InFlightSignalLedger ledger;
  ReceptionResultAccumulator results;
  CycleSignalRuntime signal_runtime{executor,
                                    receiver,
                                    working,
                                    commit,
                                    ledger,
                                    results,
                                    SnapshotVersion{0},
                                    Seconds(10)};
  CountingSelector selector;
  TxPreparationService preparation{selector};
  CandidateReceiverResolver candidates;
  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway};
  DispatcherSessionSink event_sink{dispatcher, signal_runtime};
  auto bound_result = PlanBoundTxRuntime::Create(*plan,
                                                 working,
                                                 queues,
                                                 preparation,
                                                 candidates,
                                                 signal_runtime,
                                                 event_sink);
  if(!bound_result) {
    gateway.Destroy();
    return false;
  }
  auto bound = std::move(*bound_result);
  PlanBoundHook hook{bound, signal_runtime};
  PlanInstaller installer{dispatcher};
  CycleCoordinator coordinator{installer, hook};
  const auto installed = coordinator.InstallPlan(*plan, SnapshotVersion{0});
  plan.reset();
  if(!installed) {
    gateway.Destroy();
    return false;
  }

  const auto run = dispatcher.Run();
  const auto queue_size = queues.size(NodeId{2});
  const std::vector<NodeId> expected_receivers{
      NodeId{1}, NodeId{3}, NodeId{4}};
  if(expect_failure) {
    const bool ok = !run &&
                    run.error().code == ErrorCode::kFailedPrecondition &&
                    hook.tx_start_count == 1 && hook.executed_count == 0 &&
                    selector.count == 1 && tx_phy.count == 1 &&
                    channel.count == 3 && event_sink.publish_count == 1 &&
                    event_sink.arrival_count == 0 &&
                    event_sink.finalize_count == 0 && queue_size &&
                    *queue_size == 1 &&
                    world.current_snapshot().version() == SnapshotVersion{0};
    gateway.Destroy();
    return ok;
  }

  const auto sessions = results.sessions();
  const bool ok =
      run && coordinator.state() == CycleCoordinatorState::kCompleted &&
      hook.tx_start_count == 1 && hook.executed_count == 1 &&
      hook.no_packet_count == 0 && hook.no_route_count == 0 &&
      hook.cycle_close_count == 1 && selector.count == 1 &&
      tx_phy.count == 1 && channel.count == 3 &&
      channel.receivers == expected_receivers &&
      event_sink.publish_count == 1 &&
      event_sink.received_signal_count == 3 &&
      event_sink.arrival_count == 3 && event_sink.finalize_count == 3 &&
      noise.count == 3 && rx_phy.count == 3 && sessions.size() == 3 &&
      queue_size && *queue_size == 0 && ledger.empty() &&
      world.current_snapshot().version() == SnapshotVersion{1} &&
      world.current_snapshot().committed_at() == Seconds(10);
  gateway.Destroy();
  return ok;
}

}  // namespace

auto main() -> int {
  return RunCycle(true, DurationSeconds(1), false) &&
                 RunCycle(false, DurationSeconds(1), false) &&
                 RunCycle(true, SimDuration::Zero(), true)
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
