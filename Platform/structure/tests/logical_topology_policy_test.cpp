#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/node_capability.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/topology.hpp>

#include "internal/all_feasible_links_topology_policy.hpp"
#include "internal/single_sink_star_topology_policy.hpp"

using ns3_factory::contracts::ConnectivityGraph;
using ns3_factory::contracts::DirectedLink;
using ns3_factory::contracts::DuplexMode;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::LogicalLink;
using ns3_factory::contracts::MotionState;
using ns3_factory::contracts::NodeCapabilityProfile;
using ns3_factory::contracts::NodeCommittedState;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::ProtocolRole;
using ns3_factory::contracts::RoleBinding;
using ns3_factory::contracts::RoleTable;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::Velocity3d;
using ns3_factory::contracts::WorldSnapshot;
using ns3_factory::structure::internal::AllFeasibleLinksTopologyPolicy;
using ns3_factory::structure::internal::SingleSinkStarTopologyPolicy;

namespace {

constexpr auto MakeNode(std::uint64_t id) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, 0.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto MakeSnapshot(std::size_t node_count = 4) {
  std::vector<NodeCommittedState> nodes;
  for(std::size_t id = 0; id < node_count; ++id) {
    nodes.push_back(MakeNode(id));
  }
  return WorldSnapshot::Create(SnapshotVersion{3},
                               SimTime::FromNanoseconds(20),
                               std::move(nodes));
}

auto Nodes(std::size_t count) {
  std::vector<NodeId> result;
  for(std::size_t id = 0; id < count; ++id) {
    result.push_back(NodeId{id});
  }
  return result;
}

auto SameLinks(std::span<const LogicalLink> actual,
               const std::vector<LogicalLink>& expected) -> bool {
  return actual.size() == expected.size() &&
         std::equal(actual.begin(), actual.end(), expected.begin());
}

auto TestAllFeasiblePreservesExactDirectedSet() -> bool {
  const auto snapshot = MakeSnapshot(3);
  const auto graph = ConnectivityGraph::Create(
      {DirectedLink{NodeId{1}, NodeId{2}},
       DirectedLink{NodeId{1}, NodeId{0}},
       DirectedLink{NodeId{0}, NodeId{1}}},
      Nodes(3));
  const auto roles = RoleTable::Create({}, Nodes(3));
  if(!snapshot || !graph || !roles) {
    return false;
  }

  const AllFeasibleLinksTopologyPolicy policy;
  const auto topology = policy.Build(*snapshot, *roles, *graph);
  const std::vector<LogicalLink> expected{
      LogicalLink{NodeId{0}, NodeId{1}},
      LogicalLink{NodeId{1}, NodeId{0}},
      LogicalLink{NodeId{1}, NodeId{2}}};
  return topology && SameLinks(topology->links(), expected) &&
         !topology->HasLink(NodeId{2}, NodeId{1});
}

auto TestSingleSinkStarExactDirectedFilter() -> bool {
  const auto snapshot = MakeSnapshot();
  const auto graph = ConnectivityGraph::Create(
      {DirectedLink{NodeId{3}, NodeId{1}},
       DirectedLink{NodeId{2}, NodeId{3}},
       DirectedLink{NodeId{1}, NodeId{2}},
       DirectedLink{NodeId{0}, NodeId{1}},
       DirectedLink{NodeId{2}, NodeId{0}},
       DirectedLink{NodeId{0}, NodeId{2}}},
      Nodes(4));
  const auto roles = RoleTable::Create(
      {RoleBinding{NodeId{0}, ProtocolRole::kMember},
       RoleBinding{NodeId{1}, ProtocolRole::kMember},
       RoleBinding{NodeId{2}, ProtocolRole::kSink}},
      Nodes(4));
  if(!snapshot || !graph || !roles) {
    return false;
  }

  const SingleSinkStarTopologyPolicy policy;
  const auto topology = policy.Build(*snapshot, *roles, *graph);
  const std::vector<LogicalLink> expected{
      LogicalLink{NodeId{0}, NodeId{2}},
      LogicalLink{NodeId{1}, NodeId{2}},
      LogicalLink{NodeId{2}, NodeId{0}},
      LogicalLink{NodeId{2}, NodeId{3}}};
  return topology && SameLinks(topology->links(), expected) &&
         !topology->HasLink(NodeId{0}, NodeId{1}) &&
         !topology->HasLink(NodeId{3}, NodeId{1});
}

auto TestSinkCardinalityFailures() -> bool {
  const auto snapshot = MakeSnapshot(3);
  const auto graph = ConnectivityGraph::Create({}, Nodes(3));
  const auto no_sink = RoleTable::Create(
      {RoleBinding{NodeId{0}, ProtocolRole::kMember}}, Nodes(3));
  const auto two_sinks = RoleTable::Create(
      {RoleBinding{NodeId{0}, ProtocolRole::kSink},
       RoleBinding{NodeId{2}, ProtocolRole::kSink}},
      Nodes(3));
  if(!snapshot || !graph || !no_sink || !two_sinks) {
    return false;
  }

  const SingleSinkStarTopologyPolicy policy;
  const auto no_sink_result = policy.Build(*snapshot, *no_sink, *graph);
  const auto two_sink_result = policy.Build(*snapshot, *two_sinks, *graph);
  return !no_sink_result &&
         no_sink_result.error().code == ErrorCode::kFailedPrecondition &&
         !two_sink_result &&
         two_sink_result.error().code == ErrorCode::kFailedPrecondition;
}

auto TestDisconnectedMemberIsValid() -> bool {
  const auto snapshot = MakeSnapshot(3);
  const auto graph = ConnectivityGraph::Create(
      {DirectedLink{NodeId{0}, NodeId{2}}}, Nodes(3));
  const auto roles = RoleTable::Create(
      {RoleBinding{NodeId{0}, ProtocolRole::kMember},
       RoleBinding{NodeId{1}, ProtocolRole::kMember},
       RoleBinding{NodeId{2}, ProtocolRole::kSink}},
      Nodes(3));
  if(!snapshot || !graph || !roles) {
    return false;
  }

  const SingleSinkStarTopologyPolicy policy;
  const auto topology = policy.Build(*snapshot, *roles, *graph);
  return topology && topology->links().size() == 1 &&
         topology->HasLink(NodeId{0}, NodeId{2}) &&
         !topology->HasLink(NodeId{1}, NodeId{2}) &&
         !topology->HasLink(NodeId{2}, NodeId{1});
}

}  // namespace

auto main() -> int {
  return TestAllFeasiblePreservesExactDirectedSet() &&
                 TestSingleSinkStarExactDirectedFilter() &&
                 TestSinkCardinalityFailures() &&
                 TestDisconnectedMemberIsValid()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
