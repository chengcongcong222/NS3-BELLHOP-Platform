#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/link_feasibility.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/all_feasible_links_topology_policy.hpp"
#include "internal/application_delivery_store.hpp"
#include "internal/candidate_receiver_resolver.hpp"
#include "internal/commit_service.hpp"
#include "internal/communication_id_allocator.hpp"
#include "internal/composite_protocol_cycle_planner.hpp"
#include "internal/configured_role_assignment_policy.hpp"
#include "internal/configured_tdma_mac_planner.hpp"
#include "internal/configured_tdma_policy.hpp"
#include "internal/connectivity_decision_policy.hpp"
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
#include "internal/shortest_path_to_sink_routing_planner.hpp"
#include "internal/structure_builder.hpp"
#include "internal/transmission_executor.hpp"
#include "internal/transmission_record_store.hpp"
#include "internal/transmission_session_event_sink.hpp"
#include "internal/tx_preparation.hpp"
#include "internal/world_state_store.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::kernel::internal;
using namespace ns3_factory::planning::internal;
using namespace ns3_factory::runtime::internal;
using namespace ns3_factory::structure::internal;

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

auto InitialSnapshot() -> Result<WorldSnapshot> {
  return WorldSnapshot::Create(
      SnapshotVersion{0},
      SimTime::Zero(),
      {Node(3), Node(0), Node(2), Node(1)});
}

class ChainFeasibilityEstimator final : public ILinkFeasibilityEstimator {
 public:
  auto Estimate(const LinkFeasibilityQuery& query) const
      -> Result<LinkFeasibilityEstimate> override {
    observed_at.push_back(query.observed_at());
    const bool accepted =
        (query.source_node_id() == NodeId{0} &&
         query.target_node_id() == NodeId{1}) ||
        (query.source_node_id() == NodeId{1} &&
         query.target_node_id() == NodeId{2}) ||
        (query.source_node_id() == NodeId{2} &&
         query.target_node_id() == NodeId{3});
    return LinkFeasibilityEstimate::Create(query.source_node_id(),
                                           query.target_node_id(),
                                           query.observed_at(),
                                           accepted ? 1.0 : 0.0);
  }

  mutable std::vector<SimTime> observed_at;
};

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
  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    ++count;
    const auto& signal = request.receiver_window().desired_signal();
    const auto outcome = not_decoded_receiver_ &&
                                 signal.receiver_node_id() ==
                                     *not_decoded_receiver_
                             ? DecodeOutcome::kNotDecoded
                             : DecodeOutcome::kDecoded;
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  outcome);
  }

  auto SetNotDecodedReceiver(std::optional<NodeId> receiver) noexcept
      -> void {
    not_decoded_receiver_ = receiver;
  }

  mutable std::size_t count{0};

 private:
  std::optional<NodeId> not_decoded_receiver_;
};

struct HopAuditRecord final {
  PlanningCycleId cycle_id;
  DigitalPacket packet;
  Transmission transmission;
};

struct CycleAudit final {
  PlanningCycleId cycle_id;
  SnapshotVersion base_version;
  SimTime starts_at;
  SimTime closes_at;
  std::size_t tx_opportunity_count;
  std::size_t selector_count;
  std::size_t executed_count;
  std::size_t no_packet_count;
  std::size_t encode_count;
  std::size_t channel_query_count;
  std::size_t reception_count;
  std::size_t records_at_publish;
  std::size_t records_visible_at_finalize;
  std::size_t records_at_close;
  std::size_t records_after_close;
};

class AuditingSessionSink final : public ITransmissionSessionEventSink {
 public:
  AuditingSessionSink(EventDispatcher& dispatcher,
                      CycleSignalRuntime& runtime,
                      const TransmissionRecordStore& records,
                      PlanningCycleId cycle_id,
                      std::vector<HopAuditRecord>& hop_audit) noexcept
      : dispatcher_(dispatcher),
        runtime_(runtime),
        records_(records),
        cycle_id_(cycle_id),
        hop_audit_(hop_audit) {}

