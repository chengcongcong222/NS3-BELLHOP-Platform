#include <cstdlib>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/routing.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/topology.hpp>

using ns3_factory::contracts::CycleTiming;
using ns3_factory::contracts::ConnectivityGraph;
using ns3_factory::contracts::DirectedLink;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::LogicalLink;
using ns3_factory::contracts::LogicalTopology;
using ns3_factory::contracts::MacPlan;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::ProtocolCyclePlan;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::RoleTable;
using ns3_factory::contracts::RouteEntry;
using ns3_factory::contracts::RoutingPlan;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::StructureSnapshot;
using ns3_factory::contracts::TxOpportunity;

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

static_assert(std::is_same_v<
              decltype(std::declval<TxOpportunity>().sender_node_id),
              NodeId>);
static_assert(std::is_same_v<
              decltype(std::declval<TxOpportunity>().eligible_at),
              SimTime>);
static_assert(std::is_same_v<
              decltype(std::declval<const ProtocolCyclePlan&>()
                           .routing_plan()),
              const std::optional<RoutingPlan>&>);

auto MakeStructure(PlanningCycleId cycle_id,
                   SnapshotVersion base_version)
    -> Result<StructureSnapshot> {
  const std::vector<NodeId> node_ids{NodeId{0}, NodeId{1}};
  auto connectivity = ConnectivityGraph::Create(
      {DirectedLink{NodeId{0}, NodeId{1}}}, node_ids);
  if(!connectivity) {
    return std::unexpected(connectivity.error());
  }
  auto topology = LogicalTopology::Create(
      {LogicalLink{NodeId{0}, NodeId{1}}}, node_ids, *connectivity);
  if(!topology) {
    return std::unexpected(topology.error());
  }
  auto roles = RoleTable::Create({}, node_ids);
  if(!roles) {
    return std::unexpected(roles.error());
  }
  return StructureSnapshot::Create(cycle_id,
                                   base_version,
                                   std::move(*roles),
                                   std::move(*connectivity),
                                   std::move(*topology));
}

auto MakeRoutingPlan(PlanningCycleId cycle_id,
                     SnapshotVersion base_version)
    -> Result<RoutingPlan> {
  auto structure = MakeStructure(cycle_id, base_version);
  if(!structure) {
    return std::unexpected(structure.error());
  }
  return RoutingPlan::Create(
      {RouteEntry{NodeId{0}, NodeId{1}, NodeId{1}}}, *structure);
}

auto TestTimingValidation() -> bool {
  const auto valid = CycleTiming::Create(
      PlanningCycleId{0}, SnapshotVersion{0}, At(0), At(10));
  const auto equal = CycleTiming::Create(
      PlanningCycleId{0}, SnapshotVersion{0}, At(10), At(10));
  const auto reversed = CycleTiming::Create(
      PlanningCycleId{0}, SnapshotVersion{0}, At(11), At(10));

  return valid && valid->cycle_id() == PlanningCycleId{0} &&
         valid->base_snapshot_version() == SnapshotVersion{0} &&
         valid->starts_at() == At(0) && valid->closes_at() == At(10) &&
         !equal && equal.error().code == ErrorCode::kInvalidArgument &&
         !reversed &&
         reversed.error().code == ErrorCode::kInvalidArgument;
}

auto TestCanonicalization() -> bool {
  const auto timing = CycleTiming::Create(
      PlanningCycleId{7}, SnapshotVersion{9}, At(0), At(100));
  if(!timing) {
    return false;
  }

  const std::vector<TxOpportunity> first_input{
      TxOpportunity{NodeId{9}, At(50)},
      TxOpportunity{NodeId{3}, At(20)},
      TxOpportunity{NodeId{0}, At(20)},
      TxOpportunity{NodeId{1}, At(80)}};
  const std::vector<TxOpportunity> second_input{
      first_input[2], first_input[3], first_input[0], first_input[1]};
  const auto first = ProtocolCyclePlan::Create(*timing, first_input);
  const auto second = ProtocolCyclePlan::Create(*timing, second_input);
  const auto standalone_mac_plan = MacPlan::Create(*timing, first_input);
  if(!first || !second || !standalone_mac_plan ||
     *first != *second ||
     *standalone_mac_plan != first->mac_plan()) {
    return false;
  }

  const auto opportunities = first->mac_plan().tx_opportunities();
  return !first->routing_plan() && !second->routing_plan() &&
         opportunities.size() == 4 &&
         opportunities[0] == TxOpportunity{NodeId{0}, At(20)} &&
         opportunities[1] == TxOpportunity{NodeId{3}, At(20)} &&
         opportunities[2] == TxOpportunity{NodeId{9}, At(50)} &&
         opportunities[3] == TxOpportunity{NodeId{1}, At(80)};
}

auto BuildValueOwnedRoutedPlan() -> Result<ProtocolCyclePlan> {
  auto routing = MakeRoutingPlan(PlanningCycleId{7}, SnapshotVersion{9});
  auto timing = CycleTiming::Create(
      PlanningCycleId{7}, SnapshotVersion{9}, At(0), At(100));
  if(!routing || !timing) {
    return std::unexpected(
        routing ? timing.error() : routing.error());
  }
  auto mac = MacPlan::Create(
      *timing, {TxOpportunity{NodeId{0}, At(20)}});
  if(!mac) {
    return std::unexpected(mac.error());
  }
  return ProtocolCyclePlan::Create(
      std::move(*routing), *timing, std::move(*mac));
}

