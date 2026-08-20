#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/link_feasibility.hpp>
#include <ns3_factory/contracts/state.hpp>

#include "internal/connectivity_decision_policy.hpp"

namespace ns3_factory::structure::internal {

class ConnectivityBuilder final {
 public:
  [[nodiscard]] static auto Build(
      const contracts::WorldSnapshot& snapshot,
      const ConnectivityDecisionPolicy& policy,
      const contracts::ILinkFeasibilityEstimator& estimator,
      std::optional<
          std::reference_wrapper<const contracts::ConnectivityGraph>>
          previous_graph = std::nullopt)
      -> contracts::Result<contracts::ConnectivityGraph>;
};

inline auto ConnectivityBuilder::Build(
    const contracts::WorldSnapshot& snapshot,
    const ConnectivityDecisionPolicy& policy,
    const contracts::ILinkFeasibilityEstimator& estimator,
    std::optional<std::reference_wrapper<
        const contracts::ConnectivityGraph>> previous_graph)
    -> contracts::Result<contracts::ConnectivityGraph> {
  const auto nodes = snapshot.nodes();
  std::vector<contracts::NodeId> node_ids;
  node_ids.reserve(nodes.size());
  for(const auto& node : nodes) {
    node_ids.push_back(node.node_id);
  }

  if(previous_graph) {
    const auto previous_node_ids = previous_graph->get().node_ids();
    if(previous_node_ids.size() != node_ids.size() ||
       !std::equal(node_ids.begin(),
                   node_ids.end(),
                   previous_node_ids.begin())) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kFailedPrecondition,
              "Previous ConnectivityGraph node universe does not match "
              "WorldSnapshot"});
    }
  }

  std::vector<contracts::DirectedLink> accepted_edges;
  for(const auto& source : nodes) {
    for(const auto& target : nodes) {
      if(source.node_id == target.node_id ||
         !source.capability.can_transmit ||
         !target.capability.can_receive) {
        continue;
      }

      auto query = contracts::LinkFeasibilityQuery::Create(
          source.node_id,
          target.node_id,
          source.motion.position,
          target.motion.position,
          snapshot.committed_at());
      if(!query) {
        return std::unexpected(query.error());
      }

      if(policy.max_coarse_range_m()) {
        const auto& source_position = query->source_position();
        const auto& target_position = query->target_position();
        const auto distance = std::hypot(
            target_position.x_meters - source_position.x_meters,
            target_position.y_meters - source_position.y_meters,
            target_position.z_meters - source_position.z_meters);
        if(distance > *policy.max_coarse_range_m()) {
          continue;
        }
      }

      const auto estimate = estimator.Estimate(*query);
      if(!estimate) {
        return std::unexpected(estimate.error());
      }
      const auto validation =
          contracts::ValidateLinkFeasibilityEstimate(*query, *estimate);
      if(!validation) {
        return std::unexpected(validation.error());
      }

      const bool existed =
          previous_graph &&
          previous_graph->get().HasEdge(source.node_id, target.node_id);
      const auto threshold = existed ? policy.keep_threshold()
                                     : policy.enter_threshold();
      if(estimate->feasibility_score() >= threshold) {
        accepted_edges.push_back(
            contracts::DirectedLink{source.node_id, target.node_id});
      }
    }
  }

  return contracts::ConnectivityGraph::Create(
      std::move(accepted_edges), std::move(node_ids));
}

}  // namespace ns3_factory::structure::internal