  auto Publish(const TransmissionSession& session) -> Status override {
    ++publish_count;
    records_at_publish = records_.get().size();
    if(!records_.get().Find(session.transmission().transmission_id)) {
      return std::unexpected(
          Error{ErrorCode::kNotFound,
                "TransmissionRecord is missing during event publication"});
    }
    hop_audit_.get().push_back(
        HopAuditRecord{cycle_id_, session.packet(), session.transmission()});
    for(const auto& signal : session.received_signals()) {
      auto arrival = dispatcher_.get().Schedule(SignalArrivalEvent{
          signal.first_arrival_at(),
          [this, signal]() -> Status {
            const auto now = dispatcher_.get().PlatformNow();
            if(!now) return std::unexpected(now.error());
            ++arrival_count;
            return runtime_.get().HandleSignalArrival(*now, signal);
          }});
      if(!arrival) return std::unexpected(arrival.error());
      auto finalize = dispatcher_.get().Schedule(SessionFinalizeEvent{
          signal.last_effect_end_at(),
          [this, signal]() -> Status {
            const auto now = dispatcher_.get().PlatformNow();
            if(!now) return std::unexpected(now.error());
            if(!records_.get().Find(signal.transmission_id())) {
              return std::unexpected(
                  Error{ErrorCode::kNotFound,
                        "TransmissionRecord is missing during finalize"});
            }
            ++records_visible_at_finalize;
            ++finalize_count;
            return runtime_.get().HandleSessionFinalize(*now, signal);
          }});
      if(!finalize) return std::unexpected(finalize.error());
    }
    return {};
  }

  std::size_t publish_count{0};
  std::size_t arrival_count{0};
  std::size_t finalize_count{0};
  std::size_t records_at_publish{0};
  std::size_t records_visible_at_finalize{0};

 private:
  std::reference_wrapper<EventDispatcher> dispatcher_;
  std::reference_wrapper<CycleSignalRuntime> runtime_;
  std::reference_wrapper<const TransmissionRecordStore> records_;
  PlanningCycleId cycle_id_;
  std::reference_wrapper<std::vector<HopAuditRecord>> hop_audit_;
};

class CycleRuntimeHook final : public IPlanExecutionHook {
 public:
  CycleRuntimeHook(PlanBoundTxRuntime& tx_runtime,
                   CycleSignalRuntime& signal_runtime,
                   const TransmissionRecordStore& records) noexcept
      : tx_runtime_(tx_runtime),
        signal_runtime_(signal_runtime),
        records_(records) {}

  auto OnTxStart(const TxOpportunity& opportunity,
                 SimTime now) -> Status override {
    auto outcome = tx_runtime_.get().HandleTxStart(opportunity, now);
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
    records_at_close = records_.get().size();
    return signal_runtime_.get().HandleCycleClose(now);
  }

  std::size_t executed_count{0};
  std::size_t no_packet_count{0};
  std::size_t no_route_count{0};
  std::size_t records_at_close{0};

 private:
  std::reference_wrapper<PlanBoundTxRuntime> tx_runtime_;
  std::reference_wrapper<CycleSignalRuntime> signal_runtime_;
  std::reference_wrapper<const TransmissionRecordStore> records_;
};

