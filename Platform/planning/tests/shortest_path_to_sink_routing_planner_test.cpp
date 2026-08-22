#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/node_capability.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/routing.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/topology.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

#include "internal/composite_protocol_cycle_planner.hpp"
#include "internal/configured_tdma_mac_planner.hpp"
#include "internal/configured_tdma_policy.hpp"
#include "internal/routing_planner.hpp"
#include "internal/shortest_path_to_sink_routing_planner.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::planning::internal;

static_assert(std::derived_from<ShortestPathToSinkRoutingPlanner,
                                IRoutingPlanner>);
static_assert(std::has_virtual_destructor_v<IRoutingPlanner>);

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

constexpr auto For(std::int64_t nanoseconds) -> SimDuration {
  return SimDuration::FromNanoseconds(nanoseconds);
}

constexpr auto Node(std::uint64_t id) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, 0.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto MakeStructure(
    std::vector<NodeId> node_ids,
    std::vector<RoleBinding> roles,
    std::vector<LogicalLink> logical_links,
    std::vector<DirectedLink> additional_connectivity = {})
    -> Result<StructureSnapshot> {
  std::vector<DirectedLink> connectivity_links;
  connectivity_links.reserve(logical_links.size() +
                             additional_connectivity.size());
  for(const auto& link : logical_links) {
    connectivity_links.push_back(
        DirectedLink{link.source_node_id, link.target_node_id});
  }
  connectivity_links.insert(connectivity_links.end(),
                            additional_connectivity.begin(),
                            additional_connectivity.end());

  auto connectivity = ConnectivityGraph::Create(
      std::move(connectivity_links), node_ids);
  if(!connectivity) {
    return std::unexpected(connectivity.error());
  }
  auto topology = LogicalTopology::Create(
      std::move(logical_links), node_ids, *connectivity);
  if(!topology) {
    return std::unexpected(topology.error());
  }
  auto role_table = RoleTable::Create(std::move(roles), node_ids);
  if(!role_table) {
    return std::unexpected(role_table.error());
  }
  return StructureSnapshot::Create(PlanningCycleId{7},
                                   SnapshotVersion{5},
                                   std::move(*role_table),
                                   std::move(*connectivity),
                                   std::move(*topology));
}

auto HasExactEntries(const RoutingPlan& plan,
                     const std::vector<RouteEntry>& expected) -> bool {
  return plan.entries().size() == expected.size() &&
         std::equal(plan.entries().begin(),
                    plan.entries().end(),
                    expected.begin());
}

auto TestDirectAndMultiHop() -> bool {
  const auto direct = MakeStructure(
      {NodeId{2}, NodeId{1}, NodeId{0}},
      {RoleBinding{NodeId{2}, ProtocolRole::kSink}},
      {LogicalLink{NodeId{0}, NodeId{2}}});
  const auto multi_hop = MakeStructure(
      {NodeId{3}, NodeId{2}, NodeId{1}, NodeId{0}},
      {RoleBinding{NodeId{3}, ProtocolRole::kSink}},
      {LogicalLink{NodeId{2}, NodeId{3}},
       LogicalLink{NodeId{0}, NodeId{1}},
       LogicalLink{NodeId{1}, NodeId{2}}});
  if(!direct || !multi_hop) {
    return false;
  }

  const ShortestPathToSinkRoutingPlanner planner;
  const auto direct_plan = planner.Build(*direct);
  const auto multi_hop_plan = planner.Build(*multi_hop);
  return direct_plan &&
         HasExactEntries(
             *direct_plan,
             {RouteEntry{NodeId{0}, NodeId{2}, NodeId{2}}}) &&
         !direct_plan->FindNextHop(NodeId{1}, NodeId{2}) &&
         multi_hop_plan &&
         HasExactEntries(
             *multi_hop_plan,
             {RouteEntry{NodeId{0}, NodeId{3}, NodeId{1}},
              RouteEntry{NodeId{1}, NodeId{3}, NodeId{2}},
              RouteEntry{NodeId{2}, NodeId{3}, NodeId{3}}});
}

