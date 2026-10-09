#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

#include "scenario_runtime_test_support.hpp"
#include "internal/adaptive_tdma_mac_planner.hpp"
#include "internal/communication_traffic_application.hpp"
#include "internal/shortest_path_routing_planner.hpp"

using namespace ns3_factory::assembly::internal;
using namespace ns3_factory::assembly::test;

#define CHECK(condition) do { if(!(condition)) { std::cerr << "check failed at " << __LINE__ << '\n'; return false; } } while(false)

namespace {
struct InputRecord {
  PlanningCycleId cycle;
  NodeId node;
  SimTime time;
  std::uint64_t count;
  auto operator==(const InputRecord&) const -> bool = default;
};

class RecordingApplication final : public IScenarioCycleApplication {
 public:
  RecordingApplication(std::vector<CommunicationTrafficFlow> flows, PacketQueueStore& queues)
      : source_(std::move(flows), queues) {}
  auto input_node_ids() const noexcept -> std::span<const NodeId> override {
    return source_.input_node_ids();
  }
  auto input_timing() const noexcept -> ApplicationInputTiming override {
    return source_.input_timing();
  }
  auto forecast_packet_count(PlanningCycleId cycle, NodeId node) const
      -> Result<std::uint64_t> override { return source_.forecast_packet_count(cycle, node); }
  auto OnInputReady(PlanningCycleId cycle, NodeId node, SimTime now,
                    const CycleWorkingState& state) -> Status override {
    const auto count = source_.forecast_packet_count(cycle, node);
    if(!count) return std::unexpected(count.error());
    inputs.push_back({cycle, node, now, *count});
    return source_.OnInputReady(cycle, node, now, state);
  }
  auto OnRuntimeDecision(PlanningCycleId cycle, SimTime now,
                         const CycleWorkingState& state) -> Status override {
    return source_.OnRuntimeDecision(cycle, now, state);
  }
  std::vector<InputRecord> inputs;
 private:
  CommunicationTrafficApplication source_;
};

class RecordingPlanner final : public IProtocolCyclePlanner {
 public:
  RecordingPlanner(AdaptiveTdmaPolicy policy, bool idle) : mac_(policy), idle_(idle) {}
  auto Build(const WorldSnapshot&, const StructureSnapshot&) const
      -> Result<ProtocolCyclePlan> override {
    return std::unexpected(Error{ErrorCode::kFailedPrecondition, "Test requires planning input"});
  }
  auto Build(const WorldSnapshot& world, const StructureSnapshot& structure,
             const PlanningInput& input) const -> Result<ProtocolCyclePlan> override {
    demands.emplace_back(input.demands().begin(), input.demands().end());
    auto result = CompositeProtocolCyclePlanner{routing_, mac_}.Build(world, structure, input);
    if(!result || !idle_) return result;
    auto empty = MacPlan::Create(result->timing(), {});
    if(!empty) return std::unexpected(empty.error());
    return ProtocolCyclePlan::Create(*result->routing_plan(), result->timing(), *empty);
  }
  mutable std::vector<std::vector<NodeTrafficDemand>> demands;
 private:
  ShortestPathRoutingPlanner routing_;
  AdaptiveTdmaMacPlanner mac_;
  bool idle_;
};

struct Summary {
  std::vector<TxAudit> transmissions;
  std::vector<InputRecord> inputs;
  std::vector<std::vector<NodeTrafficDemand>> demands;
  std::vector<std::size_t> queue_sizes;
  SnapshotVersion version;
  SimTime completed_at;
  std::size_t refresh_count;
  std::size_t applied_count;
};

auto Execute(std::vector<CommunicationTrafficFlow> flows, std::size_t cycles,
             bool idle = false) -> std::optional<Summary> {
  auto snapshot = InitialSnapshot();
  auto queues = PacketQueueStore::Create(NodeIds());
  auto deliveries = ApplicationDeliveryStore::Create(NodeIds());
  auto decision = ConnectivityDecisionPolicy::Create(std::nullopt, 0.5, 0.5);
  auto policy = AdaptiveTdmaPolicy::Create(DurationSeconds(2), DurationSeconds(0));
  if(!snapshot || !queues || !deliveries || !decision || !policy) return std::nullopt;
  Ns3KernelGateway gateway;
  WorldStateStore world{std::move(*snapshot)};
  CommunicationIdAllocator ids{TransmissionId{100}, ReceptionId{1000}};
  ChainFeasibilityEstimator estimator{FeasibilityMode::kStableChain};
  ConfiguredRoleAssignmentPolicy roles{std::vector<RoleBinding>{}};
  AllFeasibleLinksTopologyPolicy topology;
  StructureBuilder structure{*decision, estimator, roles, topology};
  MockTxPhy tx; MockChannel channel; MockNoise noise; MockRxPhy rx;
  RecordingPlanner planner{*policy, idle};
  RecordingApplication application{std::move(flows), *queues};
  ScenarioRuntime runtime{gateway, world, *queues, *deliveries, ids, structure,
      planner, tx, channel, noise, rx, PlanningCycleId{0}, nullptr, 10,
      &application, MacScheduleUpdatePolicy::kEveryCycle};
  std::vector<NodeAirtime> airtimes;
  for(const auto id : queues->node_ids()) airtimes.push_back({id, DurationSeconds(1)});
  if(!runtime.BindPlanningInputs(std::move(airtimes), DurationSeconds(1))) return std::nullopt;
  auto trajectories = ns3_factory::trajectory::BindGenerated(world.current_snapshot(),
      Seconds(static_cast<std::int64_t>(2 * cycles)));
  if(!trajectories || !runtime.BindTrajectories(*trajectories)) return std::nullopt;
  const auto executed = runtime.RunCycles(cycles);
  if(!executed) {
    std::cerr << executed.error().message << '\n';
    return std::nullopt;
  }
  std::vector<std::size_t> sizes;
  for(const auto node : queues->node_ids()) {
    const auto size = queues->size(node);
    if(!size) return std::nullopt;
    sizes.push_back(*size);
  }
  return Summary{std::move(tx.audit), std::move(application.inputs), std::move(planner.demands),
      std::move(sizes), world.current_snapshot().version(), world.current_snapshot().committed_at(),
      runtime.network_refresh_count(), runtime.applied_schedule_update_count()};
}

auto EveryCycleUsesFreshQueueDemand() -> bool {
  const auto run = Execute({{NodeId{0}, NodeId{3}, 1}}, 2);
  CHECK(run && run->transmissions.size() == 2);
  CHECK(run->transmissions[0].request.sender_node_id == NodeId{0});
  CHECK(run->transmissions[1].request.sender_node_id == NodeId{1});
  CHECK(run->transmissions[0].packet.packet_id == run->transmissions[1].packet.packet_id);
  CHECK(run->transmissions[1].request.started_at == Seconds(2));
  CHECK(run->demands[1][1].queued_packet_count == 1);
  CHECK(run->demands[1][0].ready_packet_count == 1);
  CHECK((run->queue_sizes == std::vector<std::size_t>{1, 0, 1, 0}));
  CHECK(run->version == SnapshotVersion{2} && run->completed_at == Seconds(4));
  CHECK(run->refresh_count == 1 && run->applied_count == 2);
  return true;
}

auto InputsDoNotDependOnSlots() -> bool {
  const std::vector<CommunicationTrafficFlow> flows{
      {NodeId{1}, NodeId{3}, 1}, {NodeId{0}, NodeId{3}, 2}, {NodeId{0}, NodeId{2}, 1}};
  const auto scheduled = Execute(flows, 3);
  const auto idle = Execute(flows, 3, true);
  CHECK(scheduled && idle);
  CHECK(scheduled->inputs == idle->inputs && idle->inputs.size() == 6);
  CHECK(idle->inputs[0].time == Seconds(0) && idle->inputs[2].time == Seconds(2) &&
        idle->inputs[4].time == Seconds(4));
  CHECK(idle->inputs[0].node == NodeId{0} && idle->inputs[0].count == 3);
  CHECK(idle->transmissions.empty());
  CHECK((idle->queue_sizes == std::vector<std::size_t>{9, 3, 0, 0}));
  CHECK(idle->demands[2][0].queued_packet_count == 6);
  CHECK(idle->version == SnapshotVersion{3} && idle->completed_at == Seconds(6));
  return true;
}

auto FlowValidationAndLegacyTiming() -> bool {
  auto queues = PacketQueueStore::Create(NodeIds());
  CHECK(queues);
  CommunicationTrafficApplication legacy{{NodeId{0}}, NodeId{3}, *queues};
  CHECK(legacy.input_timing() == ApplicationInputTiming::kLegacyFirstTxOpportunity);
  CommunicationTrafficApplication duplicate{
      std::vector<CommunicationTrafficFlow>{{NodeId{0}, NodeId{1}, 1}, {NodeId{0}, NodeId{1}, 2}}, *queues};
  CHECK(!duplicate.forecast_packet_count(PlanningCycleId{0}, NodeId{0}));
  CommunicationTrafficApplication outside{
      std::vector<CommunicationTrafficFlow>{{NodeId{0}, NodeId{9}, 1}}, *queues};
  CHECK(!outside.forecast_packet_count(PlanningCycleId{0}, NodeId{0}));
  return true;
}
}  // namespace

int main() {
  return EveryCycleUsesFreshQueueDemand() && InputsDoNotDependOnSlots() && FlowValidationAndLegacyTiming()
      ? EXIT_SUCCESS : EXIT_FAILURE;
}
