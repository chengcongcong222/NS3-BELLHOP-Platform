#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
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
#include "internal/cycle_signal_runtime.hpp"
#include "internal/cycle_working_state.hpp"
#include "internal/fifo_packet_selector.hpp"
#include "internal/in_flight_signal_ledger.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/packet_selector.hpp"
#include "internal/plan_bound_tx_runtime.hpp"
#include "internal/receiver_processor.hpp"
#include "internal/reception_disposition_applier.hpp"
#include "internal/reception_disposition_service.hpp"
#include "internal/reception_result_accumulator.hpp"
#include "internal/transmission_executor.hpp"
#include "internal/transmission_session_event_sink.hpp"
#include "internal/transmission_record_store.hpp"
#include "internal/tx_preparation.hpp"
#include "internal/world_state_store.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::runtime::internal;

namespace {

constexpr auto At(std::int64_t value) -> SimTime {
  return SimTime::FromNanoseconds(value);
}

constexpr auto For(std::int64_t value) -> SimDuration {
  return SimDuration::FromNanoseconds(value);
}

constexpr auto Node(std::uint64_t id,
                    bool can_receive = true) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, can_receive, DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, 0.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto Packet(std::uint64_t id,
            std::uint64_t source,
            PacketDestination destination) -> DigitalPacket {
  return DigitalPacket{PacketId{id},
                       NodeId{source},
                       std::move(destination),
                       {std::byte{0x01}}};
}

auto Snapshot(std::vector<NodeCommittedState> nodes,
              SnapshotVersion version = SnapshotVersion{0})
    -> Result<WorldSnapshot> {
  return WorldSnapshot::Create(version, SimTime::Zero(), std::move(nodes));
}

auto Routing(std::vector<RouteEntry> entries,
             PlanningCycleId cycle = PlanningCycleId{0},
             SnapshotVersion version = SnapshotVersion{0})
    -> Result<RoutingPlan> {
  const std::vector<NodeId> nodes{
      NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}};
  std::vector<DirectedLink> edges;
  std::vector<LogicalLink> links;
  for(const auto& entry : entries) {
    edges.push_back(
        DirectedLink{entry.forwarding_node_id, entry.next_hop_node_id});
    links.push_back(
        LogicalLink{entry.forwarding_node_id, entry.next_hop_node_id});
  }
  auto graph = ConnectivityGraph::Create(std::move(edges), nodes);
  if(!graph) return std::unexpected(graph.error());
  auto topology = LogicalTopology::Create(std::move(links), nodes, *graph);
  if(!topology) return std::unexpected(topology.error());
  auto roles = RoleTable::Create({}, nodes);
  if(!roles) return std::unexpected(roles.error());
  auto structure = StructureSnapshot::Create(cycle,
                                             version,
                                             std::move(*roles),
                                             std::move(*graph),
                                             std::move(*topology));
  if(!structure) return std::unexpected(structure.error());
  return RoutingPlan::Create(std::move(entries), *structure);
}

auto SchedulePlan(std::vector<TxOpportunity> opportunities,
                  PlanningCycleId cycle = PlanningCycleId{0},
                  SnapshotVersion version = SnapshotVersion{0},
                  SimTime close = At(10)) -> Result<ProtocolCyclePlan> {
  auto timing = CycleTiming::Create(cycle, version, SimTime::Zero(), close);
  if(!timing) return std::unexpected(timing.error());
  return ProtocolCyclePlan::Create(*timing, std::move(opportunities));
}

auto RoutedPlan(std::vector<TxOpportunity> opportunities,
                RoutingPlan routing,
                SimTime close = At(10)) -> Result<ProtocolCyclePlan> {
  auto timing = CycleTiming::Create(routing.cycle_id(),
                                    routing.base_snapshot_version(),
                                    SimTime::Zero(),
                                    close);
  if(!timing) return std::unexpected(timing.error());
  auto mac = MacPlan::Create(*timing, std::move(opportunities));
  if(!mac) return std::unexpected(mac.error());
  return ProtocolCyclePlan::Create(
      std::move(routing), *timing, std::move(*mac));
}

class MockTxPhy final : public ITxPhy {
 public:
  explicit MockTxPhy(SimDuration duration, bool fail = false) noexcept
      : duration_(duration), fail_(fail) {}

