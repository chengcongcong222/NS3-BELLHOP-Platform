#pragma once

#include <functional>
#include <optional>
#include <utility>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/link_feasibility.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/structure.hpp>

#include "internal/connectivity_builder.hpp"
#include "internal/connectivity_decision_policy.hpp"
#include "internal/logical_topology_policy.hpp"
#include "internal/role_assignment_policy.hpp"

namespace ns3_factory::structure::internal {

struct StructureBuildRequest final {
  contracts::PlanningCycleId cycle_id;
  const contracts::WorldSnapshot& snapshot;
  std::optional<
      std::reference_wrapper<const contracts::ConnectivityGraph>>
      previous_connectivity = std::nullopt;
};

class StructureBuilder final {
 public:
  constexpr StructureBuilder(
      const ConnectivityDecisionPolicy& connectivity_policy,
      const contracts::ILinkFeasibilityEstimator& estimator,
      const IRoleAssignmentPolicy& role_assignment_policy,
      const ILogicalTopologyPolicy& logical_topology_policy) noexcept
      : connectivity_policy_(connectivity_policy),
        estimator_(estimator),
        role_assignment_policy_(role_assignment_policy),
        logical_topology_policy_(logical_topology_policy) {}

  [[nodiscard]] auto Build(const StructureBuildRequest& request) const
      -> contracts::Result<contracts::StructureSnapshot>;

 private:
  const ConnectivityDecisionPolicy& connectivity_policy_;
  const contracts::ILinkFeasibilityEstimator& estimator_;
  const IRoleAssignmentPolicy& role_assignment_policy_;
  const ILogicalTopologyPolicy& logical_topology_policy_;
};

inline auto StructureBuilder::Build(
    const StructureBuildRequest& request) const
    -> contracts::Result<contracts::StructureSnapshot> {
  auto connectivity_graph = ConnectivityBuilder::Build(
      request.snapshot,
      connectivity_policy_,
      estimator_,
      request.previous_connectivity);
  if(!connectivity_graph) {
    return std::unexpected(connectivity_graph.error());
  }

  auto role_table = role_assignment_policy_.Assign(
      request.snapshot, *connectivity_graph, request.cycle_id);
  if(!role_table) {
    return std::unexpected(role_table.error());
  }

  auto logical_topology = logical_topology_policy_.Build(
      request.snapshot, *role_table, *connectivity_graph);
  if(!logical_topology) {
    return std::unexpected(logical_topology.error());
  }

  return contracts::StructureSnapshot::Create(
      request.cycle_id,
      request.snapshot.version(),
      std::move(*role_table),
      std::move(*connectivity_graph),
      std::move(*logical_topology));
}

}  // namespace ns3_factory::structure::internal