class MultiCycleHarness final {
 public:
  MultiCycleHarness(WorldSnapshot initial_snapshot,
                    PacketQueueStore queues,
                    ApplicationDeliveryStore deliveries,
                    ConnectivityDecisionPolicy connectivity_policy)
      : world_(std::move(initial_snapshot)),
        queues_(std::move(queues)),
        deliveries_(std::move(deliveries)),
        ids_(TransmissionId{100}, ReceptionId{1'000}),
        connectivity_policy_(std::move(connectivity_policy)),
        roles_({RoleBinding{NodeId{0}, ProtocolRole::kMember},
                RoleBinding{NodeId{1}, ProtocolRole::kRelay},
                RoleBinding{NodeId{2}, ProtocolRole::kRelay},
                RoleBinding{NodeId{3}, ProtocolRole::kSink}}),
        structure_builder_(connectivity_policy_,
                           estimator_,
                           roles_,
                           topology_) {}

  auto EnqueueInitialPacket(DigitalPacket packet) -> Status {
    expected_packet_ = packet;
    return queues_.Enqueue(NodeId{0}, std::move(packet));
  }

  auto RunCycle(PlanningCycleId cycle_id,
                NodeId slot_owner,
                std::optional<NodeId> not_decoded_receiver,
                bool expect_physical_send) -> bool;

  [[nodiscard]] auto ExpectQueues(
      std::optional<NodeId> packet_owner) const -> bool {
    for(const auto node : queues_.node_ids()) {
      const auto size = queues_.size(node);
      const auto front = queues_.PeekFront(node);
      if(!size || !front) return false;
      if(packet_owner && node == *packet_owner) {
        if(!expected_packet_ || *size != 1 || !*front ||
           **front != *expected_packet_) {
          return false;
        }
      } else if(*size != 0 || *front) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] auto world() const noexcept -> const WorldStateStore& {
    return world_;
  }

  [[nodiscard]] auto deliveries() const noexcept
      -> const ApplicationDeliveryStore& {
    return deliveries_;
  }

  [[nodiscard]] auto hop_audit() const noexcept
      -> const std::vector<HopAuditRecord>& {
    return hop_audit_;
  }

  [[nodiscard]] auto cycle_audit() const noexcept
      -> const std::vector<CycleAudit>& {
    return cycle_audit_;
  }

  [[nodiscard]] auto reception_ids() const noexcept
      -> const std::vector<ReceptionId>& {
    return reception_ids_;
  }

 private:
  [[nodiscard]] auto ValidateStructureAndPlan(
      PlanningCycleId cycle_id,
      const WorldSnapshot& snapshot,
      const StructureSnapshot& structure,
      const ProtocolCyclePlan& plan,
      NodeId slot_owner) const -> bool;

  WorldStateStore world_;
  PacketQueueStore queues_;
  ApplicationDeliveryStore deliveries_;
  CommunicationIdAllocator ids_;
  ChainFeasibilityEstimator estimator_;
  ConnectivityDecisionPolicy connectivity_policy_;
  ConfiguredRoleAssignmentPolicy roles_;
  AllFeasibleLinksTopologyPolicy topology_;
  StructureBuilder structure_builder_;
  ShortestPathToSinkRoutingPlanner routing_planner_;
  MockTxPhy tx_phy_;
  MockChannel channel_;
  MockNoise noise_;
  MockRxPhy rx_phy_;
  std::optional<DigitalPacket> expected_packet_;
  std::vector<HopAuditRecord> hop_audit_;
  std::vector<CycleAudit> cycle_audit_;
  std::vector<ReceptionId> reception_ids_;
};

auto MultiCycleHarness::ValidateStructureAndPlan(
    PlanningCycleId cycle_id,
    const WorldSnapshot& snapshot,
    const StructureSnapshot& structure,
    const ProtocolCyclePlan& plan,
    NodeId slot_owner) const -> bool {
  const auto& routing = plan.routing_plan();
  if(!routing) return false;
  const auto opportunities = plan.mac_plan().tx_opportunities();
  const auto& graph = structure.connectivity_graph();
  const auto& logical = structure.logical_topology();
  const auto n0 = routing->FindNextHop(NodeId{0}, NodeId{3});
  const auto n1 = routing->FindNextHop(NodeId{1}, NodeId{3});
  const auto n2 = routing->FindNextHop(NodeId{2}, NodeId{3});
  return structure.cycle_id() == cycle_id &&
         structure.base_snapshot_version() == snapshot.version() &&
         routing->cycle_id() == cycle_id &&
         routing->base_snapshot_version() == snapshot.version() &&
         plan.timing().cycle_id() == cycle_id &&
         plan.timing().base_snapshot_version() == snapshot.version() &&
         plan.timing().starts_at() == snapshot.committed_at() &&
         structure.role_table().HasRole(NodeId{3}, ProtocolRole::kSink) &&
         graph.edges().size() == 3 &&
         graph.HasEdge(NodeId{0}, NodeId{1}) &&
         graph.HasEdge(NodeId{1}, NodeId{2}) &&
         graph.HasEdge(NodeId{2}, NodeId{3}) &&
         logical.links().size() == 3 &&
         logical.HasLink(NodeId{0}, NodeId{1}) &&
         logical.HasLink(NodeId{1}, NodeId{2}) &&
         logical.HasLink(NodeId{2}, NodeId{3}) &&
         n0 && *n0 == NodeId{1} &&
         n1 && *n1 == NodeId{2} &&
         n2 && *n2 == NodeId{3} &&
         opportunities.size() == 1 &&
         opportunities.front().sender_node_id == slot_owner &&
         opportunities.front().eligible_at == snapshot.committed_at();
}

auto MultiCycleHarness::RunCycle(
    PlanningCycleId cycle_id,
    NodeId slot_owner,
    std::optional<NodeId> not_decoded_receiver,
    bool expect_physical_send) -> bool {
  const auto& snapshot = world_.current_snapshot();
  const auto base_version = snapshot.version();
  const auto starts_at = snapshot.committed_at();
  const auto estimator_start = estimator_.observed_at.size();
  auto structure = structure_builder_.Build(
      StructureBuildRequest{cycle_id, snapshot});
  auto tdma_policy = ConfiguredTdmaPolicy::Create(
      DurationSeconds(10), {slot_owner});
  if(!structure || !tdma_policy) return false;
  ConfiguredTdmaMacPlanner mac_planner{std::move(*tdma_policy)};
  CompositeProtocolCyclePlanner planner{routing_planner_, mac_planner};
  auto plan = planner.Build(snapshot, *structure);
  if(!plan || !ValidateStructureAndPlan(
                  cycle_id, snapshot, *structure, *plan, slot_owner)) {
    return false;
  }
  if(estimator_.observed_at.size() != estimator_start + 12 ||
     !std::all_of(estimator_.observed_at.begin() +
                      static_cast<std::ptrdiff_t>(estimator_start),
                  estimator_.observed_at.end(),
                  [&](SimTime observed) { return observed == starts_at; })) {
    return false;
  }

  auto working = CycleWorkingState::Create(snapshot, cycle_id, starts_at);
  if(!working) return false;
  TransmissionExecutor executor{ids_, tx_phy_, channel_};
  rx_phy_.SetNotDecodedReceiver(not_decoded_receiver);
  ReceiverProcessor receiver{ids_, noise_, rx_phy_};
  CommitService commit{world_};
  InFlightSignalLedger ledger;
  ReceptionResultAccumulator results;
  TransmissionRecordStore records;
  ReceptionDispositionService disposition_service;
  ReceptionDispositionApplier disposition_applier{queues_, deliveries_};
  CycleSignalRuntime signal_runtime{executor,
                                    receiver,
                                    *working,
                                    commit,
                                    ledger,
                                    results,
                                    records,
                                    disposition_service,
                                    disposition_applier,
                                    base_version,
                                    plan->timing().closes_at()};
  CountingSelector selector;
  TxPreparationService preparation{selector};
  CandidateReceiverResolver candidates;
  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway};
  AuditingSessionSink event_sink{
      dispatcher, signal_runtime, records, cycle_id, hop_audit_};
  auto tx_runtime = PlanBoundTxRuntime::Create(*plan,
                                               *working,
                                               queues_,
                                               preparation,
                                               candidates,
                                               signal_runtime,
                                               event_sink);
  if(!tx_runtime) {
    gateway.Destroy();
    return false;
  }
  CycleRuntimeHook hook{*tx_runtime, signal_runtime, records};
  PlanInstaller installer{dispatcher};
  CycleCoordinator coordinator{installer, hook};
  const auto installed = coordinator.InstallPlan(*plan, base_version);
  if(!installed) {
    gateway.Destroy();
    return false;
  }

  const auto encode_before = tx_phy_.count;
  const auto channel_before = channel_.count;
  const auto receiver_audit_before = channel_.receivers.size();
  const auto noise_before = noise_.count;
  const auto rx_before = rx_phy_.count;
  const auto run = dispatcher.Run();
  const auto sessions = results.sessions();
  for(const auto& session : sessions) {
    reception_ids_.push_back(session.reception().reception_id);
  }
  std::vector<NodeId> expected_receivers{
      NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}};
  std::erase(expected_receivers, slot_owner);
  const bool candidate_order =
      channel_.receivers.size() ==
          receiver_audit_before +
              (expect_physical_send ? expected_receivers.size() : 0) &&
      (!expect_physical_send ||
       std::equal(expected_receivers.begin(),
                  expected_receivers.end(),
                  channel_.receivers.begin() +
                      static_cast<std::ptrdiff_t>(receiver_audit_before)));
  const auto not_decoded_count = static_cast<std::size_t>(std::count_if(
      sessions.begin(), sessions.end(), [](const ReceptionSession& session) {
        return session.decode_result().outcome() ==
               DecodeOutcome::kNotDecoded;
      }));
  const bool decode_outcomes =
      not_decoded_receiver
          ? not_decoded_count == 1 &&
                std::any_of(
                    sessions.begin(),
                    sessions.end(),
                    [&](const ReceptionSession& session) {
                      return session.reception().receiver_node_id ==
                                 *not_decoded_receiver &&
                             session.decode_result().outcome() ==
                                 DecodeOutcome::kNotDecoded;
                    })
          : not_decoded_count == 0;
  const bool expected_counts =
      expect_physical_send
          ? hook.executed_count == 1 && hook.no_packet_count == 0 &&
                event_sink.publish_count == 1 &&
                event_sink.arrival_count == 3 &&
                event_sink.finalize_count == 3 &&
                event_sink.records_at_publish == 1 &&
                event_sink.records_visible_at_finalize == 3 &&
                hook.records_at_close == 1 && sessions.size() == 3 &&
                tx_phy_.count == encode_before + 1 &&
                channel_.count == channel_before + 3 &&
                noise_.count == noise_before + 3 &&
                rx_phy_.count == rx_before + 3
          : hook.executed_count == 0 && hook.no_packet_count == 1 &&
                event_sink.publish_count == 0 &&
                event_sink.arrival_count == 0 &&
                event_sink.finalize_count == 0 &&
                hook.records_at_close == 0 && sessions.empty() &&
                tx_phy_.count == encode_before &&
                channel_.count == channel_before &&
                noise_.count == noise_before && rx_phy_.count == rx_before;
  const bool completed =
      run && expected_counts && candidate_order && decode_outcomes &&
      hook.no_route_count == 0 &&
      selector.count == 1 &&
      coordinator.state() == CycleCoordinatorState::kCompleted &&
      records.size() == 0 && ledger.empty() &&
      world_.current_snapshot().version().value() ==
          base_version.value() + 1 &&
      world_.current_snapshot().committed_at() == plan->timing().closes_at();
  cycle_audit_.push_back(CycleAudit{cycle_id,
                                   base_version,
                                   starts_at,
                                   plan->timing().closes_at(),
                                   plan->mac_plan().tx_opportunities().size(),
                                   selector.count,
                                   hook.executed_count,
                                   hook.no_packet_count,
                                   tx_phy_.count - encode_before,
                                   channel_.count - channel_before,
                                   sessions.size(),
                                   event_sink.records_at_publish,
                                   event_sink.records_visible_at_finalize,
                                   hook.records_at_close,
                                   records.size()});
  gateway.Destroy();
  return completed;
}

auto MakeHarness() -> std::unique_ptr<MultiCycleHarness> {
  auto snapshot = InitialSnapshot();
  auto queues = PacketQueueStore::Create(
      {NodeId{3}, NodeId{0}, NodeId{2}, NodeId{1}});
  auto deliveries = ApplicationDeliveryStore::Create(
      {NodeId{3}, NodeId{0}, NodeId{2}, NodeId{1}});
  auto decision = ConnectivityDecisionPolicy::Create(
      std::nullopt, 0.5, 0.5);
  if(!snapshot || !queues || !deliveries || !decision) return nullptr;
  return std::make_unique<MultiCycleHarness>(*snapshot,
                                             std::move(*queues),
                                             std::move(*deliveries),
                                             std::move(*decision));
}

auto TestHappyPath() -> bool {
  auto harness = MakeHarness();
  const DigitalPacket packet{PacketId{10},
                             NodeId{0},
                             UnicastDestination{NodeId{3}},
                             {std::byte{0x10},
                              std::byte{0x20},
                              std::byte{0x30}}};
  if(!harness || !harness->EnqueueInitialPacket(packet) ||
     !harness->ExpectQueues(NodeId{0}) ||
     !harness->RunCycle(
         PlanningCycleId{0}, NodeId{0}, std::nullopt, true) ||
     !harness->ExpectQueues(NodeId{1}) ||
     harness->deliveries().size() != 0 ||
     !harness->RunCycle(
         PlanningCycleId{1}, NodeId{1}, std::nullopt, true) ||
     !harness->ExpectQueues(NodeId{2}) ||
     harness->deliveries().size() != 0 ||
     !harness->RunCycle(
         PlanningCycleId{2}, NodeId{2}, std::nullopt, true) ||
     !harness->ExpectQueues(std::nullopt)) {
    return false;
  }

  const auto& hops = harness->hop_audit();
  const auto& cycles = harness->cycle_audit();
  const auto& receptions = harness->reception_ids();
  if(hops.size() != 3 || cycles.size() != 3 || receptions.size() != 9 ||
     harness->deliveries().size() != 1 ||
     harness->world().current_snapshot().version() != SnapshotVersion{3} ||
     harness->world().current_snapshot().committed_at() != Seconds(30)) {
    return false;
  }

  const std::vector<NodeId> senders{NodeId{0}, NodeId{1}, NodeId{2}};
  const std::vector<NodeId> targets{NodeId{1}, NodeId{2}, NodeId{3}};
  for(std::size_t index = 0; index < hops.size(); ++index) {
    const auto* target = std::get_if<UnicastTransmissionTarget>(
        &hops[index].transmission.target);
    if(hops[index].cycle_id != PlanningCycleId{index} ||
       hops[index].packet != packet || target == nullptr ||
       hops[index].transmission.packet_id != packet.packet_id ||
       hops[index].transmission.sender_node_id != senders[index] ||
       target->node_id != targets[index] ||
       hops[index].transmission.started_at !=
           Seconds(static_cast<std::int64_t>(index) * 10) ||
       hops[index].transmission.ended_at !=
           Seconds(static_cast<std::int64_t>(index) * 10 + 1)) {
      return false;
    }
    if(index > 0 &&
       hops[index - 1].transmission.transmission_id >=
           hops[index].transmission.transmission_id) {
      return false;
    }
  }
  if(targets[0] == NodeId{3} || targets[1] == NodeId{3}) return false;

  auto sorted_receptions = receptions;
  std::sort(sorted_receptions.begin(), sorted_receptions.end());
  if(std::adjacent_find(sorted_receptions.begin(),
                        sorted_receptions.end()) !=
     sorted_receptions.end()) {
    return false;
  }
  for(std::size_t index = 0; index < cycles.size(); ++index) {
    if(cycles[index].cycle_id != PlanningCycleId{index} ||
       cycles[index].base_version != SnapshotVersion{index} ||
       cycles[index].starts_at !=
           Seconds(static_cast<std::int64_t>(index) * 10) ||
       cycles[index].closes_at !=
           Seconds(static_cast<std::int64_t>(index + 1) * 10) ||
       cycles[index].tx_opportunity_count != 1 ||
       cycles[index].selector_count != 1 ||
       cycles[index].executed_count != 1 ||
       cycles[index].no_packet_count != 0 ||
       cycles[index].encode_count != 1 ||
       cycles[index].channel_query_count != 3 ||
       cycles[index].reception_count != 3 ||
       cycles[index].records_at_publish != 1 ||
       cycles[index].records_visible_at_finalize != 3 ||
       cycles[index].records_at_close != 1 ||
       cycles[index].records_after_close != 0) {
      return false;
    }
  }

  const auto& delivery = harness->deliveries().deliveries().front();
  return delivery.receiver_node_id == NodeId{3} &&
         delivery.packet == packet;
}

auto TestTargetDecodeLoss() -> bool {
  auto harness = MakeHarness();
  const DigitalPacket packet{PacketId{10},
                             NodeId{0},
                             UnicastDestination{NodeId{3}},
                             {std::byte{0x10},
                              std::byte{0x20},
                              std::byte{0x30}}};
  if(!harness || !harness->EnqueueInitialPacket(packet) ||
     !harness->RunCycle(
         PlanningCycleId{0}, NodeId{0}, std::nullopt, true) ||
     !harness->ExpectQueues(NodeId{1}) ||
     !harness->RunCycle(
         PlanningCycleId{1}, NodeId{1}, NodeId{2}, true) ||
     !harness->ExpectQueues(std::nullopt) ||
     !harness->RunCycle(
         PlanningCycleId{2}, NodeId{2}, std::nullopt, false)) {
    return false;
  }
  const auto& cycles = harness->cycle_audit();
  return harness->hop_audit().size() == 2 &&
         harness->reception_ids().size() == 6 && cycles.size() == 3 &&
         cycles[2].no_packet_count == 1 &&
         cycles[2].executed_count == 0 &&
         harness->deliveries().size() == 0 &&
         harness->world().current_snapshot().version() == SnapshotVersion{3} &&
         harness->world().current_snapshot().committed_at() == Seconds(30);
}

}  // namespace

auto main() -> int {
  return TestHappyPath() && TestTargetDecodeLoss()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
