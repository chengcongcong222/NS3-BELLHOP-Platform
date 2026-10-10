#include "algorithm.hpp"
#include <iostream>
using namespace ns3_factory::contracts;
using namespace ns3_factory::planning;
#define CHECK(x) do { if(!(x)) { std::cerr << __LINE__ << '\n'; return 1; } } while(false)
int main() {
  auto world=WorldSnapshot::Create(SnapshotVersion{0},SimTime::Zero(),{
      {NodeId{0},{true,false,DuplexMode::kHalfDuplex},{{0,0,-10},{0,0,0}}},
      {NodeId{10},{false,true,DuplexMode::kHalfDuplex},{{10,0,-10},{0,0,0}}},
      {NodeId{99},{false,true,DuplexMode::kHalfDuplex},{{99,0,-10},{0,0,0}}}});
  std::vector<NodeId> ids{NodeId{0},NodeId{10},NodeId{99}};
  auto graph=ConnectivityGraph::Create({{NodeId{0},NodeId{10}},{NodeId{0},NodeId{99}}},ids);
  auto roles=RoleTable::Create({},ids);
  auto topology=LogicalTopology::Create({{NodeId{0},NodeId{10}},{NodeId{0},NodeId{99}}},ids,*graph);
  auto structure=StructureSnapshot::Create(PlanningCycleId{0},SnapshotVersion{0},*roles,*graph,*topology);
  auto routing=RoutingPlan::Create({{NodeId{0},NodeId{10},NodeId{10}},{NodeId{0},NodeId{99},NodeId{99}}},*structure);
  const auto one=SimDuration::FromNanoseconds(1'000'000'000);
  auto physical=PlanningInput::Create(PlanningCycleId{0},SnapshotVersion{0},SimTime::Zero(),{
      {NodeId{0},0,2,one},{NodeId{10},0,0,SimDuration::Zero()},{NodeId{99},0,0,SimDuration::Zero()}},one);
  CHECK(world && graph && roles && topology && structure && routing && physical);
  PlanningContextData data{"ordering-test",*world,*structure,*routing,*physical,
      {PlanningCycleId{0},SnapshotVersion{0},SimTime::Zero()},SimDuration::FromNanoseconds(10'000'000'000),
      {{{NodeId{0},FlowId{11}},NodeId{0},NodeId{10},NodeId{10},0,1,one},
       {{NodeId{0},FlowId{22}},NodeId{0},NodeId{99},NodeId{99},0,1,one}},
      {{NodeId{0},NodeId{10},{0,0,-10},{10,0,-10},SimTime::Zero(),25000,4000,"field","revision-1","hash","test-v1",
        LinkPredictionStatus::kAvailable,20.0,one,{},{}},
       {NodeId{0},NodeId{99},{0,0,-10},{99,0,-10},SimTime::Zero(),25000,4000,"field","revision-1","hash","test-v1",
        LinkPredictionStatus::kAvailable,80.0,SimDuration::FromNanoseconds(100'000'000),{},{}}},
      {},{{"delay_weight","0"},{"guard_margin_ns","0"}},42,{},
      {"user.closeout_probe","1.0.0","mac"}};
  auto first=PlanningContext::Create(data); CHECK(first);
  auto by_loss=user_closeout_probe::Plan(*first,data.effective_parameters); CHECK(by_loss);
  CHECK(by_loss->proposals().size()==2 && by_loss->proposals()[0].demand_key.flow_id==FlowId{11});
  data.effective_parameters["delay_weight"]="1";
  auto second=PlanningContext::Create(data); CHECK(second);
  auto by_delay=user_closeout_probe::Plan(*second,data.effective_parameters); CHECK(by_delay);
  CHECK(by_delay->proposals().size()==2 && by_delay->proposals()[0].demand_key.flow_id==FlowId{22});
  auto customized_default=data.effective_parameters;
  customized_default["delay_weight"]="0.75";
  auto default_plan=user_closeout_probe::Plan(*second,customized_default); CHECK(default_plan);
  CHECK(default_plan->proposals()[0].demand_key.flow_id==FlowId{22});
  data.run_id="another-run";
  std::reverse(data.links.begin(),data.links.end()); std::reverse(data.demands.begin(),data.demands.end());
  auto reordered=PlanningContext::Create(data); CHECK(reordered);
  auto again=user_closeout_probe::Plan(*reordered,data.effective_parameters); CHECK(again);
  CHECK(again->proposals()[0].demand_key==by_delay->proposals()[0].demand_key);
  CHECK(again->proposals()[1].start_offset==by_delay->proposals()[1].start_offset);
  CHECK(!by_delay->Validate(*reordered)); // audit hash still binds exact RunId
  CHECK(!user_closeout_probe::Plan(*first,{{"delay_weight","nan"},{"guard_margin_ns","0"}}));
  CHECK(!user_closeout_probe::Plan(*first,{{"delay_weight","0"},{"guard_margin_ns","0"},{"unknown","1"}}));
  std::cout << "CHANNEL_WEIGHT_CHANGES_FLOW_ORDER_OK\n";
}
