#include "algorithm.hpp"
#include <iostream>
using namespace ns3_factory::contracts;
using namespace ns3_factory::planning;
#define CHECK(x) do { if(!(x)) { std::cerr << __LINE__ << '\n'; return 1; } } while(false)
int main() {
  const auto step=SimDuration::FromNanoseconds(10);
  auto world=WorldSnapshot::Create(SnapshotVersion{0},SimTime::Zero(),{
    {NodeId{0},{true,false,DuplexMode::kHalfDuplex},{{0,0,-10},{0,0,0}}},
    {NodeId{99},{false,true,DuplexMode::kHalfDuplex},{{99,0,-10},{0,0,0}}}});
  std::vector<NodeId> ids{NodeId{0},NodeId{99}};
  auto graph=ConnectivityGraph::Create({{NodeId{0},NodeId{99}}},ids);
  auto roles=RoleTable::Create({},ids);
  auto topology=LogicalTopology::Create({{NodeId{0},NodeId{99}}},ids,*graph);
  auto structure=StructureSnapshot::Create(PlanningCycleId{0},SnapshotVersion{0},*roles,*graph,*topology);
  auto routing=RoutingPlan::Create({{NodeId{0},NodeId{99},NodeId{99}}},*structure);
  auto physical=PlanningInput::Create(PlanningCycleId{0},SnapshotVersion{0},SimTime::Zero(),{
    {NodeId{0},0,4,step},{NodeId{99},0,0,SimDuration::Zero()}},step);
  CHECK(world && graph && roles && topology && structure && routing && physical);
  PlanningContextData data{"run-one",*world,*structure,*routing,*physical,
    {PlanningCycleId{0},SnapshotVersion{0},SimTime::Zero()},SimDuration::FromNanoseconds(100),
    {{{NodeId{0},FlowId{11}},NodeId{0},NodeId{99},NodeId{99},0,2,step},
     {{NodeId{0},FlowId{22}},NodeId{0},NodeId{99},NodeId{99},0,2,step}},
    {{NodeId{0},NodeId{99},{0,0,-10},{99,0,-10},SimTime::Zero(),25000,4000,"field","v1","hash","test",
      LinkPredictionStatus::kAvailable,30.0,step,{},{}}},
    {},{{"packets_per_turn","1"}},42,{}, {"example.flow-round-robin","1.0.0","mac"}};
  auto context=PlanningContext::Create(data);CHECK(context);
  auto plan=example_flow_round_robin::Plan(*context,data.effective_parameters);CHECK(plan);
  CHECK(plan->proposals().size()==4);
  CHECK(plan->proposals()[1].demand_key.flow_id==FlowId{22});
  data.effective_parameters["packets_per_turn"]="2";
  auto burst=PlanningContext::Create(data);CHECK(burst);
  auto second=example_flow_round_robin::Plan(*burst,data.effective_parameters);CHECK(second);
  CHECK(second->proposals()[1].demand_key.flow_id==FlowId{11});
  data.run_id="run-two";std::reverse(data.demands.begin(),data.demands.end());
  auto reordered=PlanningContext::Create(data);CHECK(reordered);
  auto third=example_flow_round_robin::Plan(*reordered,data.effective_parameters);CHECK(third);
  CHECK(third->proposals()[1].demand_key==second->proposals()[1].demand_key);
  CHECK(!example_flow_round_robin::Plan(*context,{{"packets_per_turn","0"}}));
  std::cout << "ROUND_ROBIN_QUOTA_AND_MULTIFILE_OK\n";
}
