#include <iostream>
#include <ns3_factory/planning/sdk.hpp>
#include "scenario_runtime_test_support.hpp"
using namespace ns3_factory;
using namespace assembly::test;
using namespace assembly::internal;

// Integration fixture uses the real selected kernel with deterministic PHY
// doubles. Full authored Channel1/2 gates are separate, not claimed by this test.
class FlowPlanner final : public IProtocolCyclePlanner {
 public:
  bool uses_flow_planning() const noexcept override { return true; }
  auto Build(const WorldSnapshot&,const StructureSnapshot&) const -> Result<ProtocolCyclePlan> override {
    return std::unexpected(Error{ErrorCode::kUnsupported,"legacy call is forbidden"});
  }
  auto BuildFlow(const WorldSnapshot& world,const StructureSnapshot& structure,
      const PlanningInput& input,const FlowPlanningState& state) const -> Result<ProtocolCyclePlan> override {
    const auto routing = ShortestPathToSinkRoutingPlanner{}.Build(structure);
    if(!routing) return std::unexpected(routing.error());
    auto demands = state.demands;
    for(auto& demand : demands) demand.next_hop = routing->FindNextHop(
        demand.key.queue_owner_node_id,demand.destination_node_id);
    std::vector<LinkPrediction> links;
    for(const auto& tx : world.nodes()) for(const auto& rx : world.nodes()) if(tx.node_id != rx.node_id)
      links.push_back({tx.node_id,rx.node_id,tx.motion.position,rx.motion.position,world.committed_at(),
          25000,4000,"test-double","1","test-only","fixture-v1",LinkPredictionStatus::kAvailable,
          70.0,DurationSeconds(1),std::nullopt,std::nullopt});
    auto context = PlanningContext::Create({"flow-runtime-test",world,structure,*routing,input,
        state.structure_origin,DurationSeconds(6),demands,links,state.previous,{},42,{},
        {"fixture","1.0.0","mac"}});
    if(!context) return std::unexpected(context.error());
    observed.push_back(*context);
    std::vector<planning::ProposedFlowGrant> grants;
    // Deliberately pick the second queued flow first, never the queue head.
    auto offset = SimDuration::Zero();
    for(auto it = demands.rbegin();it != demands.rend();++it) {
      grants.push_back({it->key,offset,SimDuration::Zero()});
      offset = *CheckedAdd(offset,DurationSeconds(2));
    }
    auto plan = planning::FlowTransmissionPlan::Create(*context,grants);
    if(!plan) return std::unexpected(plan.error());
    if(AppliedMacSchedule::Capture(plan->protocol_plan())) return std::unexpected(Error{
        ErrorCode::kInternal,"legacy template accepted a flow plan"});
    return plan->protocol_plan();
  }
  mutable std::vector<PlanningContext> observed;
};

class BrokenTrace final : public ITraceSink {
 public:
  auto Emit(const TraceEvent&) noexcept -> Status override {
    ++calls;
    return std::unexpected(Error{ErrorCode::kUnavailable,"injected trace outage"});
  }
  std::size_t calls{};
};

auto Execute(ITraceSink* trace) -> std::vector<PreviousCycleFeedback> {
  auto fixture = RuntimeFixture::Create(PlanningCycleId{0},{NodeId{0}});
  if(!fixture) return {};
  FlowPlanner planner;
  ScenarioRuntime runtime{fixture->gateway,fixture->world,fixture->queues,fixture->deliveries,
      fixture->ids,fixture->structure_builder,planner,fixture->tx_phy,fixture->channel,
      fixture->noise,fixture->rx_phy,PlanningCycleId{0},trace,3,nullptr,MacScheduleUpdatePolicy::kEveryCycle};
  auto trajectories = trajectory::BindGenerated(fixture->world.current_snapshot(),Seconds(60));
  if(!trajectories || !runtime.BindTrajectories(*trajectories) ||
     !runtime.BindPlanningInputs({{NodeId{0},DurationSeconds(1)},{NodeId{1},DurationSeconds(1)},
       {NodeId{2},DurationSeconds(1)},{NodeId{3},DurationSeconds(1)}},DurationSeconds(1))) return {};
  auto first=TestPacket(); first.flow_id=FlowId{11};
  auto second=first; second.flow_id=FlowId{22}; second.packet_id=PacketId{20};
  if(!fixture->Enqueue(first) || !fixture->Enqueue(second)) return {};
  const auto run=runtime.RunCycles(3);
  if(!run) { std::cerr << run.error().message << '\n'; return {}; }
  if(planner.observed.size()!=3 || fixture->tx_phy.audit.size()!=6 ||
     fixture->tx_phy.audit[0].packet.flow_id != FlowId{22} || runtime.network_refresh_count()!=1) return {};
  std::vector<PreviousCycleFeedback> feedback;
  for(std::size_t cycle=0;cycle<3;++cycle) {
    const auto& data=planner.observed[cycle].data();
    if(data.structure_origin.cycle_id != PlanningCycleId{0} ||
       data.structure_origin.snapshot_version != SnapshotVersion{0} ||
       data.demands.size()!=2 || data.demands[0].key.queue_owner_node_id != NodeId{cycle} ||
       data.demands[0].source_node_id != NodeId{0} || data.demands[0].destination_node_id != NodeId{3} ||
       data.demands[0].ready_packets!=0 || data.demands[0].queued_packets!=1) return {};
    feedback.push_back(data.previous);
    if(cycle==0) { if(data.previous.cycle_id) return {}; continue; }
    std::uint64_t attempts=0,relays=0,overheard=0;
    for(const auto& row:data.previous.links) {
      if(row.intended_target) { attempts+=row.attempts; relays+=row.relays; }
      else overheard+=row.decoded;
      if(row.compared_bits || row.error_bits || row.crc_passed || row.crc_failed) return {};
    }
    if(attempts!=2 || relays!=2 || overheard!=4) return {};
  }
  for(auto node:NodeIds()) if(*fixture->queues.size(node)) return {};
  return feedback;
}

int main() {
  auto baseline=Execute(nullptr);
  BrokenTrace trace;
  auto outage=Execute(&trace);
  if(baseline.size()!=3 || outage.size()!=3 || !trace.calls) return 1;
  for(std::size_t i=0;i<3;++i) if(baseline[i].links!=outage[i].links ||
      baseline[i].cycle_id!=outage[i].cycle_id || baseline[i].ended_at!=outage[i].ended_at) return 2;
  std::cout << "FLOW_RELAY_AND_TRACE_INDEPENDENT_FEEDBACK_OK\n";
}
