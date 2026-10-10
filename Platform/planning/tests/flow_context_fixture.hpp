#pragma once
#include <ns3_factory/planning/sdk.hpp>
namespace ns3_factory::planning::test {
inline auto Fixture() -> contracts::PlanningContextData {
  using namespace contracts;
  const auto at=SimTime::Zero(); const auto ns=SimDuration::FromNanoseconds(10);
  std::vector<NodeId> ids{NodeId{0},NodeId{10},NodeId{20},NodeId{99}};
  std::vector<NodeCommittedState> nodes;std::vector<DirectedLink> edges;
  std::vector<LogicalLink> logical;std::vector<RouteEntry> routes;
  std::vector<NodeTrafficDemand> physical;std::vector<LinkPrediction> links;
  for(auto id:ids) {
    nodes.push_back({id,{true,true,DuplexMode::kHalfDuplex},{{static_cast<double>(id.value()),0,-10},{0,0,0}}});
    physical.push_back({id,0,id==NodeId{0} ? 2U:0U,ns});
    for(auto other:ids) if(id!=other) {
      edges.push_back({id,other});logical.push_back({id,other});routes.push_back({id,other,other});
      links.push_back({id,other,{static_cast<double>(id.value()),0,-10},{static_cast<double>(other.value()),0,-10},
          at,25000,1000,"fixture-environment","1","fixture-checksum","shared-sample-v1",
          LinkPredictionStatus::kAvailable,40.0,ns,std::nullopt,std::nullopt});
    }
  }
  const auto world=WorldSnapshot::Create(SnapshotVersion{0},at,nodes);
  const auto graph=ConnectivityGraph::Create(edges,ids);const auto roles=RoleTable::Create({},ids);
  const auto topology=LogicalTopology::Create(logical,ids,*graph);
  const auto structure=StructureSnapshot::Create(PlanningCycleId{0},SnapshotVersion{0},*roles,*graph,*topology);
  const auto routing=RoutingPlan::Create(routes,*structure);
  const auto input=PlanningInput::Create(PlanningCycleId{0},SnapshotVersion{0},at,physical,ns);
  return {"fixture-run",*world,*structure,*routing,*input,{PlanningCycleId{0},SnapshotVersion{0},at},
      SimDuration::FromNanoseconds(100),
      {{{NodeId{0},FlowId{11}},NodeId{0},NodeId{10},NodeId{10},0,1,ns},
       {{NodeId{0},FlowId{22}},NodeId{0},NodeId{20},NodeId{20},0,1,ns}},links,{},{{"fixture","value"}},42,{},
      {"fixture","1.0.0","mac"}};
}
}  // namespace ns3_factory::planning::test