  auto Encode(const DigitalPacket& packet,
              const TxEncodeRequest& request) const
      -> Result<TxEmission> override {
    ++count_;
    if(fail_) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable, "fixture Tx PHY failure"});
    }
    return TxEmission::Create(request.transmission_id,
                              packet.packet_id,
                              request.sender_node_id,
                              request.started_at,
                              duration_,
                              25'000.0,
                              4'000.0,
                              180.0);
  }

  mutable std::size_t count_{0};

 private:
  SimDuration duration_;
  bool fail_;
};

class MockChannel final : public IChannelFieldProvider {
 public:
  explicit MockChannel(bool fail = false) noexcept : fail_(fail) {}

  auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldResponse> override {
    ++count_;
    receiver_ids_.push_back(query.receiver_node_id());
    if(fail_) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable, "fixture channel failure"});
    }
    return ChannelFieldResponse::Create(query.transmission_id(),
                                        query.receiver_node_id(),
                                        70.0,
                                        For(1),
                                        {});
  }

  mutable std::size_t count_{0};
  mutable std::vector<NodeId> receiver_ids_;

 private:
  bool fail_;
};

class DummyNoise final : public INoiseFieldProvider {
 public:
  auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    return NoiseObservation::Create(query.receiver_node_id(),
                                    query.observed_from(),
                                    query.observed_until(),
                                    query.lower_frequency_hz(),
                                    query.upper_frequency_hz(),
                                    1.0);
  }
};

class DummyRx final : public IRxPhy {
 public:
  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    const auto& signal = request.receiver_window().desired_signal();
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  DecodeOutcome::kDecoded);
  }
};

class NoopEventSink final : public ITransmissionSessionEventSink {
 public:
  auto Publish(const TransmissionSession&) -> Status override {
    ++count;
    return {};
  }

  std::size_t count{0};
};

class FailingEventSink final : public ITransmissionSessionEventSink {
 public:
  auto Publish(const TransmissionSession&) -> Status override {
    ++count;
    return std::unexpected(
        Error{ErrorCode::kUnavailable, "fixture event publication failure"});
  }

  std::size_t count{0};
};

struct Fixture final {
  WorldStateStore world_store;
  CycleWorkingState working;
  PacketQueueStore queues;
  ApplicationDeliveryStore deliveries;
  CommunicationIdAllocator ids{TransmissionId{1}, ReceptionId{1}};
  MockTxPhy tx_phy;
  MockChannel channel;
  DummyNoise noise;
  DummyRx rx;
  TransmissionExecutor executor;
  ReceiverProcessor receiver;
  CommitService commit;
  InFlightSignalLedger ledger;
  ReceptionResultAccumulator results;
  TransmissionRecordStore records;
  ReceptionDispositionService disposition_service;
  ReceptionDispositionApplier disposition_applier;
  CycleSignalRuntime signal_runtime;
  FifoPacketSelector selector;
  TxPreparationService preparation;
  CandidateReceiverResolver candidates;
  NoopEventSink event_sink;

  Fixture(WorldSnapshot snapshot,
          CycleWorkingState cycle,
          PacketQueueStore packet_queues,
          ApplicationDeliveryStore application_deliveries,
          SimDuration duration = For(1),
          bool channel_failure = false,
          SimTime close = At(10))
      : world_store(snapshot),
        working(std::move(cycle)),
        queues(std::move(packet_queues)),
        deliveries(std::move(application_deliveries)),
        tx_phy(duration),
        channel(channel_failure),
        executor(ids, tx_phy, channel),
        receiver(ids, noise, rx),
        commit(world_store),
        disposition_applier(queues, deliveries),
        signal_runtime(executor,
                       receiver,
                       working,
                       commit,
                       ledger,
                       results,
                       records,
                       disposition_service,
                       disposition_applier,
                       SnapshotVersion{0},
                       close),
        preparation(selector) {}
};

