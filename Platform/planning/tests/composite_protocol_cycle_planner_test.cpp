#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <functional>
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
#include "internal/direct_to_sink_routing_planner.hpp"
#include "internal/mac_planner.hpp"
#include "internal/mac_planning_result.hpp"
#include "internal/protocol_cycle_planner.hpp"
#include "internal/routing_planner.hpp"

using ns3_factory::contracts::ConnectivityGraph;
using ns3_factory::contracts::DirectedLink;
using ns3_factory::contracts::DuplexMode;
using ns3_factory::contracts::Error;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::LogicalLink;
using ns3_factory::contracts::LogicalTopology;
using ns3_factory::contracts::MotionState;
using ns3_factory::contracts::NodeCapabilityProfile;
using ns3_factory::contracts::NodeCommittedState;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::ProtocolCyclePlan;
using ns3_factory::contracts::ProtocolRole;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::RoleBinding;
using ns3_factory::contracts::RoleTable;
using ns3_factory::contracts::RoutingPlan;
using ns3_factory::contracts::SimDuration;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::StructureSnapshot;
using ns3_factory::contracts::TxOpportunity;
using ns3_factory::contracts::Velocity3d;
using ns3_factory::contracts::WorldSnapshot;
using ns3_factory::planning::internal::CompositeProtocolCyclePlanner;
using ns3_factory::planning::internal::ConfiguredTdmaMacPlanner;
using ns3_factory::planning::internal::ConfiguredTdmaPolicy;
using ns3_factory::planning::internal::DirectToSinkRoutingPlanner;
using ns3_factory::planning::internal::IMacPlanner;
using ns3_factory::planning::internal::IProtocolCyclePlanner;
using ns3_factory::planning::internal::IRoutingPlanner;
using ns3_factory::planning::internal::MacPlanningResult;

static_assert(std::derived_from<CompositeProtocolCyclePlanner,
                                IProtocolCyclePlanner>);
static_assert(std::has_virtual_destructor_v<IProtocolCyclePlanner>);

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

constexpr auto For(std::int64_t nanoseconds) -> SimDuration {
  return SimDuration::FromNanoseconds(nanoseconds);
}

constexpr auto MakeNode(std::uint64_t id) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, 0.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto MakeWorld(std::vector<NodeId> node_ids,
               SnapshotVersion version = SnapshotVersion{5},
               SimTime committed_at = At(100)) -> Result<WorldSnapshot> {
  std::vector<NodeCommittedState> nodes;
  nodes.reserve(node_ids.size());
  for(const auto node_id : node_ids) {
    nodes.push_back(MakeNode(node_id.value()));
  }
  return WorldSnapshot::Create(version, committed_at, std::move(nodes));
}

auto MakeStructure(
    std::vector<NodeId> node_ids,
    PlanningCycleId cycle_id = PlanningCycleId{7},
    SnapshotVersion base_version = SnapshotVersion{5})
    -> Result<StructureSnapshot> {
  const std::vector<DirectedLink> directed_links{
      DirectedLink{NodeId{0}, NodeId{2}},
      DirectedLink{NodeId{1}, NodeId{2}}};
  auto connectivity =
      ConnectivityGraph::Create(directed_links, node_ids);
  if(!connectivity) {
    return std::unexpected(connectivity.error());
  }
  auto topology = LogicalTopology::Create(
      {LogicalLink{NodeId{0}, NodeId{2}},
       LogicalLink{NodeId{1}, NodeId{2}}},
      node_ids,
      *connectivity);
  if(!topology) {
    return std::unexpected(topology.error());
  }
  auto roles = RoleTable::Create(
      {RoleBinding{NodeId{2}, ProtocolRole::kSink}}, node_ids);
  if(!roles) {
    return std::unexpected(roles.error());
  }
  return StructureSnapshot::Create(cycle_id,
                                   base_version,
                                   std::move(*roles),
                                   std::move(*connectivity),
                                   std::move(*topology));
}

