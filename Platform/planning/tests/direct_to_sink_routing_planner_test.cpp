#include <algorithm>
#include <concepts>
#include <cstdlib>
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

#include "internal/direct_to_sink_routing_planner.hpp"
#include "internal/routing_planner.hpp"

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
using ns3_factory::contracts::RouteEntry;
using ns3_factory::contracts::RoutingPlan;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::StructureSnapshot;
using ns3_factory::planning::internal::DirectToSinkRoutingPlanner;
using ns3_factory::planning::internal::IRoutingPlanner;

static_assert(std::derived_from<DirectToSinkRoutingPlanner,
                                IRoutingPlanner>);
static_assert(std::has_virtual_destructor_v<IRoutingPlanner>);

namespace {

auto NodeUniverse() -> std::vector<NodeId> {
  return {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}};
}

auto MakeStructure(std::vector<RoleBinding> bindings,
                   std::vector<DirectedLink> connectivity_edges,
                   std::vector<LogicalLink> logical_links)
    -> Result<StructureSnapshot> {
  auto connectivity = ConnectivityGraph::Create(
      std::move(connectivity_edges), NodeUniverse());
  if(!connectivity) {
    return std::unexpected(connectivity.error());
  }
  auto topology = LogicalTopology::Create(
      std::move(logical_links), NodeUniverse(), *connectivity);
  if(!topology) {
    return std::unexpected(topology.error());
  }
  auto roles = RoleTable::Create(std::move(bindings), NodeUniverse());
  if(!roles) {
    return std::unexpected(roles.error());
  }
  return StructureSnapshot::Create(PlanningCycleId{7},
                                   SnapshotVersion{4},
                                   std::move(*roles),
                                   std::move(*connectivity),
                                   std::move(*topology));
}

auto MakeBaselineStructure() -> Result<StructureSnapshot> {
  return MakeStructure(
      {RoleBinding{NodeId{0}, ProtocolRole::kMember},
       RoleBinding{NodeId{1}, ProtocolRole::kRelay},
       RoleBinding{NodeId{2}, ProtocolRole::kSink},
       RoleBinding{NodeId{2}, ProtocolRole::kController},
       RoleBinding{NodeId{3}, ProtocolRole::kAccessNode}},
      {DirectedLink{NodeId{0}, NodeId{2}},
       DirectedLink{NodeId{2}, NodeId{0}},
       DirectedLink{NodeId{1}, NodeId{2}},
       DirectedLink{NodeId{3}, NodeId{1}},
       DirectedLink{NodeId{2}, NodeId{3}},
       DirectedLink{NodeId{3}, NodeId{2}}},
      {LogicalLink{NodeId{0}, NodeId{2}},
       LogicalLink{NodeId{2}, NodeId{0}},
       LogicalLink{NodeId{1}, NodeId{2}},
       LogicalLink{NodeId{3}, NodeId{1}},
       LogicalLink{NodeId{2}, NodeId{3}}});
}

auto TestExactUplinkPlanAndDeterminism() -> bool {
  const auto structure = MakeBaselineStructure();
  if(!structure) {
    return false;
  }

  const DirectToSinkRoutingPlanner planner;
  const auto first = planner.Build(*structure);
  const auto second = planner.Build(*structure);
  const std::vector<RouteEntry> expected{
      RouteEntry{NodeId{0}, NodeId{2}, NodeId{2}},
      RouteEntry{NodeId{1}, NodeId{2}, NodeId{2}}};
  if(!first || !second || *first != *second ||
     first->entries().size() != expected.size() ||
     !std::equal(first->entries().begin(),
                 first->entries().end(),
                 expected.begin())) {
    return false;
  }

  return first->cycle_id() == structure->cycle_id() &&
         first->base_snapshot_version() ==
             structure->base_snapshot_version() &&
         first->FindNextHop(NodeId{0}, NodeId{2}) == NodeId{2} &&
         first->FindNextHop(NodeId{1}, NodeId{2}) == NodeId{2} &&
         !first->FindNextHop(NodeId{2}, NodeId{0}) &&
         !first->FindNextHop(NodeId{2}, NodeId{3}) &&
         !first->FindNextHop(NodeId{3}, NodeId{1}) &&
         !first->FindNextHop(NodeId{3}, NodeId{2});
}

auto TestSinkCardinalityAndRetry() -> bool {
  const auto no_sink = MakeStructure(
      {RoleBinding{NodeId{0}, ProtocolRole::kMember}}, {}, {});
  const auto two_sinks = MakeStructure(
      {RoleBinding{NodeId{1}, ProtocolRole::kSink},
       RoleBinding{NodeId{2}, ProtocolRole::kSink}},
      {},
      {});
  const auto valid = MakeBaselineStructure();
  if(!no_sink || !two_sinks || !valid) {
    return false;
  }

  const DirectToSinkRoutingPlanner planner;
  const auto no_sink_result = planner.Build(*no_sink);
  const auto two_sink_result = planner.Build(*two_sinks);
  const auto retried = planner.Build(*valid);
  return !no_sink_result &&
         no_sink_result.error().code == ErrorCode::kFailedPrecondition &&
         !two_sink_result &&
         two_sink_result.error().code == ErrorCode::kFailedPrecondition &&
         retried && retried->entries().size() == 2;
}

auto TestZeroUsableUplinkIsSuccessfulEmptyPlan() -> bool {
  const auto structure = MakeStructure(
      {RoleBinding{NodeId{2}, ProtocolRole::kSink}},
      {DirectedLink{NodeId{2}, NodeId{0}}},
      {LogicalLink{NodeId{2}, NodeId{0}}});
  if(!structure) {
    return false;
  }

  const DirectToSinkRoutingPlanner planner;
  const auto plan = planner.Build(*structure);
  return plan && plan->entries().empty() &&
         !plan->FindNextHop(NodeId{0}, NodeId{2});
}

}  // namespace

auto main() -> int {
  return TestExactUplinkPlanAndDeterminism() &&
                 TestSinkCardinalityAndRetry() &&
                 TestZeroUsableUplinkIsSuccessfulEmptyPlan()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
