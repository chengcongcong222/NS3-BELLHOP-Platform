#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/node_capability.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::contracts {

struct Position3d final {
  double x_meters;
  double y_meters;
  double z_meters;

  constexpr auto operator==(const Position3d&) const -> bool = default;
};

struct Velocity3d final {
  double x_meters_per_second;
  double y_meters_per_second;
  double z_meters_per_second;

  constexpr auto operator==(const Velocity3d&) const -> bool = default;
};

struct MotionState final {
  Position3d position;
  Velocity3d velocity;

  constexpr auto operator==(const MotionState&) const -> bool = default;
};

struct NodeCommittedState final {
  NodeId node_id;
  NodeCapabilityProfile capability;
  MotionState motion;

  constexpr auto operator==(const NodeCommittedState&) const
      -> bool = default;
};

class WorldSnapshot final {
 public:
  [[nodiscard]] static auto Create(SnapshotVersion version,
                                   SimTime committed_at,
                                   std::vector<NodeCommittedState> nodes)
      -> Result<WorldSnapshot>;

  [[nodiscard]] constexpr auto version() const noexcept -> SnapshotVersion {
    return version_;
  }

  [[nodiscard]] constexpr auto committed_at() const noexcept -> SimTime {
    return committed_at_;
  }

  [[nodiscard]] auto nodes() const noexcept
      -> std::span<const NodeCommittedState> {
    return std::span<const NodeCommittedState>{nodes_};
  }

  [[nodiscard]] auto FindNode(NodeId node_id) const noexcept
      -> std::optional<std::reference_wrapper<const NodeCommittedState>> {
    const auto it = std::lower_bound(
        nodes_.begin(),
        nodes_.end(),
        node_id,
        [](const NodeCommittedState& node, NodeId id) {
          return node.node_id < id;
        });
    if(it == nodes_.end() || it->node_id != node_id) {
      return std::nullopt;
    }
    return std::cref(*it);
  }

 private:
  WorldSnapshot(SnapshotVersion version,
                SimTime committed_at,
                std::vector<NodeCommittedState> nodes)
      : version_(version),
        committed_at_(committed_at),
        nodes_(std::move(nodes)) {}

  SnapshotVersion version_;
  SimTime committed_at_;
  std::vector<NodeCommittedState> nodes_;
};

inline auto WorldSnapshot::Create(SnapshotVersion version,
                                  SimTime committed_at,
                                  std::vector<NodeCommittedState> nodes)
    -> Result<WorldSnapshot> {
  std::sort(nodes.begin(), nodes.end(), [](const NodeCommittedState& lhs,
                                           const NodeCommittedState& rhs) {
    return lhs.node_id < rhs.node_id;
  });

  const auto duplicate =
      std::adjacent_find(nodes.begin(), nodes.end(),
                         [](const NodeCommittedState& lhs,
                            const NodeCommittedState& rhs) {
                           return lhs.node_id == rhs.node_id;
                         });
  if(duplicate != nodes.end()) {
    return std::unexpected(
        Error{ErrorCode::kAlreadyExists,
              "WorldSnapshot contains a duplicate NodeId"});
  }

  return WorldSnapshot{version, committed_at, std::move(nodes)};
}

}  // namespace ns3_factory::contracts