auto TestEqualHopTieBreakAndInputOrderIndependence() -> bool {
  const std::vector<NodeId> nodes{
      NodeId{0}, NodeId{1}, NodeId{2}, NodeId{4}};
  const std::vector<RoleBinding> roles{
      RoleBinding{NodeId{4}, ProtocolRole::kSink}};
  const auto first_structure = MakeStructure(
      nodes,
      roles,
      {LogicalLink{NodeId{0}, NodeId{2}},
       LogicalLink{NodeId{2}, NodeId{4}},
       LogicalLink{NodeId{0}, NodeId{1}},
       LogicalLink{NodeId{1}, NodeId{4}}});
  const auto reordered_structure = MakeStructure(
      {NodeId{4}, NodeId{2}, NodeId{0}, NodeId{1}},
      roles,
      {LogicalLink{NodeId{1}, NodeId{4}},
       LogicalLink{NodeId{0}, NodeId{1}},
       LogicalLink{NodeId{2}, NodeId{4}},
       LogicalLink{NodeId{0}, NodeId{2}}});
  if(!first_structure || !reordered_structure) {
    return false;
  }

  const ShortestPathToSinkRoutingPlanner planner;
  const auto first = planner.Build(*first_structure);
  const auto repeated = planner.Build(*first_structure);
  const auto reordered = planner.Build(*reordered_structure);
  return first && repeated && reordered && *first == *repeated &&
         *first == *reordered &&
         first->FindNextHop(NodeId{0}, NodeId{4}) == NodeId{1};
}

auto TestShorterPathBeatsSmallerNodeId() -> bool {
  const auto structure = MakeStructure(
      {NodeId{0}, NodeId{1}, NodeId{3}, NodeId{4}, NodeId{9}},
      {RoleBinding{NodeId{4}, ProtocolRole::kSink}},
      {LogicalLink{NodeId{0}, NodeId{1}},
       LogicalLink{NodeId{1}, NodeId{3}},
       LogicalLink{NodeId{3}, NodeId{4}},
       LogicalLink{NodeId{0}, NodeId{9}},
       LogicalLink{NodeId{9}, NodeId{4}}});
  if(!structure) {
    return false;
  }

  const auto plan = ShortestPathToSinkRoutingPlanner{}.Build(*structure);
  return plan &&
         plan->FindNextHop(NodeId{0}, NodeId{4}) == NodeId{9} &&
         plan->FindNextHop(NodeId{1}, NodeId{4}) == NodeId{3};
}

auto TestDirectedAsymmetryAndNoConnectivityBypass() -> bool {
  const auto structure = MakeStructure(
      {NodeId{0}, NodeId{2}},
      {RoleBinding{NodeId{2}, ProtocolRole::kSink}},
      {LogicalLink{NodeId{2}, NodeId{0}}},
      {DirectedLink{NodeId{0}, NodeId{2}}});
  if(!structure) {
    return false;
  }

  const auto plan = ShortestPathToSinkRoutingPlanner{}.Build(*structure);
  return plan && plan->entries().empty() &&
         !plan->FindNextHop(NodeId{0}, NodeId{2});
}

auto TestReachableAndUnreachableCycles() -> bool {
  const auto reachable = MakeStructure(
      {NodeId{0}, NodeId{1}, NodeId{2}},
      {RoleBinding{NodeId{2}, ProtocolRole::kSink}},
      {LogicalLink{NodeId{0}, NodeId{1}},
       LogicalLink{NodeId{1}, NodeId{0}},
       LogicalLink{NodeId{1}, NodeId{2}}});
  const auto unreachable = MakeStructure(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}, NodeId{4}},
      {RoleBinding{NodeId{3}, ProtocolRole::kSink}},
      {LogicalLink{NodeId{0}, NodeId{1}},
       LogicalLink{NodeId{1}, NodeId{0}},
       LogicalLink{NodeId{2}, NodeId{3}}});
  if(!reachable || !unreachable) {
    return false;
  }

  const ShortestPathToSinkRoutingPlanner planner;
  const auto reachable_plan = planner.Build(*reachable);
  const auto unreachable_plan = planner.Build(*unreachable);
  return reachable_plan &&
         reachable_plan->FindNextHop(NodeId{0}, NodeId{2}) == NodeId{1} &&
         reachable_plan->FindNextHop(NodeId{1}, NodeId{2}) == NodeId{2} &&
         unreachable_plan && unreachable_plan->entries().size() == 1 &&
         unreachable_plan->FindNextHop(NodeId{2}, NodeId{3}) == NodeId{3} &&
         !unreachable_plan->FindNextHop(NodeId{0}, NodeId{3}) &&
         !unreachable_plan->FindNextHop(NodeId{1}, NodeId{3}) &&
         !unreachable_plan->FindNextHop(NodeId{4}, NodeId{3});
}