auto MakeFixture(std::vector<NodeCommittedState> nodes,
                 SimDuration duration = For(1),
                 bool channel_failure = false,
                 SimTime close = At(10)) -> std::unique_ptr<Fixture> {
  auto snapshot = Snapshot(std::move(nodes));
  if(!snapshot) return nullptr;
  auto working = CycleWorkingState::Create(
      *snapshot, PlanningCycleId{0}, SimTime::Zero());
  std::vector<NodeId> node_ids;
  for(const auto& node : snapshot->nodes()) node_ids.push_back(node.node_id);
  auto deliveries = ApplicationDeliveryStore::Create(node_ids);
  auto queues = PacketQueueStore::Create(std::move(node_ids));
  if(!working || !queues || !deliveries) return nullptr;
  return std::make_unique<Fixture>(*snapshot,
                                   std::move(*working),
                                   std::move(*queues),
                                   std::move(*deliveries),
                                   duration,
                                   channel_failure,
                                   close);
}

auto TestBindingMembershipTimeAndPlanLifetime() -> bool {
  auto fixture = MakeFixture({Node(0), Node(1), Node(2), Node(3)});
  auto wrong_queues = PacketQueueStore::Create({NodeId{0}, NodeId{1}});
  const auto wrong_cycle = SchedulePlan(
      {TxOpportunity{NodeId{0}, At(1)}}, PlanningCycleId{1});
  const auto wrong_base = SchedulePlan(
      {TxOpportunity{NodeId{0}, At(1)}},
      PlanningCycleId{0},
      SnapshotVersion{1});
  if(!fixture || !wrong_queues || !wrong_cycle || !wrong_base) return false;

  const auto cycle_failure = PlanBoundTxRuntime::Create(
      *wrong_cycle,
      fixture->working,
      fixture->queues,
      fixture->preparation,
      fixture->candidates,
      fixture->signal_runtime,
      fixture->event_sink);
  const auto base_failure = PlanBoundTxRuntime::Create(
      *wrong_base,
      fixture->working,
      fixture->queues,
      fixture->preparation,
      fixture->candidates,
      fixture->signal_runtime,
      fixture->event_sink);
  auto good_plan = SchedulePlan({TxOpportunity{NodeId{0}, At(1)}});
  if(!good_plan) return false;
  const auto universe_failure = PlanBoundTxRuntime::Create(
      *good_plan,
      fixture->working,
      *wrong_queues,
      fixture->preparation,
      fixture->candidates,
      fixture->signal_runtime,
      fixture->event_sink);
  auto bound = [&]() -> Result<PlanBoundTxRuntime> {
    auto original = SchedulePlan({TxOpportunity{NodeId{0}, At(1)}});
    if(!original) return std::unexpected(original.error());
    return PlanBoundTxRuntime::Create(std::move(*original),
                                      fixture->working,
                                      fixture->queues,
                                      fixture->preparation,
                                      fixture->candidates,
                                      fixture->signal_runtime,
                                      fixture->event_sink);
  }();
  if(!bound) return false;
  const auto wrong_opportunity =
      bound->HandleTxStart(TxOpportunity{NodeId{1}, At(1)}, At(1));
  const auto wrong_time =
      bound->HandleTxStart(TxOpportunity{NodeId{0}, At(1)}, At(2));
  const auto no_packet =
      bound->HandleTxStart(TxOpportunity{NodeId{0}, At(1)}, At(1));
  return !cycle_failure && !base_failure && !universe_failure &&
         !wrong_opportunity && !wrong_time && no_packet &&
         std::holds_alternative<UnusedNoPacketTxStart>(*no_packet) &&
         fixture->records.size() == 0;
}

