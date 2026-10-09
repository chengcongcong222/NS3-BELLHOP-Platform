#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

#include "internal/adaptive_tdma_mac_planner.hpp"
#include "internal/composite_protocol_cycle_planner.hpp"
#include "internal/direct_routing_planner.hpp"
#include "internal/shortest_path_routing_planner.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::planning::internal;

#define CHECK(condition) do { if(!(condition)) { std::cerr << "check failed at " << __LINE__ << '\n'; return false; } } while(false)

namespace {
constexpr auto At(std::int64_t value) -> SimTime { return SimTime::FromNanoseconds(value); }
constexpr auto For(std::int64_t value) -> SimDuration { return SimDuration::FromNanoseconds(value); }

auto World(SimTime start = At(0)) -> Result<WorldSnapshot> {
  std::vector<NodeCommittedState> nodes;
  for(std::uint64_t id = 0; id < 5; ++id) {
    nodes.push_back({NodeId{id}, {true, true, DuplexMode::kHalfDuplex},
                     {{static_cast<double>(id), 0, 0}, {0, 0, 0}}});
  }
  return WorldSnapshot::Create(SnapshotVersion{4}, start, std::move(nodes));
}

auto Structure(PlanningCycleId cycle = PlanningCycleId{0},
               std::vector<RoleBinding> bindings = {}) -> Result<StructureSnapshot> {
  const std::vector<NodeId> nodes{NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}, NodeId{4}};
  auto graph = ConnectivityGraph::Create({{NodeId{0}, NodeId{2}}, {NodeId{0}, NodeId{1}},
      {NodeId{1}, NodeId{3}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{0}}}, nodes);
  if(!graph) return std::unexpected(graph.error());
  auto roles = RoleTable::Create(std::move(bindings), nodes);
  if(!roles) return std::unexpected(roles.error());
  auto topology = LogicalTopology::Create({{NodeId{0}, NodeId{2}}, {NodeId{0}, NodeId{1}},
      {NodeId{1}, NodeId{3}}, {NodeId{2}, NodeId{3}}, {NodeId{3}, NodeId{0}}}, nodes, *graph);
  if(!topology) return std::unexpected(topology.error());
  return StructureSnapshot::Create(cycle, SnapshotVersion{4}, *roles, *graph, *topology);
}

auto Input(std::uint64_t first, std::uint64_t second,
           PlanningCycleId cycle = PlanningCycleId{0}, SimTime start = At(0)) -> Result<PlanningInput> {
  return PlanningInput::Create(cycle, SnapshotVersion{4}, start,
      {{NodeId{4}, 0, 0, For(3)}, {NodeId{1}, 0, second, For(3)},
       {NodeId{2}, 0, 0, For(3)}, {NodeId{0}, first, 0, For(3)},
       {NodeId{3}, 0, 0, For(3)}}, For(2));
}

