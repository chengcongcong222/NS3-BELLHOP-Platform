#pragma once

#include <vector>

#include "internal/routing_planner.hpp"

namespace ns3_factory::planning::internal {

// Every selected directed logical edge supplies its direct destination route.
// No protocol role, unique sink or reverse edge is inferred.
class DirectRoutingPlanner final : public IRoutingPlanner {
 public:
  auto Build(const contracts::StructureSnapshot& structure) const
      -> contracts::Result<contracts::RoutingPlan> override {
    std::vector<contracts::RouteEntry> entries;
    for(const auto& link : structure.logical_topology().links()) {
      entries.push_back({link.source_node_id, link.target_node_id,
                         link.target_node_id});
    }
    return contracts::RoutingPlan::Create(std::move(entries), structure);
  }
};

}  // namespace ns3_factory::planning::internal
