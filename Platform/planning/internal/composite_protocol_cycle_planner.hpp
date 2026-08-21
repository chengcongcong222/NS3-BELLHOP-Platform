#pragma once

#include <algorithm>
#include <functional>
#include <utility>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/structure.hpp>

#include "internal/mac_planner.hpp"
#include "internal/protocol_cycle_planner.hpp"
#include "internal/routing_planner.hpp"

namespace ns3_factory::planning::internal {

class CompositeProtocolCyclePlanner final : public IProtocolCyclePlanner {
 public:
  // The caller-owned planners must outlive this synchronous orchestrator.
  CompositeProtocolCyclePlanner(const IRoutingPlanner& routing_planner,
                                const IMacPlanner& mac_planner) noexcept
      : routing_planner_(routing_planner), mac_planner_(mac_planner) {}

  [[nodiscard]] auto Build(
      const contracts::WorldSnapshot& world_snapshot,
      const contracts::StructureSnapshot& structure_snapshot) const
      -> contracts::Result<contracts::ProtocolCyclePlan> override;

 private:
  std::reference_wrapper<const IRoutingPlanner> routing_planner_;
  std::reference_wrapper<const IMacPlanner> mac_planner_;
};

inline auto CompositeProtocolCyclePlanner::Build(
    const contracts::WorldSnapshot& world_snapshot,
    const contracts::StructureSnapshot& structure_snapshot) const
    -> contracts::Result<contracts::ProtocolCyclePlan> {
  if(structure_snapshot.base_snapshot_version() !=
     world_snapshot.version()) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "StructureSnapshot base version does not match WorldSnapshot"});
  }

  const auto world_nodes = world_snapshot.nodes();
  const auto structure_node_ids =
      structure_snapshot.connectivity_graph().node_ids();
  const auto same_universe =
      world_nodes.size() == structure_node_ids.size() &&
      std::equal(world_nodes.begin(),
                 world_nodes.end(),
                 structure_node_ids.begin(),
                 [](const contracts::NodeCommittedState& world_node,
                    contracts::NodeId structure_node_id) {
                   return world_node.node_id == structure_node_id;
                 });
  if(!same_universe) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "StructureSnapshot and WorldSnapshot node universes differ"});
  }

  auto routing_plan = routing_planner_.get().Build(structure_snapshot);
  if(!routing_plan) {
    return std::unexpected(routing_plan.error());
  }
  if(routing_plan->cycle_id() != structure_snapshot.cycle_id() ||
     routing_plan->base_snapshot_version() !=
         structure_snapshot.base_snapshot_version()) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Routing planner output provenance does not match structure"});
  }

  auto mac_result =
      mac_planner_.get().Build(world_snapshot, structure_snapshot);
  if(!mac_result) {
    return std::unexpected(mac_result.error());
  }
  const auto& timing = mac_result->timing();
  if(timing.cycle_id() != structure_snapshot.cycle_id() ||
     timing.base_snapshot_version() !=
         structure_snapshot.base_snapshot_version() ||
     timing.base_snapshot_version() != world_snapshot.version()) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "MAC planner output provenance does not match planning input"});
  }

  return contracts::ProtocolCyclePlan::Create(
      std::move(*routing_plan), timing, mac_result->mac_plan());
}

}  // namespace ns3_factory::planning::internal
