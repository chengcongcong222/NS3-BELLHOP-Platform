#include <algorithm>
#include "algorithm.hpp"
using namespace ns3_factory::contracts;
int main() {
  const auto t = SimTime::FromNanoseconds(0);
  const auto d = SimDuration::FromNanoseconds(10);
  std::vector<NodeCommittedState> nodes;
  for(auto id : {99, 0, 20, 10}) nodes.push_back({NodeId{static_cast<std::uint64_t>(id)},
    {true, true, DuplexMode::kHalfDuplex}, {{0,0,-10}, {0,0,0}}});
  const auto world = WorldSnapshot::Create(SnapshotVersion{1}, t, nodes);
  const std::vector<NodeId> ids{NodeId{0}, NodeId{10}, NodeId{20}, NodeId{99}};
  const auto graph = ConnectivityGraph::Create({}, ids);
  const auto roles = RoleTable::Create({}, ids);
  const auto topology = LogicalTopology::Create({}, ids, *graph);
  const auto structure = StructureSnapshot::Create(PlanningCycleId{0}, SnapshotVersion{1}, *roles, *graph, *topology);
  const auto input = PlanningInput::Create(PlanningCycleId{0}, SnapshotVersion{1}, t,
    {{NodeId{0},1,0,d}, {NodeId{10},1,0,d}, {NodeId{20},0,0,d}, {NodeId{99},0,0,d}}, d);
  if(!world || !structure || !input) return 1;
  const ns3_factory::planning::Parameters parameters{{"gap_ns","0"},{"order","升序"}};
  const auto first = user_integration_multifile::Build(*world, *structure, *input, SimDuration::FromNanoseconds(100), parameters);
  const auto second = user_integration_multifile::Build(*world, *structure, *input, SimDuration::FromNanoseconds(100), parameters);
  if(!first || !second || *first != *second || first->size() != 2 || first->front().sender_node_id != NodeId{0}) return 2;
  const auto reversed = user_integration_multifile::Build(*world, *structure, *input, SimDuration::FromNanoseconds(100), {{"gap_ns","0"},{"order","降序"}});
  return !reversed || reversed->front().sender_node_id != NodeId{10} ? 3 : 0;
}
