#include <cstdlib>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/routing.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/topology.hpp>

#include "internal/applied_mac_schedule.hpp"

namespace {

using namespace ns3_factory::assembly::internal;
using namespace ns3_factory::contracts;

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

auto MakeStructure(PlanningCycleId cycle_id,
                   SnapshotVersion base_version)
    -> Result<StructureSnapshot> {
  const std::vector<NodeId> node_ids{NodeId{10}, NodeId{20}, NodeId{99}};
  auto connectivity = ConnectivityGraph::Create(
      {DirectedLink{NodeId{10}, NodeId{99}},
       DirectedLink{NodeId{20}, NodeId{99}}},
      node_ids);
  if(!connectivity) return std::unexpected(connectivity.error());
  auto topology = LogicalTopology::Create(
      {LogicalLink{NodeId{10}, NodeId{99}},
       LogicalLink{NodeId{20}, NodeId{99}}},
      node_ids,
      *connectivity);
  if(!topology) return std::unexpected(topology.error());
  auto roles = RoleTable::Create(
      {RoleBinding{NodeId{10}, ProtocolRole::kMember},
       RoleBinding{NodeId{20}, ProtocolRole::kMember},
       RoleBinding{NodeId{99}, ProtocolRole::kSink}},
      node_ids);
  if(!roles) return std::unexpected(roles.error());
  return StructureSnapshot::Create(cycle_id,
                                   base_version,
                                   std::move(*roles),
                                   std::move(*connectivity),
                                   std::move(*topology));
}

auto MakeAppliedPlan(const StructureSnapshot& structure)
    -> Result<ProtocolCyclePlan> {
  auto timing = CycleTiming::Create(
      structure.cycle_id(), structure.base_snapshot_version(), At(100), At(112));
  if(!timing) return std::unexpected(timing.error());
  auto mac = MacPlan::Create(
      *timing,
      {TxOpportunity{NodeId{10}, At(100)},
       TxOpportunity{NodeId{20}, At(104)}});
  if(!mac) return std::unexpected(mac.error());
  auto routing = RoutingPlan::Create(
      {RouteEntry{NodeId{10}, NodeId{99}, NodeId{99}},
       RouteEntry{NodeId{20}, NodeId{99}, NodeId{99}}},
      structure);
  if(!routing) return std::unexpected(routing.error());
  return ProtocolCyclePlan::Create(
      std::move(*routing), *timing, std::move(*mac));
}

auto TestCurrentProvenanceAndNoCandidateLeakage() -> bool {
  const auto original_structure =
      MakeStructure(PlanningCycleId{1}, SnapshotVersion{4});
  const auto current_structure =
      MakeStructure(PlanningCycleId{8}, SnapshotVersion{11});
  if(!original_structure || !current_structure) return false;
  const auto original_plan = MakeAppliedPlan(*original_structure);
  if(!original_plan) return false;
  const auto applied = AppliedMacSchedule::Capture(*original_plan);
  if(!applied) return false;

  const auto rebound = applied->Bind(*current_structure, At(1'000));
  if(!rebound || !rebound->routing_plan()) return false;
  const auto opportunities = rebound->mac_plan().tx_opportunities();
  return rebound->timing().cycle_id() == PlanningCycleId{8} &&
         rebound->timing().base_snapshot_version() == SnapshotVersion{11} &&
         rebound->timing().starts_at() == At(1'000) &&
         rebound->timing().closes_at() == At(1'012) &&
         opportunities.size() == 2 &&
         opportunities[0] == TxOpportunity{NodeId{10}, At(1'000)} &&
         opportunities[1] == TxOpportunity{NodeId{20}, At(1'004)} &&
         rebound->routing_plan()->cycle_id() == PlanningCycleId{8} &&
         rebound->routing_plan()->base_snapshot_version() ==
             SnapshotVersion{11} &&
         rebound->routing_plan()->FindNextHop(NodeId{10}, NodeId{99}) ==
             NodeId{99} &&
         rebound->routing_plan()->FindNextHop(NodeId{20}, NodeId{99}) ==
             NodeId{99};
}

}  // namespace

auto main() -> int {
  return TestCurrentProvenanceAndNoCandidateLeakage() ? EXIT_SUCCESS
                                                       : EXIT_FAILURE;
}
