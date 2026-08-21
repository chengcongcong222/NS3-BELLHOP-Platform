#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/node_capability.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/state.hpp>

#include "internal/configured_role_assignment_policy.hpp"

using ns3_factory::contracts::ConnectivityGraph;
using ns3_factory::contracts::DuplexMode;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::MotionState;
using ns3_factory::contracts::NodeCapabilityProfile;
using ns3_factory::contracts::NodeCommittedState;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::ProtocolRole;
using ns3_factory::contracts::RoleBinding;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::Velocity3d;
using ns3_factory::contracts::WorldSnapshot;
using ns3_factory::structure::internal::ConfiguredRoleAssignmentPolicy;

namespace {

constexpr auto MakeNode(std::uint64_t id) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, 0.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto MakeSnapshot() {
  return WorldSnapshot::Create(
      SnapshotVersion{7},
      SimTime::FromNanoseconds(100),
      std::vector<NodeCommittedState>{
          MakeNode(2), MakeNode(0), MakeNode(3), MakeNode(1)});
}

auto MakeGraph() {
  return ConnectivityGraph::Create(
      {}, {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
}

auto TestExplicitCanonicalMultiRoleAssignment() -> bool {
  const auto snapshot = MakeSnapshot();
  const auto graph = MakeGraph();
  if(!snapshot || !graph) {
    return false;
  }

  ConfiguredRoleAssignmentPolicy first{
      {RoleBinding{NodeId{2}, ProtocolRole::kController},
       RoleBinding{NodeId{1}, ProtocolRole::kMember},
       RoleBinding{NodeId{2}, ProtocolRole::kSink},
       RoleBinding{NodeId{0}, ProtocolRole::kMember}}};
  ConfiguredRoleAssignmentPolicy second{
      {RoleBinding{NodeId{0}, ProtocolRole::kMember},
       RoleBinding{NodeId{2}, ProtocolRole::kSink},
       RoleBinding{NodeId{2}, ProtocolRole::kController},
       RoleBinding{NodeId{1}, ProtocolRole::kMember}}};

  const auto first_result =
      first.Assign(*snapshot, *graph, PlanningCycleId{9});
  const auto second_result =
      second.Assign(*snapshot, *graph, PlanningCycleId{99});
  if(!first_result || !second_result ||
     *first_result != *second_result) {
    return false;
  }

  const std::vector<RoleBinding> expected{
      RoleBinding{NodeId{0}, ProtocolRole::kMember},
      RoleBinding{NodeId{1}, ProtocolRole::kMember},
      RoleBinding{NodeId{2}, ProtocolRole::kSink},
      RoleBinding{NodeId{2}, ProtocolRole::kController}};
  return first_result->bindings().size() == expected.size() &&
         std::equal(first_result->bindings().begin(),
                    first_result->bindings().end(),
                    expected.begin()) &&
         first_result->HasRole(NodeId{0}, ProtocolRole::kMember) &&
         first_result->HasRole(NodeId{2}, ProtocolRole::kSink) &&
         first_result->HasRole(NodeId{2}, ProtocolRole::kController) &&
         !first_result->HasRole(NodeId{3}, ProtocolRole::kMember);
}

auto TestInvalidConfigurationFailsExplicitly() -> bool {
  const auto snapshot = MakeSnapshot();
  const auto graph = MakeGraph();
  if(!snapshot || !graph) {
    return false;
  }

  ConfiguredRoleAssignmentPolicy unknown{
      {RoleBinding{NodeId{4}, ProtocolRole::kMember}}};
  ConfiguredRoleAssignmentPolicy duplicate{
      {RoleBinding{NodeId{0}, ProtocolRole::kMember},
       RoleBinding{NodeId{0}, ProtocolRole::kMember}}};
  const auto unknown_result =
      unknown.Assign(*snapshot, *graph, PlanningCycleId{1});
  const auto duplicate_result =
      duplicate.Assign(*snapshot, *graph, PlanningCycleId{1});

  return !unknown_result &&
         unknown_result.error().code == ErrorCode::kNotFound &&
         !duplicate_result &&
         duplicate_result.error().code == ErrorCode::kAlreadyExists;
}

}  // namespace

auto main() -> int {
  return TestExplicitCanonicalMultiRoleAssignment() &&
                 TestInvalidConfigurationFailsExplicitly()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
