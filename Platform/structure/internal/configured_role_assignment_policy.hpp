#pragma once

#include <utility>
#include <vector>

#include <ns3_factory/contracts/role.hpp>

#include "internal/role_assignment_policy.hpp"

namespace ns3_factory::structure::internal {

class ConfiguredRoleAssignmentPolicy final
    : public IRoleAssignmentPolicy {
 public:
  explicit ConfiguredRoleAssignmentPolicy(
      std::vector<contracts::RoleBinding> configured_bindings) noexcept
      : configured_bindings_(std::move(configured_bindings)) {}

  [[nodiscard]] auto Assign(
      const contracts::WorldSnapshot& snapshot,
      const contracts::ConnectivityGraph& connectivity_graph,
      contracts::PlanningCycleId cycle_id) const
      -> contracts::Result<contracts::RoleTable> override;

 private:
  std::vector<contracts::RoleBinding> configured_bindings_;
};

inline auto ConfiguredRoleAssignmentPolicy::Assign(
    const contracts::WorldSnapshot& snapshot,
    const contracts::ConnectivityGraph&,
    contracts::PlanningCycleId) const
    -> contracts::Result<contracts::RoleTable> {
  std::vector<contracts::NodeId> node_ids;
  node_ids.reserve(snapshot.nodes().size());
  for(const auto& node : snapshot.nodes()) {
    node_ids.push_back(node.node_id);
  }
  return contracts::RoleTable::Create(configured_bindings_,
                                      std::move(node_ids));
}

}  // namespace ns3_factory::structure::internal