auto TestBroadcastFanOutConsumeAndZeroCandidates() -> bool {
  auto fixture = MakeFixture({Node(3), Node(0), Node(2), Node(1)});
  if(!fixture) return false;
  auto plan = SchedulePlan({TxOpportunity{NodeId{2}, At(1)}});
  if(!plan || !fixture->queues.Enqueue(
                  NodeId{2}, Packet(1, 2, BroadcastDestination{}))) {
    return false;
  }
  auto bound = PlanBoundTxRuntime::Create(std::move(*plan),
                                          fixture->working,
                                          fixture->queues,
                                          fixture->preparation,
                                          fixture->candidates,
                                          fixture->signal_runtime,
                                          fixture->event_sink);
  if(!bound) return false;
  const auto result =
      bound->HandleTxStart(TxOpportunity{NodeId{2}, At(1)}, At(1));
  if(!result || !std::holds_alternative<ExecutedTxStart>(*result)) {
    return false;
  }
  const auto& session = std::get<ExecutedTxStart>(*result).session;
  const std::vector<NodeId> expected{NodeId{0}, NodeId{1}, NodeId{3}};

  auto zero = MakeFixture({Node(0)});
  auto zero_plan = SchedulePlan({TxOpportunity{NodeId{0}, At(1)}});
  if(!zero || !zero_plan || !zero->queues.Enqueue(
                                NodeId{0},
                                Packet(2, 0, BroadcastDestination{}))) {
    return false;
  }
  auto zero_bound = PlanBoundTxRuntime::Create(std::move(*zero_plan),
                                               zero->working,
                                               zero->queues,
                                               zero->preparation,
                                               zero->candidates,
                                               zero->signal_runtime,
                                               zero->event_sink);
  if(!zero_bound) return false;
  const auto zero_result =
      zero_bound->HandleTxStart(TxOpportunity{NodeId{0}, At(1)}, At(1));
  return session.received_signals().size() == 3 &&
         fixture->records.size() == 1 &&
         fixture->channel.receiver_ids_ == expected &&
         *fixture->queues.size(NodeId{2}) == 0 && zero_result &&
         std::holds_alternative<ExecutedTxStart>(*zero_result) &&
         zero->records.size() == 1 &&
         zero->tx_phy.count_ == 1 && zero->channel.count_ == 0 &&
         std::get<ExecutedTxStart>(*zero_result)
             .session.received_signals().empty() &&
         *zero->queues.size(NodeId{0}) == 0;
}

auto TestRelayUnicastFanOutAndNoRouteHeadBlocking() -> bool {
  auto fixture = MakeFixture({Node(0), Node(1), Node(2), Node(3)});
  auto route = Routing(
      {RouteEntry{NodeId{1}, NodeId{3}, NodeId{2}}});
  if(!fixture || !route) return false;
  auto plan = RoutedPlan({TxOpportunity{NodeId{1}, At(1)}}, *route);
  if(!plan || !fixture->queues.Enqueue(
                  NodeId{1},
                  Packet(3, 0, UnicastDestination{NodeId{3}}))) {
    return false;
  }
  auto bound = PlanBoundTxRuntime::Create(std::move(*plan),
                                          fixture->working,
                                          fixture->queues,
                                          fixture->preparation,
                                          fixture->candidates,
                                          fixture->signal_runtime,
                                          fixture->event_sink);
  if(!bound) return false;
  const auto result =
      bound->HandleTxStart(TxOpportunity{NodeId{1}, At(1)}, At(1));
  if(!result || !std::holds_alternative<ExecutedTxStart>(*result)) {
    return false;
  }
  const auto& session = std::get<ExecutedTxStart>(*result).session;
  const auto& transmission = session.transmission();
  const std::vector<NodeId> expected{NodeId{0}, NodeId{2}, NodeId{3}};

  auto blocked = MakeFixture({Node(0), Node(1), Node(2), Node(3)});
  auto only_route = Routing(
      {RouteEntry{NodeId{0}, NodeId{2}, NodeId{2}}});
  if(!blocked || !only_route) return false;
  auto blocked_plan = RoutedPlan(
      {TxOpportunity{NodeId{0}, At(1)},
       TxOpportunity{NodeId{0}, At(2)}},
      *only_route);
  if(!blocked_plan ||
     !blocked->queues.Enqueue(
         NodeId{0}, Packet(4, 0, UnicastDestination{NodeId{3}})) ||
     !blocked->queues.Enqueue(
         NodeId{0}, Packet(5, 0, UnicastDestination{NodeId{2}}))) {
    return false;
  }
  auto blocked_bound = PlanBoundTxRuntime::Create(
      std::move(*blocked_plan),
      blocked->working,
      blocked->queues,
      blocked->preparation,
      blocked->candidates,
      blocked->signal_runtime,
      blocked->event_sink);
  if(!blocked_bound) return false;
  const auto first = blocked_bound->HandleTxStart(
      TxOpportunity{NodeId{0}, At(1)}, At(1));
  const auto second = blocked_bound->HandleTxStart(
      TxOpportunity{NodeId{0}, At(2)}, At(2));
  const auto front = blocked->queues.PeekFront(NodeId{0});
  return session.packet().source_node_id == NodeId{0} &&
         transmission.sender_node_id == NodeId{1} &&
         std::get<UnicastTransmissionTarget>(transmission.target).node_id ==
             NodeId{2} &&
         fixture->channel.receiver_ids_ == expected &&
         *fixture->queues.size(NodeId{1}) == 0 && first && second &&
         std::holds_alternative<UnusedNoRouteTxStart>(*first) &&
         std::holds_alternative<UnusedNoRouteTxStart>(*second) &&
         blocked->tx_phy.count_ == 0 && blocked->records.size() == 0 &&
         *blocked->queues.size(NodeId{0}) == 2 &&
         front && *front && (*front)->packet_id == PacketId{4};
}

