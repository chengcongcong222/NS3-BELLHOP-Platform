#include <algorithm>
#include <cstdlib>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/routing.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/topology.hpp>

using ns3_factory::contracts::ConnectivityGraph;
using ns3_factory::contracts::DirectedLink;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::LogicalLink;
using ns3_factory::contracts::LogicalTopology;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::RoleTable;
using ns3_factory::contracts::RouteEntry;
using ns3_factory::contracts::RoutingPlan;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::StructureSnapshot;

static_assert(std::is_aggregate_v<RouteEntry>);
static_assert(!std::is_default_constructible_v<RoutingPlan>);
static_assert(std::is_same_v<
              decltype(std::declval<const RoutingPlan&>().entries()),
              std::span<const RouteEntry>>);
static_assert(std::is_same_v<
              decltype(std::declval<const RoutingPlan&>().FindNextHop(
                  NodeId{0}, NodeId{1})),
              std::optional<NodeId>>);

namespace {

auto NodeUniverse() -> std::vector<NodeId> {
  return {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}};
}

auto MakeStructure(bool include_alternate_next_hop = false)
    -> Result<StructureSnapshot> {
  auto connectivity = ConnectivityGraph::Create(
      {DirectedLink{NodeId{3}, NodeId{0}},
       DirectedLink{NodeId{2}, NodeId{0}},
       DirectedLink{NodeId{1}, NodeId{3}},
       DirectedLink{NodeId{0}, NodeId{2}},
       DirectedLink{NodeId{0}, NodeId{1}}},
      NodeUniverse());
  if(!connectivity) {
    return std::unexpected(connectivity.error());
  }

  std::vector<LogicalLink> links{
      LogicalLink{NodeId{3}, NodeId{0}},
      LogicalLink{NodeId{2}, NodeId{0}},
      LogicalLink{NodeId{1}, NodeId{3}},
      LogicalLink{NodeId{0}, NodeId{1}}};
  if(include_alternate_next_hop) {
    links.push_back(LogicalLink{NodeId{0}, NodeId{2}});
  }
  auto topology = LogicalTopology::Create(
      std::move(links), NodeUniverse(), *connectivity);
  if(!topology) {
    return std::unexpected(topology.error());
  }

  auto roles = RoleTable::Create({}, NodeUniverse());
  if(!roles) {
    return std::unexpected(roles.error());
  }
  return StructureSnapshot::Create(PlanningCycleId{7},
                                   SnapshotVersion{4},
                                   std::move(*roles),
                                   std::move(*connectivity),
                                   std::move(*topology));
}

auto TestIdentityAndLogicalEdgeValidation() -> bool {
  const auto structure = MakeStructure();
  if(!structure) {
    return false;
  }

  const auto forwarding_is_destination = RoutingPlan::Create(
      {RouteEntry{NodeId{3}, NodeId{3}, NodeId{0}}}, *structure);
  const auto forwarding_is_next_hop = RoutingPlan::Create(
      {RouteEntry{NodeId{0}, NodeId{3}, NodeId{0}}}, *structure);
  const auto unknown_forwarding = RoutingPlan::Create(
      {RouteEntry{NodeId{9}, NodeId{3}, NodeId{1}}}, *structure);
  const auto unknown_destination = RoutingPlan::Create(
      {RouteEntry{NodeId{0}, NodeId{9}, NodeId{1}}}, *structure);
  const auto unknown_next_hop = RoutingPlan::Create(
      {RouteEntry{NodeId{0}, NodeId{3}, NodeId{9}}}, *structure);
  const auto connectivity_only_edge = RoutingPlan::Create(
      {RouteEntry{NodeId{0}, NodeId{3}, NodeId{2}}}, *structure);

  return !forwarding_is_destination &&
         forwarding_is_destination.error().code ==
             ErrorCode::kInvalidArgument &&
         !forwarding_is_next_hop &&
         forwarding_is_next_hop.error().code ==
             ErrorCode::kInvalidArgument &&
         !unknown_forwarding &&
         unknown_forwarding.error().code == ErrorCode::kNotFound &&
         !unknown_destination &&
         unknown_destination.error().code == ErrorCode::kNotFound &&
         !unknown_next_hop &&
         unknown_next_hop.error().code == ErrorCode::kNotFound &&
         !connectivity_only_edge &&
         connectivity_only_edge.error().code ==
             ErrorCode::kFailedPrecondition;
}

auto TestUniqueRouteKey() -> bool {
  const auto structure = MakeStructure();
  const auto structure_with_alternate = MakeStructure(true);
  if(!structure || !structure_with_alternate) {
    return false;
  }

  const auto duplicate = RoutingPlan::Create(
      {RouteEntry{NodeId{0}, NodeId{3}, NodeId{1}},
       RouteEntry{NodeId{0}, NodeId{3}, NodeId{1}}},
      *structure);
  const auto ambiguous = RoutingPlan::Create(
      {RouteEntry{NodeId{0}, NodeId{3}, NodeId{1}},
       RouteEntry{NodeId{0}, NodeId{3}, NodeId{2}}},
      *structure_with_alternate);

  return !duplicate &&
         duplicate.error().code == ErrorCode::kAlreadyExists &&
         !ambiguous &&
         ambiguous.error().code == ErrorCode::kAlreadyExists;
}

auto TestCanonicalQueriesAndProvenance() -> bool {
  const auto structure = MakeStructure();
  if(!structure) {
    return false;
  }

  const std::vector<RouteEntry> canonical{
      RouteEntry{NodeId{0}, NodeId{3}, NodeId{1}},
      RouteEntry{NodeId{1}, NodeId{3}, NodeId{3}},
      RouteEntry{NodeId{2}, NodeId{1}, NodeId{0}}};
  const auto first = RoutingPlan::Create(
      {canonical[2], canonical[0], canonical[1]}, *structure);
  const auto second = RoutingPlan::Create(
      {canonical[1], canonical[2], canonical[0]}, *structure);
  const auto empty = RoutingPlan::Create({}, *structure);
  if(!first || !second || !empty || *first != *second ||
     first->entries().size() != canonical.size() ||
     !std::equal(first->entries().begin(),
                 first->entries().end(),
                 canonical.begin())) {
    return false;
  }

  const auto from_zero = first->FindNextHop(NodeId{0}, NodeId{3});
  const auto direct = first->FindNextHop(NodeId{1}, NodeId{3});
  const auto zero_next_hop = first->FindNextHop(NodeId{2}, NodeId{1});
  const auto no_route = first->FindNextHop(NodeId{2}, NodeId{3});
  return first->cycle_id() == PlanningCycleId{7} &&
         first->base_snapshot_version() == SnapshotVersion{4} &&
         from_zero && *from_zero == NodeId{1} && direct &&
         *direct == NodeId{3} && zero_next_hop &&
         *zero_next_hop == NodeId{0} && !no_route &&
         empty->entries().empty();
}

}  // namespace

auto main() -> int {
  return TestIdentityAndLogicalEdgeValidation() &&
                 TestUniqueRouteKey() &&
                 TestCanonicalQueriesAndProvenance()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
