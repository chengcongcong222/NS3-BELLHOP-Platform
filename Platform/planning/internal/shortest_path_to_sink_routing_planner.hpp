#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/routing.hpp>
#include <ns3_factory/contracts/structure.hpp>

#include "internal/routing_planner.hpp"

namespace ns3_factory::planning::internal {

class ShortestPathToSinkRoutingPlanner final : public IRoutingPlanner {
 public:
  [[nodiscard]] auto Build(
      const contracts::StructureSnapshot& structure_snapshot) const
      -> contracts::Result<contracts::RoutingPlan> override;
};

inline auto ShortestPathToSinkRoutingPlanner::Build(
    const contracts::StructureSnapshot& structure_snapshot) const
    -> contracts::Result<contracts::RoutingPlan> {
  const auto sink_nodes = structure_snapshot.role_table().NodesWithRole(
      contracts::ProtocolRole::kSink);
  if(sink_nodes.size() != 1) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "ShortestPathToSinkRoutingPlanner requires exactly one sink"});
  }

  const auto& topology = structure_snapshot.logical_topology();
  const auto node_ids = topology.node_ids();
  const auto index_of = [&](contracts::NodeId node_id) {
    return static_cast<std::size_t>(
        std::lower_bound(node_ids.begin(), node_ids.end(), node_id) -
        node_ids.begin());
  };

  std::vector<std::vector<std::size_t>> predecessors(node_ids.size());
  std::vector<std::vector<std::size_t>> successors(node_ids.size());
  for(const auto& link : topology.links()) {
    const auto source_index = index_of(link.source_node_id);
    const auto target_index = index_of(link.target_node_id);
    predecessors[target_index].push_back(source_index);
    successors[source_index].push_back(target_index);
  }
  for(auto& adjacent : predecessors) {
    std::sort(adjacent.begin(), adjacent.end());
  }
  for(auto& adjacent : successors) {
    std::sort(adjacent.begin(), adjacent.end());
  }

  constexpr auto kUnreachable = std::numeric_limits<std::size_t>::max();
  std::vector<std::size_t> distance_to_sink(node_ids.size(), kUnreachable);
  const auto sink_node_id = sink_nodes.front();
  const auto sink_index = index_of(sink_node_id);
  distance_to_sink[sink_index] = 0;

  std::deque<std::size_t> frontier{sink_index};
  while(!frontier.empty()) {
    const auto current = frontier.front();
    frontier.pop_front();
    for(const auto predecessor : predecessors[current]) {
      if(distance_to_sink[predecessor] != kUnreachable) {
        continue;
      }
      distance_to_sink[predecessor] = distance_to_sink[current] + 1;
      frontier.push_back(predecessor);
    }
  }

  std::vector<contracts::RouteEntry> entries;
  entries.reserve(node_ids.size());
  for(std::size_t source = 0; source < node_ids.size(); ++source) {
    if(source == sink_index || distance_to_sink[source] == kUnreachable) {
      continue;
    }

    auto selected = kUnreachable;
    for(const auto target : successors[source]) {
      if(distance_to_sink[target] != kUnreachable &&
         distance_to_sink[target] + 1 == distance_to_sink[source] &&
         (selected == kUnreachable || node_ids[target] < node_ids[selected])) {
        selected = target;
      }
    }
    if(selected == kUnreachable) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kFailedPrecondition,
              "Reachable node has no shortest-path next hop"});
    }
    entries.push_back(contracts::RouteEntry{
        node_ids[source], sink_node_id, node_ids[selected]});
  }

  return contracts::RoutingPlan::Create(
      std::move(entries), structure_snapshot);
}

}  // namespace ns3_factory::planning::internal
