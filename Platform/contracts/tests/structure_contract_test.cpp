#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/structure.hpp>

using ns3_factory::contracts::ConnectivityGraph;
using ns3_factory::contracts::DirectedLink;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::LogicalLink;
using ns3_factory::contracts::LogicalTopology;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::ProtocolRole;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::RoleBinding;
using ns3_factory::contracts::RoleTable;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::StructureSnapshot;

static_assert(static_cast<std::uint8_t>(ProtocolRole::kMember) == 1);
static_assert(static_cast<std::uint8_t>(ProtocolRole::kSink) == 2);
static_assert(static_cast<std::uint8_t>(ProtocolRole::kController) == 3);
static_assert(static_cast<std::uint8_t>(ProtocolRole::kRelay) == 4);
static_assert(static_cast<std::uint8_t>(ProtocolRole::kAccessNode) == 5);
static_assert(static_cast<std::uint8_t>(ProtocolRole::kAnchor) == 6);
static_assert(!std::same_as<DirectedLink, LogicalLink>);
static_assert(!std::is_default_constructible_v<RoleTable>);
static_assert(!std::is_default_constructible_v<ConnectivityGraph>);
static_assert(!std::is_default_constructible_v<LogicalTopology>);
static_assert(!std::is_default_constructible_v<StructureSnapshot>);
static_assert(!std::is_aggregate_v<StructureSnapshot>);
static_assert(std::same_as<
              decltype(std::declval<const RoleTable&>().bindings()),
              std::span<const RoleBinding>>);
static_assert(std::same_as<
              decltype(std::declval<const ConnectivityGraph&>().edges()),
              std::span<const DirectedLink>>);
static_assert(std::same_as<
              decltype(std::declval<const LogicalTopology&>().links()),
              std::span<const LogicalLink>>);
static_assert(std::same_as<
              decltype(std::declval<const StructureSnapshot&>().role_table()),
              const RoleTable&>);
static_assert(std::same_as<
              decltype(std::declval<const StructureSnapshot&>()
                           .connectivity_graph()),
              const ConnectivityGraph&>);
static_assert(std::same_as<
              decltype(std::declval<const StructureSnapshot&>()
                           .logical_topology()),
              const LogicalTopology&>);
static_assert(!std::assignable_from<
              decltype(std::declval<const RoleTable&>().bindings()[0]),
              RoleBinding>);