auto DemandAllocationAndFairness() -> bool {
  const auto world = World();
  const auto structure = Structure();
  const auto input = Input(4, 1);
  const auto policy = AdaptiveTdmaPolicy::Create(For(18), For(1));
  CHECK(world && structure && input && policy);
  CHECK(input->knowledge_scope() == PlanningKnowledgeScope::kGlobalSimulationKnowledge);
  const AdaptiveTdmaMacPlanner mac{*policy};
  const DirectRoutingPlanner routing;
  const CompositeProtocolCyclePlanner planner{routing, mac};
  const auto plan = planner.Build(*world, *structure, *input);
  CHECK(plan);
  const auto slots = plan->mac_plan().tx_opportunities();
  CHECK(slots.size() == 3);
  CHECK((slots[0] == TxOpportunity{NodeId{0}, At(0)}));
  CHECK((slots[1] == TxOpportunity{NodeId{1}, At(6)}));
  CHECK((slots[2] == TxOpportunity{NodeId{0}, At(12)}));
  CHECK(plan->timing().closes_at() == At(18));
  CHECK(ValidatePlanningPlan(*plan, *input, *world, *structure));
  CHECK(!mac.Build(*world, *structure));
  const auto next_structure = Structure(PlanningCycleId{1});
  const auto next_input = Input(4, 1, PlanningCycleId{1});
  const auto next = mac.Build(*world, *next_structure, *next_input);
  CHECK(next && next->mac_plan().tx_opportunities().front().sender_node_id == NodeId{1});
  const auto swapped = Input(1, 4);
  const auto swapped_plan = mac.Build(*world, *structure, *swapped);
  CHECK(swapped_plan && swapped_plan->mac_plan().tx_opportunities()[2].sender_node_id == NodeId{1});
  const auto empty = Input(0, 0);
  const auto idle = mac.Build(*world, *structure, *empty);
  CHECK(idle && idle->mac_plan().tx_opportunities().empty() && idle->timing().closes_at() == At(18));
  const auto short_policy = AdaptiveTdmaPolicy::Create(For(5), For(1));
  const auto deferred = AdaptiveTdmaMacPlanner{*short_policy}.Build(*world, *structure, *input);
  CHECK(deferred && deferred->mac_plan().tx_opportunities().empty());
  const auto varied_input = PlanningInput::Create(PlanningCycleId{0}, SnapshotVersion{4}, At(0),
      {{NodeId{0}, 1, 0, For(1)}, {NodeId{1}, 0, 1, For(4)},
       {NodeId{2}, 0, 0, For(3)}, {NodeId{3}, 0, 0, For(3)}, {NodeId{4}, 0, 0, For(3)}}, For(2));
  const auto varied_policy = AdaptiveTdmaPolicy::Create(For(11), For(1));
  const auto varied = AdaptiveTdmaMacPlanner{*varied_policy}.Build(*world, *structure, *varied_input);
  CHECK(varied && varied->mac_plan().tx_opportunities().size() == 2);
  CHECK((varied->mac_plan().tx_opportunities()[1] == TxOpportunity{NodeId{1}, At(4)}));
  CHECK(varied->timing().closes_at() == At(11));
  const auto changing_structure = Structure(PlanningCycleId{4});
  const auto changing_input = PlanningInput::Create(PlanningCycleId{4}, SnapshotVersion{4}, At(0),
      {{NodeId{0}, 5, 0, For(3)}, {NodeId{1}, 0, 0, For(3)},
       {NodeId{2}, 0, 0, For(3)}, {NodeId{3}, 1, 0, For(3)}, {NodeId{4}, 1, 0, For(3)}}, For(2));
  const auto one_slot_policy = AdaptiveTdmaPolicy::Create(For(6), For(1));
  const auto rotating = AdaptiveTdmaMacPlanner{*one_slot_policy}.Build(
      *world, *changing_structure, *changing_input);
  CHECK(rotating && rotating->mac_plan().tx_opportunities().size() == 1);
  CHECK(rotating->mac_plan().tx_opportunities().front().sender_node_id == NodeId{4});
  return true;
}

