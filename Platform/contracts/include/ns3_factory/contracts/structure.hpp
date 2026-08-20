#pragma once

#include <utility>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/detail/node_universe.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/topology.hpp>

namespace ns3_factory::contracts {

class StructureSnapshot final {
 public:
  [[nodiscard]] static auto Create(
      PlanningCycleId cycle_id,
      SnapshotVersion base_snapshot_version,
      RoleTable role_table,
      ConnectivityGraph connectivity_graph,
      LogicalTopology logical_topology) -> Result<StructureSnapshot>;

  [[nodiscard]] constexpr auto cycle_id() const noexcept
      -> PlanningCycleId {
    return cycle_id_;
  }

  [[nodiscard]] constexpr auto base_snapshot_version() const noexcept
      -> SnapshotVersion {
    return base_snapshot_version_;
  }

  [[nodiscard]] constexpr auto role_table() const noexcept
      -> const RoleTable& {
    return role_table_;
  }

  [[nodiscard]] constexpr auto connectivity_graph() const noexcept
      -> const ConnectivityGraph& {
    return connectivity_graph_;
  }

  [[nodiscard]] constexpr auto logical_topology() const noexcept
      -> const LogicalTopology& {
    return logical_topology_;
  }

  auto operator==(const StructureSnapshot&) const -> bool = default;

 private:
  StructureSnapshot(PlanningCycleId cycle_id,
                    SnapshotVersion base_snapshot_version,
                    RoleTable role_table,
                    ConnectivityGraph connectivity_graph,
                    LogicalTopology logical_topology) noexcept
      : cycle_id_(cycle_id),
        base_snapshot_version_(base_snapshot_version),
        role_table_(std::move(role_table)),
        connectivity_graph_(std::move(connectivity_graph)),
        logical_topology_(std::move(logical_topology)) {}

  PlanningCycleId cycle_id_;
  SnapshotVersion base_snapshot_version_;
  RoleTable role_table_;
  ConnectivityGraph connectivity_graph_;
  LogicalTopology logical_topology_;
};

inline auto StructureSnapshot::Create(
    PlanningCycleId cycle_id,
    SnapshotVersion base_snapshot_version,
    RoleTable role_table,
    ConnectivityGraph connectivity_graph,
    LogicalTopology logical_topology) -> Result<StructureSnapshot> {
  if(!detail::SameNodeUniverse(role_table.node_ids(),
                               connectivity_graph.node_ids()) ||
     !detail::SameNodeUniverse(logical_topology.node_ids(),
                               connectivity_graph.node_ids())) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "StructureSnapshot components use different node universes"});
  }

  for(const auto& link : logical_topology.links()) {
    if(!connectivity_graph.HasEdge(link.source_node_id,
                                   link.target_node_id)) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "StructureSnapshot logical topology is not a connectivity "
                "subset"});
    }
  }

  return StructureSnapshot{cycle_id,
                           base_snapshot_version,
                           std::move(role_table),
                           std::move(connectivity_graph),
                           std::move(logical_topology)};
}

}  // namespace ns3_factory::contracts