auto MakeMacResult(PlanningCycleId cycle_id = PlanningCycleId{7},
                   SnapshotVersion base_version = SnapshotVersion{5},
                   SimTime starts_at = At(100),
                   SimTime closes_at = At(130))
    -> Result<MacPlanningResult> {
  auto timing = ns3_factory::contracts::CycleTiming::Create(
      cycle_id, base_version, starts_at, closes_at);
  if(!timing) {
    return std::unexpected(timing.error());
  }
  return MacPlanningResult::Create(
      *timing,
      {TxOpportunity{NodeId{1}, starts_at},
       TxOpportunity{NodeId{0}, At(starts_at.nanoseconds() + 10)},
       TxOpportunity{NodeId{3}, At(starts_at.nanoseconds() + 20)}});
}

class StubRoutingPlanner final : public IRoutingPlanner {
 public:
  StubRoutingPlanner(Result<RoutingPlan> result,
                     std::vector<int>& call_order) noexcept
      : result_(std::move(result)), call_order_(call_order) {}

  [[nodiscard]] auto Build(const StructureSnapshot&) const
      -> Result<RoutingPlan> override {
    ++call_count_;
    call_order_.get().push_back(1);
    return result_;
  }

  [[nodiscard]] auto call_count() const noexcept -> std::size_t {
    return call_count_;
  }

 private:
  Result<RoutingPlan> result_;
  std::reference_wrapper<std::vector<int>> call_order_;
  mutable std::size_t call_count_{0};
};

class FailOnceRoutingPlanner final : public IRoutingPlanner {
 public:
  FailOnceRoutingPlanner(RoutingPlan valid_plan,
                         std::vector<int>& call_order) noexcept
      : valid_plan_(std::move(valid_plan)), call_order_(call_order) {}

  [[nodiscard]] auto Build(const StructureSnapshot&) const
      -> Result<RoutingPlan> override {
    ++call_count_;
    call_order_.get().push_back(1);
    if(call_count_ == 1) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable, "transient routing failure"});
    }
    return valid_plan_;
  }

  [[nodiscard]] auto call_count() const noexcept -> std::size_t {
    return call_count_;
  }

 private:
  RoutingPlan valid_plan_;
  std::reference_wrapper<std::vector<int>> call_order_;
  mutable std::size_t call_count_{0};
};

class StubMacPlanner final : public IMacPlanner {
 public:
  StubMacPlanner(Result<MacPlanningResult> result,
                 std::vector<int>& call_order) noexcept
      : result_(std::move(result)), call_order_(call_order) {}

  [[nodiscard]] auto Build(const WorldSnapshot&,
                           const StructureSnapshot&) const
      -> Result<MacPlanningResult> override {
    ++call_count_;
    call_order_.get().push_back(2);
    return result_;
  }

  [[nodiscard]] auto call_count() const noexcept -> std::size_t {
    return call_count_;
  }

 private:
  Result<MacPlanningResult> result_;
  std::reference_wrapper<std::vector<int>> call_order_;
  mutable std::size_t call_count_{0};
};

auto MakeValidRouting(const StructureSnapshot& structure)
    -> Result<RoutingPlan> {
  return DirectToSinkRoutingPlanner{}.Build(structure);
}

