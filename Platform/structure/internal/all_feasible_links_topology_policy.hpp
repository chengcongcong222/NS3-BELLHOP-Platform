#pragma once

#include <utility>
#include <vector>

#include <ns3_factory/contracts/topology.hpp>

#include "internal/logical_topology_policy.hpp"

namespace ns3_factory::structure::internal {

class AllFeasibleLinksTopologyPolicy final
    : public ILogicalTopologyPolicy {
 public:
  [[nodiscard]] auto Build(
      const contracts::WorldSnapshot& snapshot,
      const contracts::RoleTable& role_table,
      const contracts::ConnectivityGraph& connectivity_graph) const
      -> contracts::Result<contracts::LogicalTopology> override;
};

inline auto AllFeasibleLinksTopologyPolicy::Build(
    const contracts::WorldSnapshot& snapshot,
    const contracts::RoleTable&,
    const contracts::ConnectivityGraph& connectivity_graph) const
    -> contracts::Result<contracts::LogicalTopology> {
  std::vector<contracts::NodeId> node_ids;
  node_ids.reserve(snapshot.nodes().size());
  for(const auto& node : snapshot.nodes()) {
    node_ids.push_back(node.node_id);
  }

  std::vector<contracts::LogicalLink> links;
  links.reserve(connectivity_graph.edges().size());
  for(const auto& edge : connectivity_graph.edges()) {
    links.push_back(contracts::LogicalLink{edge.source_node_id,
                                           edge.target_node_id});
  }
  return contracts::LogicalTopology::Create(
      std::move(links), std::move(node_ids), connectivity_graph);
}

}  // namespace ns3_factory::structure::internal
