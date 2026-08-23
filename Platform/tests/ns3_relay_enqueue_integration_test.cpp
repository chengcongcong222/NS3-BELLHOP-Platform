#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
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

#include "internal/application_delivery_store.hpp"
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
#include "internal/reception_disposition_applier.hpp"
#include "internal/reception_disposition_service.hpp"
#include "internal/reception_result_accumulator.hpp"
#include "internal/transmission_executor.hpp"
#include "internal/transmission_record_store.hpp"
#include "internal/transmission_session_event_sink.hpp"
#include "internal/tx_preparation.hpp"
#include "internal/world_state_store.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::kernel::internal;
using namespace ns3_factory::runtime::internal;

namespace {

enum class Scenario {
  kRelay,
  kFinalDelivery,
  kBroadcast,
  kTargetNotDecoded,
};

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
      {Node(3), Node(0), Node(2), Node(1)});
}

auto MakePlan(Scenario scenario) -> Result<ProtocolCyclePlan> {
  auto timing = CycleTiming::Create(PlanningCycleId{0},
                                    SnapshotVersion{0},
                                    SimTime::Zero(),
                                    Seconds(10));
  if(!timing) return std::unexpected(timing.error());
  const auto sender = scenario == Scenario::kFinalDelivery
                          ? NodeId{1}
                          : NodeId{0};
  const std::vector<TxOpportunity> opportunities{
      TxOpportunity{sender, Seconds(1)}};
  if(scenario == Scenario::kBroadcast) {
    return ProtocolCyclePlan::Create(*timing, opportunities);
  }

  const auto next_hop = scenario == Scenario::kFinalDelivery
                            ? NodeId{3}
                            : NodeId{1};
  const std::vector<NodeId> nodes{
      NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}};
  auto graph = ConnectivityGraph::Create(
      {DirectedLink{sender, next_hop}}, nodes);
  if(!graph) return std::unexpected(graph.error());
  auto topology = LogicalTopology::Create(
      {LogicalLink{sender, next_hop}}, nodes, *graph);
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
      {RouteEntry{sender, NodeId{3}, next_hop}}, *structure);
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
  auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldOutcome> override {
    ++count;
    receivers.push_back(query.receiver_node_id());
    return ChannelFieldResponse::Create(query.transmission_id(),
                                        query.receiver_node_id(),
                                        70.0,
                                        DurationSeconds(1),
                                        {});
  }

  mutable std::size_t count{0};
  mutable std::vector<NodeId> receivers;
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
  explicit MockRxPhy(Scenario scenario) noexcept : scenario_(scenario) {}

  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    ++count;
    const auto& signal = request.receiver_window().desired_signal();
    const auto outcome =
        scenario_ == Scenario::kTargetNotDecoded &&
                signal.receiver_node_id() == NodeId{1}
            ? DecodeOutcome::kNotDecoded
            : DecodeOutcome::kDecoded;
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  outcome);
  }

  mutable std::size_t count{0};

 private:
  Scenario scenario_;
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
                CycleSignalRuntime& signal_runtime,
                const TransmissionRecordStore& records) noexcept
      : tx_runtime_(tx_runtime),
        signal_runtime_(signal_runtime),
        records_(records) {}

  auto OnTxStart(const TxOpportunity& opportunity,
                 SimTime now) -> Status override {
    ++tx_start_count;
    auto outcome = tx_runtime_.get().HandleTxStart(opportunity, now);
    if(!outcome) return std::unexpected(outcome.error());
    if(std::holds_alternative<ExecutedTxStart>(*outcome)) ++executed_count;
    return {};
  }

  auto OnCycleClose(const CycleTiming& timing,
                    SimTime now) -> Status override {
    ++cycle_close_count;
    records_at_close = records_.get().size();
    if(now != timing.closes_at()) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition, "close time mismatch"});
    }
    return signal_runtime_.get().HandleCycleClose(now);
  }

  std::size_t tx_start_count{0};
  std::size_t executed_count{0};
  std::size_t cycle_close_count{0};
  std::size_t records_at_close{0};

 private:
  std::reference_wrapper<PlanBoundTxRuntime> tx_runtime_;
  std::reference_wrapper<CycleSignalRuntime> signal_runtime_;
  std::reference_wrapper<const TransmissionRecordStore> records_;
};

auto QueuesEmpty(const PacketQueueStore& queues) -> bool {
  for(const auto node : queues.node_ids()) {
    const auto size = queues.size(node);
    if(!size || *size != 0) return false;
  }
  return true;
}

