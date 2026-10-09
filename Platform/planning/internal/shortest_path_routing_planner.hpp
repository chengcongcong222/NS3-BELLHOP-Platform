#pragma once

#include <algorithm>
#include <deque>
#include <limits>
#include <vector>

#include "internal/routing_planner.hpp"

namespace ns3_factory::planning::internal {

// Deterministic all-destination directed minimum-hop routing. Equal-hop paths
// select the lowest next-hop NodeId; role assignment does not select a target.
class ShortestPathRoutingPlanner final : public IRoutingPlanner {
 public:
  auto Build(const contracts::StructureSnapshot& structure) const
      -> contracts::Result<contracts::RoutingPlan> override {
    using namespace contracts;
    const auto& topology = structure.logical_topology();
    const auto nodes = topology.node_ids();
    const auto index_of = [&](NodeId id) {
      return static_cast<std::size_t>(std::lower_bound(nodes.begin(), nodes.end(), id) - nodes.begin());
    };
    std::vector<std::vector<std::size_t>> predecessors(nodes.size());
    std::vector<std::vector<std::size_t>> successors(nodes.size());
    for(const auto& link : topology.links()) {
      const auto source = index_of(link.source_node_id);
      const auto target = index_of(link.target_node_id);
      predecessors[target].push_back(source);
      successors[source].push_back(target);
    }
    constexpr auto unreachable = std::numeric_limits<std::size_t>::max();
    std::vector<RouteEntry> entries;
    for(std::size_t destination = 0; destination < nodes.size(); ++destination) {
      std::vector<std::size_t> distance(nodes.size(), unreachable);
      std::deque<std::size_t> frontier{destination};
      distance[destination] = 0;
      while(!frontier.empty()) {
        const auto current = frontier.front();
        frontier.pop_front();
        for(const auto predecessor : predecessors[current]) {
          if(distance[predecessor] != unreachable) continue;
          distance[predecessor] = distance[current] + 1;
          frontier.push_back(predecessor);
        }
      }
      for(std::size_t source = 0; source < nodes.size(); ++source) {
        if(source == destination || distance[source] == unreachable) continue;
        auto selected = unreachable;
        for(const auto target : successors[source]) {
          if(distance[target] != unreachable && distance[target] + 1 == distance[source] &&
             (selected == unreachable || nodes[target] < nodes[selected])) selected = target;
        }
        if(selected == unreachable) return std::unexpected(Error{ErrorCode::kFailedPrecondition,
            "Reachable destination has no minimum-hop next hop"});
        entries.push_back({nodes[source], nodes[destination], nodes[selected]});
      }
    }
    return RoutingPlan::Create(std::move(entries), structure);
  }
};

}  // namespace ns3_factory::planning::internal
