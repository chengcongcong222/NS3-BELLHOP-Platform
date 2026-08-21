#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/structure.hpp>

namespace ns3_factory::contracts {

struct RouteEntry final {
  NodeId forwarding_node_id;
  NodeId destination_node_id;
  NodeId next_hop_node_id;

  constexpr auto operator==(const RouteEntry&) const noexcept
      -> bool = default;
};

class RoutingPlan final {
 public:
  [[nodiscard]] static auto Create(
      std::vector<RouteEntry> entries,
      const StructureSnapshot& structure_snapshot) -> Result<RoutingPlan>;

  [[nodiscard]] constexpr auto cycle_id() const noexcept
      -> PlanningCycleId {
    return cycle_id_;
  }

  [[nodiscard]] constexpr auto base_snapshot_version() const noexcept
      -> SnapshotVersion {
    return base_snapshot_version_;
  }

  [[nodiscard]] auto entries() const noexcept
      -> std::span<const RouteEntry> {
    return std::span<const RouteEntry>{entries_};
  }

  [[nodiscard]] auto FindNextHop(NodeId forwarding_node_id,
                                 NodeId destination_node_id) const noexcept
      -> std::optional<NodeId>;

  auto operator==(const RoutingPlan&) const -> bool = default;

 private:
  RoutingPlan(PlanningCycleId cycle_id,
              SnapshotVersion base_snapshot_version,
              std::vector<RouteEntry> entries) noexcept
      : cycle_id_(cycle_id),
        base_snapshot_version_(base_snapshot_version),
        entries_(std::move(entries)) {}

  [[nodiscard]] static constexpr auto Less(
      const RouteEntry& lhs,
      const RouteEntry& rhs) noexcept -> bool;

  PlanningCycleId cycle_id_;
  SnapshotVersion base_snapshot_version_;
  std::vector<RouteEntry> entries_;
};

inline constexpr auto RoutingPlan::Less(const RouteEntry& lhs,
                                        const RouteEntry& rhs) noexcept
    -> bool {
  if(lhs.forwarding_node_id != rhs.forwarding_node_id) {
    return lhs.forwarding_node_id < rhs.forwarding_node_id;
  }
  if(lhs.destination_node_id != rhs.destination_node_id) {
    return lhs.destination_node_id < rhs.destination_node_id;
  }
  return lhs.next_hop_node_id < rhs.next_hop_node_id;
}

inline auto RoutingPlan::Create(
    std::vector<RouteEntry> entries,
    const StructureSnapshot& structure_snapshot) -> Result<RoutingPlan> {
  const auto& logical_topology = structure_snapshot.logical_topology();
  const auto node_ids = logical_topology.node_ids();
  const auto contains_node = [&](NodeId node_id) {
    return std::binary_search(node_ids.begin(), node_ids.end(), node_id);
  };

  for(const auto& entry : entries) {
    if(entry.forwarding_node_id == entry.destination_node_id) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "RouteEntry forwarding node must differ from destination"});
    }
    if(entry.forwarding_node_id == entry.next_hop_node_id) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "RouteEntry forwarding node must differ from next hop"});
    }
    if(!contains_node(entry.forwarding_node_id) ||
       !contains_node(entry.destination_node_id) ||
       !contains_node(entry.next_hop_node_id)) {
      return std::unexpected(
          Error{ErrorCode::kNotFound,
                "RouteEntry references a NodeId outside the structure "
                "node universe"});
    }
    if(!logical_topology.HasLink(entry.forwarding_node_id,
                                 entry.next_hop_node_id)) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "RouteEntry next hop is not an edge in LogicalTopology"});
    }
  }

  std::sort(entries.begin(), entries.end(), Less);
  for(std::size_t index = 1; index < entries.size(); ++index) {
    const auto& previous = entries[index - 1];
    const auto& current = entries[index];
    if(previous.forwarding_node_id == current.forwarding_node_id &&
       previous.destination_node_id == current.destination_node_id) {
      return std::unexpected(
          Error{ErrorCode::kAlreadyExists,
                "RoutingPlan contains multiple entries for one "
                "forwarding-node/destination route key"});
    }
  }

  return RoutingPlan{structure_snapshot.cycle_id(),
                     structure_snapshot.base_snapshot_version(),
                     std::move(entries)};
}

inline auto RoutingPlan::FindNextHop(
    NodeId forwarding_node_id,
    NodeId destination_node_id) const noexcept -> std::optional<NodeId> {
  const auto key = std::pair{forwarding_node_id, destination_node_id};
  const auto entry = std::lower_bound(
      entries_.begin(),
      entries_.end(),
      key,
      [](const RouteEntry& candidate,
         const std::pair<NodeId, NodeId>& route_key) {
        if(candidate.forwarding_node_id != route_key.first) {
          return candidate.forwarding_node_id < route_key.first;
        }
        return candidate.destination_node_id < route_key.second;
      });
  if(entry == entries_.end() ||
     entry->forwarding_node_id != forwarding_node_id ||
     entry->destination_node_id != destination_node_id) {
    return std::nullopt;
  }
  return entry->next_hop_node_id;
}

}  // namespace ns3_factory::contracts
