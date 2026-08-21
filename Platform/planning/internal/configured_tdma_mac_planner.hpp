#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

#include "internal/configured_tdma_policy.hpp"
#include "internal/mac_planner.hpp"
#include "internal/mac_planning_result.hpp"

namespace ns3_factory::planning::internal {

class ConfiguredTdmaMacPlanner final : public IMacPlanner {
 public:
  explicit ConfiguredTdmaMacPlanner(ConfiguredTdmaPolicy policy) noexcept
      : policy_(std::move(policy)) {}

  [[nodiscard]] auto Build(
      const contracts::WorldSnapshot& world_snapshot,
      const contracts::StructureSnapshot& structure_snapshot) const
      -> contracts::Result<MacPlanningResult> override;

 private:
  ConfiguredTdmaPolicy policy_;
};

inline auto ConfiguredTdmaMacPlanner::Build(
    const contracts::WorldSnapshot& world_snapshot,
    const contracts::StructureSnapshot& structure_snapshot) const
    -> contracts::Result<MacPlanningResult> {
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

  for(const auto owner_node_id : policy_.slot_owners()) {
    const auto world_node = world_snapshot.FindNode(owner_node_id);
    const auto structure_has_node = std::binary_search(
        structure_node_ids.begin(), structure_node_ids.end(), owner_node_id);
    if(!world_node || !structure_has_node) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kNotFound,
              "Configured TDMA slot owner is outside the node universe"});
    }
    if(!world_node->get().capability.can_transmit) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kFailedPrecondition,
              "Configured TDMA slot owner cannot transmit"});
    }
  }

  const auto starts_at = world_snapshot.committed_at();
  auto next_slot_start = starts_at;
  std::vector<contracts::TxOpportunity> opportunities;
  opportunities.reserve(policy_.slot_owners().size());
  for(const auto owner_node_id : policy_.slot_owners()) {
    opportunities.push_back(
        contracts::TxOpportunity{owner_node_id, next_slot_start});
    const auto next =
        contracts::CheckedAdd(next_slot_start, policy_.slot_duration());
    if(!next) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kOverflow,
              "Configured TDMA slot time exceeds SimTime range"});
    }
    next_slot_start = *next;
  }

  auto timing = contracts::CycleTiming::Create(
      structure_snapshot.cycle_id(),
      world_snapshot.version(),
      starts_at,
      next_slot_start);
  if(!timing) {
    return std::unexpected(timing.error());
  }

  // This plan defines deterministic slot-start opportunities only.
  // TxOpportunity has no slot-end deadline, so physical TxEmission duration
  // containment requires a separately reviewed future contract/runtime rule.
  return MacPlanningResult::Create(
      *timing, std::move(opportunities));
}

}  // namespace ns3_factory::planning::internal
