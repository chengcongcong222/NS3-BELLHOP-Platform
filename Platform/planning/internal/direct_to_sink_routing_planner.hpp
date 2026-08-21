#pragma once

#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/routing.hpp>
#include <ns3_factory/contracts/structure.hpp>

#include "internal/routing_planner.hpp"

namespace ns3_factory::planning::internal {

class DirectToSinkRoutingPlanner final : public IRoutingPlanner {
 public:
  [[nodiscard]] auto Build(
      const contracts::StructureSnapshot& structure_snapshot) const
      -> contracts::Result<contracts::RoutingPlan> override;
};

inline auto DirectToSinkRoutingPlanner::Build(
    const contracts::StructureSnapshot& structure_snapshot) const
    -> contracts::Result<contracts::RoutingPlan> {
  const auto sink_nodes = structure_snapshot.role_table().NodesWithRole(
      contracts::ProtocolRole::kSink);
  if(sink_nodes.size() != 1) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "DirectToSinkRoutingPlanner requires exactly one sink"});
  }
  const auto sink_node_id = sink_nodes.front();

  std::vector<contracts::RouteEntry> entries;
  for(const auto& link : structure_snapshot.logical_topology().links()) {
    if(link.source_node_id != sink_node_id &&
       link.target_node_id == sink_node_id) {
      entries.push_back(contracts::RouteEntry{link.source_node_id,
                                               sink_node_id,
                                               sink_node_id});
    }
  }
  return contracts::RoutingPlan::Create(
      std::move(entries), structure_snapshot);
}

}  // namespace ns3_factory::planning::internal