auto TestRepeatedSlotsAndExecutionFailuresPreserveQueue() -> bool {
  auto repeated = MakeFixture({Node(0)});
  auto repeated_plan = SchedulePlan(
      {TxOpportunity{NodeId{0}, At(1)},
       TxOpportunity{NodeId{0}, At(2)}});
  if(!repeated || !repeated_plan ||
     !repeated->queues.Enqueue(
         NodeId{0}, Packet(6, 0, BroadcastDestination{})) ||
     !repeated->queues.Enqueue(
         NodeId{0}, Packet(7, 0, BroadcastDestination{}))) {
    return false;
  }
  auto bound = PlanBoundTxRuntime::Create(std::move(*repeated_plan),
                                          repeated->working,
                                          repeated->queues,
                                          repeated->preparation,
                                          repeated->candidates,
                                          repeated->signal_runtime,
                                          repeated->event_sink);
  if(!bound) return false;
  const auto first =
      bound->HandleTxStart(TxOpportunity{NodeId{0}, At(1)}, At(1));
  const auto second =
      bound->HandleTxStart(TxOpportunity{NodeId{0}, At(2)}, At(2));

  auto channel_failure =
      MakeFixture({Node(0), Node(1)}, For(1), true);
  auto failure_plan = SchedulePlan({TxOpportunity{NodeId{0}, At(1)}});
  if(!channel_failure || !failure_plan ||
     !channel_failure->queues.Enqueue(
         NodeId{0}, Packet(8, 0, BroadcastDestination{}))) {
    return false;
  }
  auto failure_bound = PlanBoundTxRuntime::Create(
      std::move(*failure_plan),
      channel_failure->working,
      channel_failure->queues,
      channel_failure->preparation,
      channel_failure->candidates,
      channel_failure->signal_runtime,
      channel_failure->event_sink);
  if(!failure_bound) return false;
  const auto failed = failure_bound->HandleTxStart(
      TxOpportunity{NodeId{0}, At(1)}, At(1));

  auto crossing = MakeFixture({Node(0)}, For(10), false, At(5));
  auto crossing_plan = SchedulePlan(
      {TxOpportunity{NodeId{0}, At(1)}},
      PlanningCycleId{0},
      SnapshotVersion{0},
      At(5));
  if(!crossing || !crossing_plan ||
     !crossing->queues.Enqueue(
         NodeId{0}, Packet(9, 0, BroadcastDestination{}))) {
    return false;
  }
  auto crossing_bound = PlanBoundTxRuntime::Create(
      std::move(*crossing_plan),
      crossing->working,
      crossing->queues,
      crossing->preparation,
      crossing->candidates,
      crossing->signal_runtime,
      crossing->event_sink);
  if(!crossing_bound) return false;
  const auto crossed = crossing_bound->HandleTxStart(
      TxOpportunity{NodeId{0}, At(1)}, At(1));

  return first && second && repeated->tx_phy.count_ == 2 &&
         repeated->records.size() == 2 &&
         *repeated->queues.size(NodeId{0}) == 0 && !failed &&
         failed.error().code == ErrorCode::kUnavailable &&
         channel_failure->records.size() == 0 &&
         channel_failure->event_sink.count == 0 &&
         *channel_failure->queues.size(NodeId{0}) == 1 && !crossed &&
         crossed.error().code == ErrorCode::kFailedPrecondition &&
         crossing->records.size() == 0 &&
         crossing->event_sink.count == 0 &&
         *crossing->queues.size(NodeId{0}) == 1;
}

class WrongPacketSelector final : public IPacketSelector {
 public:
  auto Select(NodeId sender, const PacketQueueStore&) const
      -> Result<std::optional<SelectedPacket>> override {
    return std::optional<SelectedPacket>{SelectedPacket{
        sender, Packet(99, sender.value(), BroadcastDestination{})}};
  }
};