auto TestActualCompositeIntegrationAndDeterminism() -> bool {
  const auto world = MakeWorld(
      {NodeId{3}, NodeId{0}, NodeId{2}, NodeId{1}});
  const auto structure = MakeStructure(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  const auto policy = ConfiguredTdmaPolicy::Create(
      For(10), {NodeId{1}, NodeId{0}, NodeId{3}});
  if(!world || !structure || !policy) {
    return false;
  }

  const DirectToSinkRoutingPlanner routing_planner;
  const ConfiguredTdmaMacPlanner mac_planner{*policy};
  const CompositeProtocolCyclePlanner planner{routing_planner,
                                               mac_planner};
  const auto first = planner.Build(*world, *structure);
  const auto second = planner.Build(*world, *structure);
  if(!first || !second || *first != *second ||
     !first->routing_plan()) {
    return false;
  }

  const auto& routing = *first->routing_plan();
  const auto opportunities = first->mac_plan().tx_opportunities();
  return first->timing().cycle_id() == PlanningCycleId{7} &&
         first->timing().base_snapshot_version() == SnapshotVersion{5} &&
         routing.cycle_id() == PlanningCycleId{7} &&
         routing.base_snapshot_version() == SnapshotVersion{5} &&
         routing.entries().size() == 2 &&
         routing.FindNextHop(NodeId{0}, NodeId{2}) == NodeId{2} &&
         routing.FindNextHop(NodeId{1}, NodeId{2}) == NodeId{2} &&
         !routing.FindNextHop(NodeId{3}, NodeId{2}) &&
         opportunities.size() == 3 &&
         opportunities[0] == TxOpportunity{NodeId{1}, At(100)} &&
         opportunities[1] == TxOpportunity{NodeId{0}, At(110)} &&
         opportunities[2] == TxOpportunity{NodeId{3}, At(120)};
}

auto TestFixedOrderAndChildFailureAtomicity() -> bool {
  const auto world = MakeWorld(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  const auto structure = MakeStructure(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  const auto routing = structure ? MakeValidRouting(*structure)
                                 : Result<RoutingPlan>{
                                       std::unexpected(structure.error())};
  const auto mac = MakeMacResult();
  if(!world || !structure || !routing || !mac) {
    return false;
  }

  std::vector<int> success_order;
  const StubRoutingPlanner successful_routing{routing, success_order};
  const StubMacPlanner successful_mac{mac, success_order};
  const auto success = CompositeProtocolCyclePlanner{
                           successful_routing, successful_mac}
                           .Build(*world, *structure);

  std::vector<int> routing_failure_order;
  const StubRoutingPlanner failed_routing{
      std::unexpected(Error{ErrorCode::kNotFound, "no route"}),
      routing_failure_order};
  const StubMacPlanner skipped_mac{mac, routing_failure_order};
  const auto routing_failure = CompositeProtocolCyclePlanner{
                                   failed_routing, skipped_mac}
                                   .Build(*world, *structure);

  std::vector<int> mac_failure_order;
  const StubRoutingPlanner routing_before_mac{routing, mac_failure_order};
  const StubMacPlanner failed_mac{
      std::unexpected(Error{ErrorCode::kOverflow, "MAC overflow"}),
      mac_failure_order};
  const auto mac_failure = CompositeProtocolCyclePlanner{
                               routing_before_mac, failed_mac}
                               .Build(*world, *structure);

  return success && success_order == std::vector<int>{1, 2} &&
         successful_routing.call_count() == 1 &&
         successful_mac.call_count() == 1 && !routing_failure &&
         routing_failure.error().code == ErrorCode::kNotFound &&
         routing_failure_order == std::vector<int>{1} &&
         failed_routing.call_count() == 1 && skipped_mac.call_count() == 0 &&
         !mac_failure && mac_failure.error().code == ErrorCode::kOverflow &&
         mac_failure_order == std::vector<int>({1, 2}) &&
         routing_before_mac.call_count() == 1 && failed_mac.call_count() == 1;
}

auto TestPreflightRejectsBeforeChildCalls() -> bool {
  const auto version_mismatch_world = MakeWorld(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}}, SnapshotVersion{6});
  const auto universe_mismatch_world =
      MakeWorld({NodeId{0}, NodeId{1}, NodeId{2}});
  const auto structure = MakeStructure(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  const auto routing = structure ? MakeValidRouting(*structure)
                                 : Result<RoutingPlan>{
                                       std::unexpected(structure.error())};
  const auto mac = MakeMacResult();
  if(!version_mismatch_world || !universe_mismatch_world || !structure ||
     !routing || !mac) {
    return false;
  }

  std::vector<int> version_order;
  const StubRoutingPlanner version_routing{routing, version_order};
  const StubMacPlanner version_mac{mac, version_order};
  const auto version_result = CompositeProtocolCyclePlanner{
                                  version_routing, version_mac}
                                  .Build(*version_mismatch_world, *structure);

  std::vector<int> universe_order;
  const StubRoutingPlanner universe_routing{routing, universe_order};
  const StubMacPlanner universe_mac{mac, universe_order};
  const auto universe_result = CompositeProtocolCyclePlanner{
                                   universe_routing, universe_mac}
                                   .Build(*universe_mismatch_world, *structure);

  return !version_result &&
         version_result.error().code == ErrorCode::kFailedPrecondition &&
         version_order.empty() && version_routing.call_count() == 0 &&
         version_mac.call_count() == 0 && !universe_result &&
         universe_result.error().code == ErrorCode::kFailedPrecondition &&
         universe_order.empty() && universe_routing.call_count() == 0 &&
         universe_mac.call_count() == 0;
}

auto TestChildProvenanceValidation() -> bool {
  const auto world = MakeWorld(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  const auto structure = MakeStructure(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  const auto wrong_cycle_structure = MakeStructure(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}},
      PlanningCycleId{8},
      SnapshotVersion{5});
  const auto valid_routing = structure ? MakeValidRouting(*structure)
                                       : Result<RoutingPlan>{
                                             std::unexpected(
                                                 structure.error())};
  const auto wrong_routing =
      wrong_cycle_structure
          ? MakeValidRouting(*wrong_cycle_structure)
          : Result<RoutingPlan>{
                std::unexpected(wrong_cycle_structure.error())};
  const auto valid_mac = MakeMacResult();
  const auto wrong_mac = MakeMacResult(
      PlanningCycleId{7}, SnapshotVersion{6}, At(200), At(230));
  if(!world || !structure || !wrong_cycle_structure || !valid_routing ||
     !wrong_routing || !valid_mac || !wrong_mac) {
    return false;
  }

  std::vector<int> routing_order;
  const StubRoutingPlanner bad_routing{wrong_routing, routing_order};
  const StubMacPlanner skipped_mac{valid_mac, routing_order};
  const auto bad_routing_result = CompositeProtocolCyclePlanner{
                                      bad_routing, skipped_mac}
                                      .Build(*world, *structure);

  std::vector<int> mac_order;
  const StubRoutingPlanner good_routing{valid_routing, mac_order};
  const StubMacPlanner bad_mac{wrong_mac, mac_order};
  const auto bad_mac_result = CompositeProtocolCyclePlanner{
                                  good_routing, bad_mac}
                                  .Build(*world, *structure);

  return !bad_routing_result &&
         bad_routing_result.error().code ==
             ErrorCode::kFailedPrecondition &&
         routing_order == std::vector<int>{1} &&
         bad_routing.call_count() == 1 && skipped_mac.call_count() == 0 &&
         !bad_mac_result &&
         bad_mac_result.error().code == ErrorCode::kFailedPrecondition &&
         mac_order == std::vector<int>({1, 2}) &&
         good_routing.call_count() == 1 && bad_mac.call_count() == 1;
}

auto TestGeneralTimingAndRetry() -> bool {
  const auto world = MakeWorld(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}},
      SnapshotVersion{5},
      At(100));
  const auto structure = MakeStructure(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  const auto routing = structure ? MakeValidRouting(*structure)
                                 : Result<RoutingPlan>{
                                       std::unexpected(structure.error())};
  const auto later_mac = MakeMacResult(
      PlanningCycleId{7}, SnapshotVersion{5}, At(500), At(530));
  if(!world || !structure || !routing || !later_mac) {
    return false;
  }

  std::vector<int> order;
  const FailOnceRoutingPlanner fail_once{*routing, order};
  const StubMacPlanner mac{later_mac, order};
  const CompositeProtocolCyclePlanner planner{fail_once, mac};
  const auto first = planner.Build(*world, *structure);
  const auto retried = planner.Build(*world, *structure);
  return !first && first.error().code == ErrorCode::kUnavailable &&
         retried && retried->routing_plan() &&
         retried->timing().starts_at() == At(500) &&
         fail_once.call_count() == 2 && mac.call_count() == 1 &&
         order == std::vector<int>({1, 1, 2});
}

}  // namespace

auto main() -> int {
  return TestActualCompositeIntegrationAndDeterminism() &&
                 TestFixedOrderAndChildFailureAtomicity() &&
                 TestPreflightRejectsBeforeChildCalls() &&
                 TestChildProvenanceValidation() &&
                 TestGeneralTimingAndRetry()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