auto TestRoleIndependence() -> bool {
  const auto structure = MakeStructure(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}},
      {RoleBinding{NodeId{1}, ProtocolRole::kRelay},
       RoleBinding{NodeId{2}, ProtocolRole::kController},
       RoleBinding{NodeId{3}, ProtocolRole::kSink}},
      {LogicalLink{NodeId{0}, NodeId{1}},
       LogicalLink{NodeId{1}, NodeId{2}},
       LogicalLink{NodeId{2}, NodeId{3}}});
  if(!structure) {
    return false;
  }

  const auto plan = ShortestPathToSinkRoutingPlanner{}.Build(*structure);
  return plan && plan->entries().size() == 3 &&
         plan->FindNextHop(NodeId{0}, NodeId{3}) == NodeId{1} &&
         plan->FindNextHop(NodeId{1}, NodeId{3}) == NodeId{2} &&
         plan->FindNextHop(NodeId{2}, NodeId{3}) == NodeId{3};
}

auto TestSinkErrorsAndRetry() -> bool {
  const auto no_sink = MakeStructure(
      {NodeId{0}, NodeId{1}}, {}, {});
  const auto two_sinks = MakeStructure(
      {NodeId{0}, NodeId{1}},
      {RoleBinding{NodeId{0}, ProtocolRole::kSink},
       RoleBinding{NodeId{1}, ProtocolRole::kSink}},
      {});
  const auto valid = MakeStructure(
      {NodeId{0}, NodeId{1}},
      {RoleBinding{NodeId{1}, ProtocolRole::kSink}},
      {LogicalLink{NodeId{0}, NodeId{1}}});
  if(!no_sink || !two_sinks || !valid) {
    return false;
  }

  const ShortestPathToSinkRoutingPlanner planner;
  const auto missing = planner.Build(*no_sink);
  const auto multiple = planner.Build(*two_sinks);
  const auto retried = planner.Build(*valid);
  return !missing &&
         missing.error().code == ErrorCode::kFailedPrecondition &&
         !multiple &&
         multiple.error().code == ErrorCode::kFailedPrecondition &&
         retried && retried->entries().size() == 1;
}

auto TestCompositePlannerIntegrationAndFutureRelayLookup() -> bool {
  const std::vector<NodeId> nodes{NodeId{0}, NodeId{1}, NodeId{2}};
  const auto structure = MakeStructure(
      nodes,
      {RoleBinding{NodeId{2}, ProtocolRole::kSink}},
      {LogicalLink{NodeId{0}, NodeId{1}},
       LogicalLink{NodeId{1}, NodeId{2}}});
  const auto world = WorldSnapshot::Create(
      SnapshotVersion{5}, At(100), {Node(2), Node(0), Node(1)});
  const auto policy = ConfiguredTdmaPolicy::Create(
      For(10), {NodeId{0}, NodeId{1}});
  if(!structure || !world || !policy) {
    return false;
  }

  const ShortestPathToSinkRoutingPlanner routing;
  const ConfiguredTdmaMacPlanner mac{*policy};
  const CompositeProtocolCyclePlanner planner{routing, mac};
  const auto plan = planner.Build(*world, *structure);
  if(!plan || !plan->routing_plan()) {
    return false;
  }

  const auto& routes = *plan->routing_plan();
  const auto opportunities = plan->mac_plan().tx_opportunities();
  return routes.FindNextHop(NodeId{0}, NodeId{2}) == NodeId{1} &&
         routes.FindNextHop(NodeId{1}, NodeId{2}) == NodeId{2} &&
         opportunities.size() == 2 &&
         opportunities[0] == TxOpportunity{NodeId{0}, At(100)} &&
         opportunities[1] == TxOpportunity{NodeId{1}, At(110)} &&
         plan->timing().closes_at() == At(120);
}

}  // namespace

auto main() -> int {
  return TestDirectAndMultiHop() &&
                 TestEqualHopTieBreakAndInputOrderIndependence() &&
                 TestShorterPathBeatsSmallerNodeId() &&
                 TestDirectedAsymmetryAndNoConnectivityBypass() &&
                 TestReachableAndUnreachableCycles() &&
                 TestRoleIndependence() && TestSinkErrorsAndRetry() &&
                 TestCompositePlannerIntegrationAndFutureRelayLookup()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