namespace {

auto Nodes012() -> std::vector<NodeId> {
  return {NodeId{2}, NodeId{0}, NodeId{1}};
}

auto TestRoleTable() -> bool {
  const std::vector<RoleBinding> first_input{
      RoleBinding{NodeId{2}, ProtocolRole::kController},
      RoleBinding{NodeId{1}, ProtocolRole::kSink},
      RoleBinding{NodeId{0}, ProtocolRole::kMember},
      RoleBinding{NodeId{2}, ProtocolRole::kSink}};
  const std::vector<RoleBinding> second_input{
      first_input[2], first_input[3], first_input[0], first_input[1]};
  const auto first = RoleTable::Create(first_input, Nodes012());
  const auto second = RoleTable::Create(
      second_input, {NodeId{1}, NodeId{2}, NodeId{0}});
  if(!first || !second || *first != *second) {
    return false;
  }

  const auto bindings = first->bindings();
  const auto sinks = first->NodesWithRole(ProtocolRole::kSink);
  const auto duplicate = RoleTable::Create(
      {RoleBinding{NodeId{2}, ProtocolRole::kSink},
       RoleBinding{NodeId{2}, ProtocolRole::kSink}},
      Nodes012());
  const auto unknown = RoleTable::Create(
      {RoleBinding{NodeId{99}, ProtocolRole::kMember}}, Nodes012());
  const auto invalid_role = RoleTable::Create(
      {RoleBinding{NodeId{0}, static_cast<ProtocolRole>(0)}}, Nodes012());
  const auto duplicate_universe = RoleTable::Create(
      {}, {NodeId{0}, NodeId{0}});

  return bindings.size() == 4 &&
         bindings[0] == RoleBinding{NodeId{0}, ProtocolRole::kMember} &&
         bindings[1] == RoleBinding{NodeId{1}, ProtocolRole::kSink} &&
         bindings[2] == RoleBinding{NodeId{2}, ProtocolRole::kSink} &&
         bindings[3] ==
             RoleBinding{NodeId{2}, ProtocolRole::kController} &&
         first->HasRole(NodeId{0}, ProtocolRole::kMember) &&
         first->HasRole(NodeId{2}, ProtocolRole::kSink) &&
         first->HasRole(NodeId{2}, ProtocolRole::kController) &&
         !first->HasRole(NodeId{2}, ProtocolRole::kRelay) &&
         sinks == std::vector<NodeId>{NodeId{1}, NodeId{2}} &&
         !duplicate &&
         duplicate.error().code == ErrorCode::kAlreadyExists &&
         !unknown && unknown.error().code == ErrorCode::kNotFound &&
         !invalid_role &&
         invalid_role.error().code == ErrorCode::kInvalidArgument &&
         !duplicate_universe &&
         duplicate_universe.error().code == ErrorCode::kAlreadyExists;
}

auto TestConnectivityGraph() -> bool {
  const std::vector<DirectedLink> first_input{
      DirectedLink{NodeId{2}, NodeId{1}},
      DirectedLink{NodeId{0}, NodeId{2}},
      DirectedLink{NodeId{1}, NodeId{0}},
      DirectedLink{NodeId{0}, NodeId{1}}};
  const std::vector<DirectedLink> second_input{
      first_input[3], first_input[1], first_input[0], first_input[2]};
  const auto first = ConnectivityGraph::Create(first_input, Nodes012());
  const auto second = ConnectivityGraph::Create(
      second_input, {NodeId{1}, NodeId{0}, NodeId{2}});
  const auto one_way = ConnectivityGraph::Create(
      {DirectedLink{NodeId{0}, NodeId{1}}}, Nodes012());
  if(!first || !second || *first != *second || !one_way) {
    return false;
  }

  const auto edges = first->edges();
  const auto outgoing = first->OutgoingNeighbors(NodeId{0});
  const auto incoming = first->IncomingNeighbors(NodeId{1});
  const auto self_loop = ConnectivityGraph::Create(
      {DirectedLink{NodeId{1}, NodeId{1}}}, Nodes012());
  const auto duplicate = ConnectivityGraph::Create(
      {DirectedLink{NodeId{0}, NodeId{1}},
       DirectedLink{NodeId{0}, NodeId{1}}},
      Nodes012());
  const auto unknown = ConnectivityGraph::Create(
      {DirectedLink{NodeId{0}, NodeId{99}}}, Nodes012());
  const auto empty = ConnectivityGraph::Create({}, Nodes012());

  return edges.size() == 4 &&
         edges[0] == DirectedLink{NodeId{0}, NodeId{1}} &&
         edges[1] == DirectedLink{NodeId{0}, NodeId{2}} &&
         edges[2] == DirectedLink{NodeId{1}, NodeId{0}} &&
         edges[3] == DirectedLink{NodeId{2}, NodeId{1}} &&
         first->HasEdge(NodeId{0}, NodeId{1}) &&
         first->HasEdge(NodeId{1}, NodeId{0}) &&
         one_way->HasEdge(NodeId{0}, NodeId{1}) &&
         !one_way->HasEdge(NodeId{1}, NodeId{0}) &&
         outgoing == std::vector<NodeId>{NodeId{1}, NodeId{2}} &&
         incoming == std::vector<NodeId>{NodeId{0}, NodeId{2}} &&
         !self_loop &&
         self_loop.error().code == ErrorCode::kInvalidArgument &&
         !duplicate &&
         duplicate.error().code == ErrorCode::kAlreadyExists &&
         !unknown && unknown.error().code == ErrorCode::kNotFound &&
         empty && empty->edges().empty();
}

auto MakeConnectivity(bool alternate_order)
    -> Result<ConnectivityGraph> {
  std::vector<DirectedLink> edges{
      DirectedLink{NodeId{0}, NodeId{1}},
      DirectedLink{NodeId{0}, NodeId{2}},
      DirectedLink{NodeId{1}, NodeId{2}},
      DirectedLink{NodeId{2}, NodeId{1}}};
  if(alternate_order) {
    edges = {edges[3], edges[1], edges[0], edges[2]};
  }
  return ConnectivityGraph::Create(
      std::move(edges),
      alternate_order ? std::vector<NodeId>{NodeId{1}, NodeId{2}, NodeId{0}}
                      : Nodes012());
}

auto TestLogicalTopology() -> bool {
  const auto connectivity = MakeConnectivity(false);
  if(!connectivity) {
    return false;
  }

  const std::vector<LogicalLink> first_input{
      LogicalLink{NodeId{1}, NodeId{2}},
      LogicalLink{NodeId{0}, NodeId{2}}};
  const std::vector<LogicalLink> second_input{
      first_input[1], first_input[0]};
  const auto first = LogicalTopology::Create(
      first_input, Nodes012(), *connectivity);
  const auto second = LogicalTopology::Create(
      second_input, {NodeId{1}, NodeId{0}, NodeId{2}}, *connectivity);
  const auto invalid_subset = LogicalTopology::Create(
      {LogicalLink{NodeId{2}, NodeId{0}}}, Nodes012(), *connectivity);
  const auto self_loop = LogicalTopology::Create(
      {LogicalLink{NodeId{1}, NodeId{1}}}, Nodes012(), *connectivity);
  const auto duplicate = LogicalTopology::Create(
      {LogicalLink{NodeId{0}, NodeId{2}},
       LogicalLink{NodeId{0}, NodeId{2}}},
      Nodes012(),
      *connectivity);
  const auto unknown = LogicalTopology::Create(
      {LogicalLink{NodeId{0}, NodeId{99}}}, Nodes012(), *connectivity);
  const auto different_universe = LogicalTopology::Create(
      {}, {NodeId{0}, NodeId{1}}, *connectivity);
  const auto empty = LogicalTopology::Create({}, Nodes012(), *connectivity);

  return first && second && *first == *second &&
         first->links().size() == 2 &&
         first->links()[0] == LogicalLink{NodeId{0}, NodeId{2}} &&
         first->links()[1] == LogicalLink{NodeId{1}, NodeId{2}} &&
         first->HasLink(NodeId{0}, NodeId{2}) &&
         !first->HasLink(NodeId{2}, NodeId{0}) && !invalid_subset &&
         invalid_subset.error().code == ErrorCode::kFailedPrecondition &&
         !self_loop &&
         self_loop.error().code == ErrorCode::kInvalidArgument &&
         !duplicate &&
         duplicate.error().code == ErrorCode::kAlreadyExists &&
         !unknown && unknown.error().code == ErrorCode::kNotFound &&
         !different_universe &&
         different_universe.error().code ==
             ErrorCode::kFailedPrecondition &&
         empty && empty->links().empty();
}

auto MakeRoleTable(bool alternate_order) -> Result<RoleTable> {
  std::vector<RoleBinding> bindings{
      RoleBinding{NodeId{2}, ProtocolRole::kSink},
      RoleBinding{NodeId{0}, ProtocolRole::kMember},
      RoleBinding{NodeId{1}, ProtocolRole::kRelay}};
  if(alternate_order) {
    bindings = {bindings[1], bindings[2], bindings[0]};
  }
  return RoleTable::Create(
      std::move(bindings),
      alternate_order ? std::vector<NodeId>{NodeId{1}, NodeId{0}, NodeId{2}}
                      : Nodes012());
}

auto MakeLogicalTopology(bool alternate_order,
                         const ConnectivityGraph& connectivity)
    -> Result<LogicalTopology> {
  std::vector<LogicalLink> links{
      LogicalLink{NodeId{0}, NodeId{2}},
      LogicalLink{NodeId{1}, NodeId{2}}};
  if(alternate_order) {
    links = {links[1], links[0]};
  }
  return LogicalTopology::Create(
      std::move(links),
      alternate_order ? std::vector<NodeId>{NodeId{2}, NodeId{0}, NodeId{1}}
                      : Nodes012(),
      connectivity);
}

auto TestStructureSnapshot() -> bool {
  const auto roles_first = MakeRoleTable(false);
  const auto roles_second = MakeRoleTable(true);
  const auto graph_first = MakeConnectivity(false);
  const auto graph_second = MakeConnectivity(true);
  if(!roles_first || !roles_second || !graph_first || !graph_second) {
    return false;
  }
  const auto topology_first =
      MakeLogicalTopology(false, *graph_first);
  const auto topology_second =
      MakeLogicalTopology(true, *graph_second);
  if(!topology_first || !topology_second) {
    return false;
  }

  const auto first = StructureSnapshot::Create(
      PlanningCycleId{7},
      SnapshotVersion{11},
      *roles_first,
      *graph_first,
      *topology_first);
  const auto second = StructureSnapshot::Create(
      PlanningCycleId{7},
      SnapshotVersion{11},
      *roles_second,
      *graph_second,
      *topology_second);

  const auto smaller_roles = RoleTable::Create(
      {RoleBinding{NodeId{0}, ProtocolRole::kMember}},
      {NodeId{0}, NodeId{1}});
  if(!smaller_roles) {
    return false;
  }
  const auto mismatched_universe = StructureSnapshot::Create(
      PlanningCycleId{7},
      SnapshotVersion{11},
      *smaller_roles,
      *graph_first,
      *topology_first);
  const auto invalid_subset = LogicalTopology::Create(
      {LogicalLink{NodeId{2}, NodeId{0}}}, Nodes012(), *graph_first);

  return first && second && *first == *second &&
         first->cycle_id() == PlanningCycleId{7} &&
         first->base_snapshot_version() == SnapshotVersion{11} &&
         first->role_table() == *roles_first &&
         first->connectivity_graph() == *graph_first &&
         first->logical_topology() == *topology_first &&
         !mismatched_universe &&
         mismatched_universe.error().code ==
             ErrorCode::kFailedPrecondition &&
         !invalid_subset &&
         invalid_subset.error().code == ErrorCode::kFailedPrecondition;
}

}  // namespace

auto main() -> int {
  if(!TestRoleTable() || !TestConnectivityGraph() ||
     !TestLogicalTopology() || !TestStructureSnapshot()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
