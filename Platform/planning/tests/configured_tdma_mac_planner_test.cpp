#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/node_capability.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/topology.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

#include "internal/configured_tdma_mac_planner.hpp"
#include "internal/configured_tdma_policy.hpp"
#include "internal/mac_planner.hpp"
#include "internal/mac_planning_result.hpp"

using ns3_factory::contracts::ConnectivityGraph;
using ns3_factory::contracts::DirectedLink;
using ns3_factory::contracts::DuplexMode;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::LogicalLink;
using ns3_factory::contracts::LogicalTopology;
using ns3_factory::contracts::MotionState;
using ns3_factory::contracts::NodeCapabilityProfile;
using ns3_factory::contracts::NodeCommittedState;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::ProtocolRole;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::RoleBinding;
using ns3_factory::contracts::RoleTable;
using ns3_factory::contracts::SimDuration;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::StructureSnapshot;
using ns3_factory::contracts::TxOpportunity;
using ns3_factory::contracts::Velocity3d;
using ns3_factory::contracts::WorldSnapshot;
using ns3_factory::planning::internal::ConfiguredTdmaMacPlanner;
using ns3_factory::planning::internal::ConfiguredTdmaPolicy;
using ns3_factory::planning::internal::IMacPlanner;
using ns3_factory::planning::internal::MacPlanningResult;

static_assert(std::derived_from<ConfiguredTdmaMacPlanner, IMacPlanner>);
static_assert(std::has_virtual_destructor_v<IMacPlanner>);
static_assert(!std::is_default_constructible_v<ConfiguredTdmaPolicy>);
static_assert(!std::is_default_constructible_v<MacPlanningResult>);
static_assert(std::is_same_v<
              decltype(std::declval<const ConfiguredTdmaPolicy&>()
                           .slot_owners()),
              std::span<const NodeId>>);

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

constexpr auto For(std::int64_t nanoseconds) -> SimDuration {
  return SimDuration::FromNanoseconds(nanoseconds);
}

