#pragma once

#include <algorithm>
#include <span>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::runtime::internal {

struct WorkingNodeOverlay final {
  contracts::NodeCommittedState state;
  contracts::SimTime motion_anchor;
};

class StateProjector final {
 public:
  [[nodiscard]] static auto ProjectNodeState(
      const contracts::WorldSnapshot& base_snapshot,
      std::span<const WorkingNodeOverlay> overlays,
      contracts::NodeId node_id,
      contracts::SimTime query_time)
      -> contracts::Result<contracts::NodeCommittedState>;

 private:
  [[nodiscard]] static auto ProjectFromAnchor(
      const contracts::NodeCommittedState& anchor_state,
      contracts::SimTime anchor_time,
      contracts::SimTime query_time)
      -> contracts::Result<contracts::NodeCommittedState>;
};

inline auto StateProjector::ProjectNodeState(
    const contracts::WorldSnapshot& base_snapshot,
    std::span<const WorkingNodeOverlay> overlays,
    contracts::NodeId node_id,
    contracts::SimTime query_time)
    -> contracts::Result<contracts::NodeCommittedState> {
  const auto base_node = base_snapshot.FindNode(node_id);
  if(!base_node) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kNotFound,
        "StateProjector could not find the requested NodeId"});
  }

  const auto overlay = std::lower_bound(
      overlays.begin(),
      overlays.end(),
      node_id,
      [](const WorkingNodeOverlay& candidate, contracts::NodeId id) {
        return candidate.state.node_id < id;
      });
  if(overlay != overlays.end() && overlay->state.node_id == node_id) {
    return ProjectFromAnchor(overlay->state, overlay->motion_anchor, query_time);
  }

  return ProjectFromAnchor(
      base_node->get(), base_snapshot.committed_at(), query_time);
}

inline auto StateProjector::ProjectFromAnchor(
    const contracts::NodeCommittedState& anchor_state,
    contracts::SimTime anchor_time,
    contracts::SimTime query_time)
    -> contracts::Result<contracts::NodeCommittedState> {
  if(query_time < anchor_time) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "StateProjector query_time precedes the motion anchor"});
  }

  const auto elapsed = contracts::CheckedSubtract(query_time, anchor_time);
  if(!elapsed) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kOverflow,
        "StateProjector elapsed simulation time overflowed"});
  }

  constexpr double kNanosecondsPerSecond = 1'000'000'000.0;
  const double elapsed_seconds =
      static_cast<double>(elapsed->nanoseconds()) / kNanosecondsPerSecond;

  auto projected = anchor_state;
  projected.motion.position.x_meters +=
      anchor_state.motion.velocity.x_meters_per_second * elapsed_seconds;
  projected.motion.position.y_meters +=
      anchor_state.motion.velocity.y_meters_per_second * elapsed_seconds;
  projected.motion.position.z_meters +=
      anchor_state.motion.velocity.z_meters_per_second * elapsed_seconds;
  return projected;
}

}  // namespace ns3_factory::runtime::internal