auto ValidatorsAndOverflow() -> bool {
  const auto world = World(); const auto structure = Structure(); const auto input = Input(1, 1);
  CHECK(world && structure && input);
  CHECK(!AdaptiveTdmaPolicy::Create(For(0), For(0)));
  CHECK(!AdaptiveTdmaPolicy::Create(For(10), For(-1)));
  CHECK(!PlanningInput::Create(PlanningCycleId{0}, SnapshotVersion{4}, At(0),
      {{NodeId{0}, 1, 0, For(1)}, {NodeId{0}, 0, 0, For(1)}}, For(0)));
  CHECK(!PlanningInput::Create(PlanningCycleId{0}, SnapshotVersion{4}, At(0),
      {{NodeId{0}, std::numeric_limits<std::uint64_t>::max(), 1, For(1)}}, For(0)));
  const auto incomplete = PlanningInput::Create(PlanningCycleId{0}, SnapshotVersion{4}, At(0), {}, For(0));
  CHECK(incomplete && !ValidatePlanningInput(*incomplete, *world, *structure));
  const auto stale = Input(1, 1, PlanningCycleId{1});
  CHECK(!ValidatePlanningInput(*stale, *world, *structure));
  const auto timing = CycleTiming::Create(PlanningCycleId{0}, SnapshotVersion{4}, At(0), At(9));
  const auto routes = DirectRoutingPlanner{}.Build(*structure);
  const auto overlapping_mac = MacPlan::Create(*timing,
      {{NodeId{0}, At(0)}, {NodeId{1}, At(4)}});
  const auto overlaps = ProtocolCyclePlan::Create(*routes, *timing, *overlapping_mac);
  CHECK(overlaps && !ValidatePlanningPlan(*overlaps, *input, *world, *structure));
  const auto crossing_mac = MacPlan::Create(*timing, {{NodeId{0}, At(5)}});
  const auto crosses = ProtocolCyclePlan::Create(*routes, *timing, *crossing_mac);
  CHECK(crosses && !ValidatePlanningPlan(*crosses, *input, *world, *structure));
  const auto unbound = ProtocolCyclePlan::Create(*timing, {{NodeId{0}, At(0)}});
  CHECK(unbound && !ValidatePlanningPlan(*unbound, *input, *world, *structure));
  for(const auto destination : {NodeId{3}, NodeId{9}}) {
    std::vector<NodeId> other_nodes{NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}, NodeId{4}};
    if(destination == NodeId{9}) other_nodes.push_back(destination);
    const auto other_graph = ConnectivityGraph::Create({{NodeId{0}, destination}}, other_nodes);
    const auto other_roles = RoleTable::Create({}, other_nodes);
    const auto other_topology = LogicalTopology::Create({{NodeId{0}, destination}}, other_nodes, *other_graph);
    const auto other_structure = StructureSnapshot::Create(PlanningCycleId{0}, SnapshotVersion{4},
        *other_roles, *other_graph, *other_topology);
    const auto foreign_routes = DirectRoutingPlanner{}.Build(*other_structure);
    const auto safe_mac = MacPlan::Create(*timing, {{NodeId{0}, At(0)}});
    const auto foreign_plan = ProtocolCyclePlan::Create(*foreign_routes, *timing, *safe_mac);
    CHECK(foreign_plan && !ValidatePlanningPlan(*foreign_plan, *input, *world, *structure));
  }
  const auto extreme_world = World(At(std::numeric_limits<std::int64_t>::max() - 2));
  const auto extreme_input = Input(1, 0, PlanningCycleId{0}, extreme_world->committed_at());
  const auto policy = AdaptiveTdmaPolicy::Create(For(18), For(1));
  const auto overflow = AdaptiveTdmaMacPlanner{*policy}.Build(*extreme_world, *structure, *extreme_input);
  CHECK(!overflow && overflow.error().code == ErrorCode::kOverflow);
  return true;
}

auto AllDestinationRouting() -> bool {
  const auto structure = Structure();
  const auto multi_sink = Structure(PlanningCycleId{0},
      {{NodeId{0}, ProtocolRole::kSink}, {NodeId{3}, ProtocolRole::kSink}});
  CHECK(structure && multi_sink);
  const auto direct = DirectRoutingPlanner{}.Build(*structure);
  const auto shortest = ShortestPathRoutingPlanner{}.Build(*structure);
  const auto roles_ignored = ShortestPathRoutingPlanner{}.Build(*multi_sink);
  CHECK(direct && shortest && roles_ignored);
  CHECK(direct->FindNextHop(NodeId{0}, NodeId{1}) == NodeId{1});
  CHECK(!direct->FindNextHop(NodeId{0}, NodeId{3}));
  CHECK(shortest->FindNextHop(NodeId{0}, NodeId{3}) == NodeId{1});
  CHECK(shortest->FindNextHop(NodeId{1}, NodeId{2}) == NodeId{3});
  CHECK(!shortest->FindNextHop(NodeId{4}, NodeId{0}));
  CHECK(!shortest->FindNextHop(NodeId{0}, NodeId{4}));
  CHECK(!shortest->FindNextHop(NodeId{0}, NodeId{0}));
  CHECK(std::equal(shortest->entries().begin(), shortest->entries().end(),
                   roles_ignored->entries().begin(), roles_ignored->entries().end()));
  return true;
}
}  // namespace

int main() {
  return DemandAllocationAndFairness() && ValidatorsAndOverflow() && AllDestinationRouting()
      ? EXIT_SUCCESS : EXIT_FAILURE;
}