auto TestPostExecutionConsumeFailureIsFatal() -> bool {
  auto fixture = MakeFixture({Node(0)});
  auto plan = SchedulePlan({TxOpportunity{NodeId{0}, At(1)}});
  if(!fixture || !plan || !fixture->queues.Enqueue(
                            NodeId{0},
                            Packet(10, 0, BroadcastDestination{}))) {
    return false;
  }
  const WrongPacketSelector wrong_selector;
  const TxPreparationService wrong_preparation{wrong_selector};
  auto bound = PlanBoundTxRuntime::Create(std::move(*plan),
                                          fixture->working,
                                          fixture->queues,
                                          wrong_preparation,
                                          fixture->candidates,
                                          fixture->signal_runtime,
                                          fixture->event_sink);
  if(!bound) return false;
  const auto result =
      bound->HandleTxStart(TxOpportunity{NodeId{0}, At(1)}, At(1));
  const auto front = fixture->queues.PeekFront(NodeId{0});
  return !result &&
         result.error().code == ErrorCode::kFailedPrecondition &&
         fixture->tx_phy.count_ == 1 && fixture->records.size() == 1 &&
         front && *front &&
         (*front)->packet_id == PacketId{10};
}

auto TestPublicationFailureRetainsRecordAndQueue() -> bool {
  auto fixture = MakeFixture({Node(0)});
  auto plan = SchedulePlan({TxOpportunity{NodeId{0}, At(1)}});
  if(!fixture || !plan || !fixture->queues.Enqueue(
                            NodeId{0},
                            Packet(11, 0, BroadcastDestination{}))) {
    return false;
  }
  FailingEventSink event_sink;
  auto bound = PlanBoundTxRuntime::Create(std::move(*plan),
                                          fixture->working,
                                          fixture->queues,
                                          fixture->preparation,
                                          fixture->candidates,
                                          fixture->signal_runtime,
                                          event_sink);
  if(!bound) return false;

  const auto result =
      bound->HandleTxStart(TxOpportunity{NodeId{0}, At(1)}, At(1));
  return !result && result.error().code == ErrorCode::kUnavailable &&
         event_sink.count == 1 && fixture->records.size() == 1 &&
         *fixture->queues.size(NodeId{0}) == 1;
}

auto TestRegistrationFailurePreventsPublicationAndConsume() -> bool {
  auto fixture = MakeFixture({Node(0)});
  auto plan = SchedulePlan({TxOpportunity{NodeId{0}, At(1)}});
  const auto packet = Packet(12, 0, BroadcastDestination{});
  auto existing = TransmissionRecord::Create(
      packet,
      Transmission{TransmissionId{1},
                   PacketId{12},
                   NodeId{0},
                   BroadcastTransmissionTarget{},
                   At(1),
                   At(2)});
  if(!fixture || !plan || !existing ||
     !fixture->records.Register(std::move(*existing)) ||
     !fixture->queues.Enqueue(NodeId{0}, packet)) {
    return false;
  }
  auto bound = PlanBoundTxRuntime::Create(std::move(*plan),
                                          fixture->working,
                                          fixture->queues,
                                          fixture->preparation,
                                          fixture->candidates,
                                          fixture->signal_runtime,
                                          fixture->event_sink);
  if(!bound) return false;

  const auto result =
      bound->HandleTxStart(TxOpportunity{NodeId{0}, At(1)}, At(1));
  return !result && result.error().code == ErrorCode::kAlreadyExists &&
         fixture->records.size() == 1 && fixture->event_sink.count == 0 &&
         *fixture->queues.size(NodeId{0}) == 1;
}

}  // namespace

auto main() -> int {
  return TestBindingMembershipTimeAndPlanLifetime() &&
                 TestBroadcastFanOutConsumeAndZeroCandidates() &&
                 TestRelayUnicastFanOutAndNoRouteHeadBlocking() &&
                 TestRepeatedSlotsAndExecutionFailuresPreserveQueue() &&
                 TestPostExecutionConsumeFailureIsFatal() &&
                 TestPublicationFailureRetainsRecordAndQueue() &&
                 TestRegistrationFailurePreventsPublicationAndConsume()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