constexpr auto MakeNode(std::uint64_t id,
                        bool can_transmit = true) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{
          can_transmit, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, 0.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto MakeWorld(std::vector<NodeCommittedState> nodes,
               SimTime committed_at = At(1'000'000'000),
               SnapshotVersion version = SnapshotVersion{4})
    -> Result<WorldSnapshot> {
  return WorldSnapshot::Create(
      version, committed_at, std::move(nodes));
}

auto MakeStructure(
    std::vector<NodeId> node_ids,
    SnapshotVersion base_version = SnapshotVersion{4},
    std::vector<RoleBinding> role_bindings = {},
    std::vector<DirectedLink> connectivity_edges = {},
    std::vector<LogicalLink> logical_links = {})
    -> Result<StructureSnapshot> {
  auto connectivity = ConnectivityGraph::Create(
      std::move(connectivity_edges), node_ids);
  if(!connectivity) {
    return std::unexpected(connectivity.error());
  }
  auto topology = LogicalTopology::Create(
      std::move(logical_links), node_ids, *connectivity);
  if(!topology) {
    return std::unexpected(topology.error());
  }
  auto roles = RoleTable::Create(std::move(role_bindings), node_ids);
  if(!roles) {
    return std::unexpected(roles.error());
  }
  return StructureSnapshot::Create(PlanningCycleId{7},
                                   base_version,
                                   std::move(*roles),
                                   std::move(*connectivity),
                                   std::move(*topology));
}

auto TestExactScheduleRoleAndRoutingIndependence() -> bool {
  const auto world = MakeWorld({MakeNode(2), MakeNode(0), MakeNode(1)});
  const auto structure = MakeStructure(
      {NodeId{0}, NodeId{1}, NodeId{2}},
      SnapshotVersion{4},
      {RoleBinding{NodeId{2}, ProtocolRole::kSink},
       RoleBinding{NodeId{0}, ProtocolRole::kRelay}});
  const auto policy = ConfiguredTdmaPolicy::Create(
      For(100'000'000), {NodeId{2}, NodeId{0}, NodeId{1}});
  if(!world || !structure || !policy) {
    return false;
  }

  const ConfiguredTdmaMacPlanner planner{*policy};
  const auto first = planner.Build(*world, *structure);
  const auto second = planner.Build(*world, *structure);
  if(!first || !second || *first != *second) {
    return false;
  }

  const auto& timing = first->timing();
  const auto opportunities = first->mac_plan().tx_opportunities();
  return timing.cycle_id() == PlanningCycleId{7} &&
         timing.base_snapshot_version() == SnapshotVersion{4} &&
         timing.starts_at() == At(1'000'000'000) &&
         timing.closes_at() == At(1'300'000'000) &&
         opportunities.size() == 3 &&
         opportunities[0] ==
             TxOpportunity{NodeId{2}, At(1'000'000'000)} &&
         opportunities[1] ==
             TxOpportunity{NodeId{0}, At(1'100'000'000)} &&
         opportunities[2] ==
             TxOpportunity{NodeId{1}, At(1'200'000'000)};
}

auto TestSlotOwnerOrderIsSemantic() -> bool {
  const auto world = MakeWorld({MakeNode(0), MakeNode(1), MakeNode(2)});
  const auto structure =
      MakeStructure({NodeId{0}, NodeId{1}, NodeId{2}});
  const auto first_policy = ConfiguredTdmaPolicy::Create(
      For(10), {NodeId{2}, NodeId{0}, NodeId{1}});
  const auto second_policy = ConfiguredTdmaPolicy::Create(
      For(10), {NodeId{0}, NodeId{1}, NodeId{2}});
  if(!world || !structure || !first_policy || !second_policy) {
    return false;
  }

  const auto first =
      ConfiguredTdmaMacPlanner{*first_policy}.Build(*world, *structure);
  const auto second =
      ConfiguredTdmaMacPlanner{*second_policy}.Build(*world, *structure);
  return first && second && *first != *second &&
         first->mac_plan().tx_opportunities()[0].sender_node_id ==
             NodeId{2} &&
         second->mac_plan().tx_opportunities()[0].sender_node_id ==
             NodeId{0};
}

auto TestRepeatedOwnerIsValid() -> bool {
  const auto world = MakeWorld({MakeNode(0), MakeNode(1)});
  const auto structure = MakeStructure({NodeId{0}, NodeId{1}});
  const auto policy = ConfiguredTdmaPolicy::Create(
      For(25), {NodeId{0}, NodeId{1}, NodeId{0}});
  if(!world || !structure || !policy) {
    return false;
  }

  const auto result =
      ConfiguredTdmaMacPlanner{*policy}.Build(*world, *structure);
  if(!result) {
    return false;
  }
  const auto opportunities = result->mac_plan().tx_opportunities();
  return opportunities.size() == 3 &&
         opportunities[0] ==
             TxOpportunity{NodeId{0}, At(1'000'000'000)} &&
         opportunities[1] ==
             TxOpportunity{NodeId{1}, At(1'000'000'025)} &&
         opportunities[2] ==
             TxOpportunity{NodeId{0}, At(1'000'000'050)};
}

auto TestPolicyValidation() -> bool {
  const auto positive =
      ConfiguredTdmaPolicy::Create(For(1), {NodeId{0}});
  const auto zero = ConfiguredTdmaPolicy::Create(
      SimDuration::Zero(), {NodeId{0}});
  const auto negative =
      ConfiguredTdmaPolicy::Create(For(-1), {NodeId{0}});
  const auto empty = ConfiguredTdmaPolicy::Create(For(1), {});
  return positive && !zero &&
         zero.error().code == ErrorCode::kInvalidArgument &&
         !negative &&
         negative.error().code == ErrorCode::kInvalidArgument &&
         !empty && empty.error().code == ErrorCode::kInvalidArgument;
}

auto TestCapabilityFailureUnknownOwnerAndRetry() -> bool {
  const auto disabled_world = MakeWorld({MakeNode(0, false)});
  const auto valid_world = MakeWorld({MakeNode(0, true)});
  const auto structure = MakeStructure({NodeId{0}});
  const auto policy =
      ConfiguredTdmaPolicy::Create(For(10), {NodeId{0}});
  const auto unknown_policy =
      ConfiguredTdmaPolicy::Create(For(10), {NodeId{9}});
  if(!disabled_world || !valid_world || !structure || !policy ||
     !unknown_policy) {
    return false;
  }

  const ConfiguredTdmaMacPlanner planner{*policy};
  const auto disabled = planner.Build(*disabled_world, *structure);
  const auto retried = planner.Build(*valid_world, *structure);
  const auto unknown = ConfiguredTdmaMacPlanner{*unknown_policy}.Build(
      *valid_world, *structure);
  return !disabled &&
         disabled.error().code == ErrorCode::kFailedPrecondition &&
         retried && retried->mac_plan().tx_opportunities().size() == 1 &&
         !unknown && unknown.error().code == ErrorCode::kNotFound;
}

auto TestWorldStructureMismatch() -> bool {
  const auto version_mismatch_world = MakeWorld(
      {MakeNode(0)}, At(0), SnapshotVersion{5});
  const auto world = MakeWorld({MakeNode(0)}, At(0), SnapshotVersion{4});
  const auto structure =
      MakeStructure({NodeId{0}}, SnapshotVersion{4});
  const auto universe_mismatch_structure =
      MakeStructure({NodeId{1}}, SnapshotVersion{4});
  const auto policy =
      ConfiguredTdmaPolicy::Create(For(10), {NodeId{0}});
  if(!version_mismatch_world || !world || !structure ||
     !universe_mismatch_structure || !policy) {
    return false;
  }

  const ConfiguredTdmaMacPlanner planner{*policy};
  const auto version_mismatch =
      planner.Build(*version_mismatch_world, *structure);
  const auto universe_mismatch =
      planner.Build(*world, *universe_mismatch_structure);
  return !version_mismatch &&
         version_mismatch.error().code ==
             ErrorCode::kFailedPrecondition &&
         !universe_mismatch &&
         universe_mismatch.error().code ==
             ErrorCode::kFailedPrecondition;
}

auto TestCheckedTimeOverflowIsAtomic() -> bool {
  constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
  const auto close_overflow_world = MakeWorld({MakeNode(0)}, At(kMax));
  const auto intermediate_overflow_world =
      MakeWorld({MakeNode(0), MakeNode(1)}, At(kMax - 1));
  const auto retry_world = MakeWorld({MakeNode(0)}, At(0));
  const auto one_node_structure = MakeStructure({NodeId{0}});
  const auto two_node_structure =
      MakeStructure({NodeId{0}, NodeId{1}});
  const auto close_policy =
      ConfiguredTdmaPolicy::Create(For(1), {NodeId{0}});
  const auto intermediate_policy =
      ConfiguredTdmaPolicy::Create(For(2), {NodeId{0}, NodeId{1}});
  if(!close_overflow_world || !intermediate_overflow_world || !retry_world ||
     !one_node_structure || !two_node_structure || !close_policy ||
     !intermediate_policy) {
    return false;
  }

  const ConfiguredTdmaMacPlanner close_planner{*close_policy};
  const auto close_overflow = close_planner.Build(
      *close_overflow_world, *one_node_structure);
  const auto retried = close_planner.Build(*retry_world,
                                           *one_node_structure);
  const auto intermediate_overflow =
      ConfiguredTdmaMacPlanner{*intermediate_policy}.Build(
          *intermediate_overflow_world, *two_node_structure);
  return !close_overflow &&
         close_overflow.error().code == ErrorCode::kOverflow &&
         retried && retried->timing().starts_at() == At(0) &&
         retried->timing().closes_at() == At(1) &&
         !intermediate_overflow &&
         intermediate_overflow.error().code == ErrorCode::kOverflow;
}

}  // namespace

auto main() -> int {
  return TestExactScheduleRoleAndRoutingIndependence() &&
                 TestSlotOwnerOrderIsSemantic() &&
                 TestRepeatedOwnerIsValid() && TestPolicyValidation() &&
                 TestCapabilityFailureUnknownOwnerAndRetry() &&
                 TestWorldStructureMismatch() &&
                 TestCheckedTimeOverflowIsAtomic()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