auto TestRoutedPlanAndValueOwnership() -> bool {
  const auto plan = BuildValueOwnedRoutedPlan();
  if(!plan || !plan->routing_plan()) {
    return false;
  }
  const auto& routing = *plan->routing_plan();
  const auto opportunities = plan->mac_plan().tx_opportunities();
  return routing.cycle_id() == PlanningCycleId{7} &&
         routing.base_snapshot_version() == SnapshotVersion{9} &&
         routing.FindNextHop(NodeId{0}, NodeId{1}) == NodeId{1} &&
         opportunities.size() == 1 &&
         opportunities.front() ==
             TxOpportunity{NodeId{0}, At(20)};
}

auto TestRoutedPlanProvenanceValidation() -> bool {
  const auto timing = CycleTiming::Create(
      PlanningCycleId{7}, SnapshotVersion{9}, At(0), At(100));
  const auto mac = timing
                       ? MacPlan::Create(
                             *timing,
                             {TxOpportunity{NodeId{0}, At(20)}})
                       : Result<MacPlan>{
                             std::unexpected(timing.error())};
  const auto wrong_cycle =
      MakeRoutingPlan(PlanningCycleId{8}, SnapshotVersion{9});
  const auto wrong_version =
      MakeRoutingPlan(PlanningCycleId{7}, SnapshotVersion{10});
  if(!timing || !mac || !wrong_cycle || !wrong_version) {
    return false;
  }

  const auto cycle_mismatch = ProtocolCyclePlan::Create(
      *wrong_cycle, *timing, *mac);
  const auto version_mismatch = ProtocolCyclePlan::Create(
      *wrong_version, *timing, *mac);
  return !cycle_mismatch &&
         cycle_mismatch.error().code == ErrorCode::kFailedPrecondition &&
         !version_mismatch &&
         version_mismatch.error().code ==
             ErrorCode::kFailedPrecondition;
}

auto TestRoutedPlanRevalidatesMacAgainstTiming() -> bool {
  const auto routing =
      MakeRoutingPlan(PlanningCycleId{7}, SnapshotVersion{9});
  const auto wide_timing = CycleTiming::Create(
      PlanningCycleId{7}, SnapshotVersion{9}, At(0), At(100));
  const auto narrow_timing = CycleTiming::Create(
      PlanningCycleId{7}, SnapshotVersion{9}, At(0), At(50));
  const auto mac = wide_timing
                       ? MacPlan::Create(
                             *wide_timing,
                             {TxOpportunity{NodeId{0}, At(90)}})
                       : Result<MacPlan>{
                             std::unexpected(wide_timing.error())};
  if(!routing || !wide_timing || !narrow_timing || !mac) {
    return false;
  }

  const auto result = ProtocolCyclePlan::Create(
      *routing, *narrow_timing, *mac);
  return !result && result.error().code == ErrorCode::kOutOfRange;
}

auto TestOpportunityRangeAndDuplicateValidation() -> bool {
  const auto timing = CycleTiming::Create(
      PlanningCycleId{1}, SnapshotVersion{2}, At(10), At(20));
  if(!timing) {
    return false;
  }

  const auto at_start = ProtocolCyclePlan::Create(
      *timing, {TxOpportunity{NodeId{0}, At(10)}});
  const auto before = ProtocolCyclePlan::Create(
      *timing, {TxOpportunity{NodeId{0}, At(9)}});
  const auto at_close = ProtocolCyclePlan::Create(
      *timing, {TxOpportunity{NodeId{0}, At(20)}});
  const auto after = ProtocolCyclePlan::Create(
      *timing, {TxOpportunity{NodeId{0}, At(21)}});
  const auto duplicate = ProtocolCyclePlan::Create(
      *timing,
      {TxOpportunity{NodeId{0}, At(15)},
       TxOpportunity{NodeId{0}, At(15)}});
  const auto same_time_different_sender = ProtocolCyclePlan::Create(
      *timing,
      {TxOpportunity{NodeId{1}, At(15)},
       TxOpportunity{NodeId{0}, At(15)}});
  const auto empty = ProtocolCyclePlan::Create(*timing, {});

  return at_start && !before &&
         before.error().code == ErrorCode::kOutOfRange && !at_close &&
         at_close.error().code == ErrorCode::kOutOfRange && !after &&
         after.error().code == ErrorCode::kOutOfRange && !duplicate &&
         duplicate.error().code == ErrorCode::kAlreadyExists &&
         same_time_different_sender && empty;
}

}  // namespace

auto main() -> int {
  if(!TestTimingValidation() || !TestCanonicalization() ||
     !TestOpportunityRangeAndDuplicateValidation() ||
     !TestRoutedPlanAndValueOwnership() ||
     !TestRoutedPlanProvenanceValidation() ||
     !TestRoutedPlanRevalidatesMacAgainstTiming()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