auto RunScenario(Scenario scenario) -> bool {
  auto snapshot = MakeSnapshot();
  auto plan_result = MakePlan(scenario);
  if(!snapshot || !plan_result) return false;
  std::optional<ProtocolCyclePlan> plan{std::move(*plan_result)};
  WorldStateStore world{*snapshot};
  auto working_result = CycleWorkingState::Create(
      world.current_snapshot(), PlanningCycleId{0}, SimTime::Zero());
  auto queue_result = PacketQueueStore::Create(
      {NodeId{3}, NodeId{0}, NodeId{2}, NodeId{1}});
  auto delivery_result = ApplicationDeliveryStore::Create(
      {NodeId{3}, NodeId{0}, NodeId{2}, NodeId{1}});
  if(!working_result || !queue_result || !delivery_result) return false;
  auto working = std::move(*working_result);
  auto queues = std::move(*queue_result);
  auto deliveries = std::move(*delivery_result);
  const auto sender = scenario == Scenario::kFinalDelivery
                          ? NodeId{1}
                          : NodeId{0};
  const DigitalPacket expected_packet{
      PacketId{77},
      NodeId{0},
      scenario == Scenario::kBroadcast
          ? PacketDestination{BroadcastDestination{}}
          : PacketDestination{UnicastDestination{NodeId{3}}},
      {std::byte{0xA5}, std::byte{0x5A}}};
  if(!queues.Enqueue(sender, expected_packet)) return false;

  CommunicationIdAllocator ids{TransmissionId{50}, ReceptionId{100}};
  MockTxPhy tx_phy;
  MockChannel channel;
  MockNoise noise;
  MockRxPhy rx_phy{scenario};
  TransmissionExecutor executor{ids, tx_phy, channel};
  ReceiverProcessor receiver{ids, noise, rx_phy};
  CommitService commit{world};
  InFlightSignalLedger ledger;
  ReceptionResultAccumulator results;
  TransmissionRecordStore records;
  ReceptionDispositionService disposition_service;
  ReceptionDispositionApplier disposition_applier{queues, deliveries};
  CycleSignalRuntime signal_runtime{executor,
                                    receiver,
                                    working,
                                    commit,
                                    ledger,
                                    results,
                                    records,
                                    disposition_service,
                                    disposition_applier,
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
  PlanBoundHook hook{bound, signal_runtime, records};
  PlanInstaller installer{dispatcher};
  CycleCoordinator coordinator{installer, hook};
  const auto installed = coordinator.InstallPlan(*plan, SnapshotVersion{0});
  plan.reset();
  if(!installed) {
    gateway.Destroy();
    return false;
  }

  const auto run = dispatcher.Run();
  const std::vector<NodeId> expected_receivers =
      sender == NodeId{0}
          ? std::vector<NodeId>{NodeId{1}, NodeId{2}, NodeId{3}}
          : std::vector<NodeId>{NodeId{0}, NodeId{2}, NodeId{3}};
  const bool common =
      run && coordinator.state() == CycleCoordinatorState::kCompleted &&
      hook.tx_start_count == 1 && hook.executed_count == 1 &&
      hook.cycle_close_count == 1 && hook.records_at_close == 1 &&
      selector.count == 1 && tx_phy.count == 1 && channel.count == 3 &&
      channel.receivers == expected_receivers &&
      event_sink.publish_count == 1 &&
      event_sink.received_signal_count == 3 &&
      event_sink.arrival_count == 3 && event_sink.finalize_count == 3 &&
      noise.count == 3 && rx_phy.count == 3 &&
      results.sessions().size() == 3 && records.size() == 0 &&
      ledger.empty() && world.current_snapshot().version() == SnapshotVersion{1};
  bool outcome = false;
  if(common && scenario == Scenario::kRelay) {
    const auto relayed = queues.PeekFront(NodeId{1});
    outcome = relayed && *relayed && **relayed == expected_packet &&
              *queues.size(NodeId{0}) == 0 &&
              *queues.size(NodeId{2}) == 0 &&
              *queues.size(NodeId{3}) == 0 && deliveries.size() == 0;
  } else if(common && scenario == Scenario::kFinalDelivery) {
    outcome = QueuesEmpty(queues) && deliveries.size() == 1 &&
              deliveries.deliveries().front().receiver_node_id == NodeId{3} &&
              deliveries.deliveries().front().packet == expected_packet;
  } else if(common && scenario == Scenario::kBroadcast) {
    outcome = QueuesEmpty(queues) && deliveries.size() == 3;
  } else if(common && scenario == Scenario::kTargetNotDecoded) {
    outcome = QueuesEmpty(queues) && deliveries.size() == 0;
  }
  gateway.Destroy();
  return outcome;
}

}  // namespace

auto main() -> int {
  return RunScenario(Scenario::kRelay) &&
                 RunScenario(Scenario::kFinalDelivery) &&
                 RunScenario(Scenario::kBroadcast) &&
                 RunScenario(Scenario::kTargetNotDecoded)
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
