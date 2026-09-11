#pragma once

#include <cmath>

#include <ns3_factory/contracts/trajectory.hpp>

namespace ns3_factory::runtime::internal {
class StateProjector final {
 public:
  static auto ProjectNodeState(const contracts::WorldSnapshot& snapshot,
                               const contracts::INodeTrajectory& trajectories,
                               contracts::NodeId node, contracts::SimTime time)
      -> contracts::Result<contracts::NodeCommittedState> {
    const auto base = snapshot.FindNode(node);
    if(!base) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kNotFound, "StateProjector node not found"});
    if(time < snapshot.committed_at()) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition, "Projection precedes snapshot"});
    auto motion = trajectories.Query(node, time);
    if(!motion) return std::unexpected(motion.error());
    if(!std::isfinite(motion->position.x_meters) || !std::isfinite(motion->position.y_meters) ||
       !std::isfinite(motion->position.z_meters) || !std::isfinite(motion->velocity.x_meters_per_second) ||
       !std::isfinite(motion->velocity.y_meters_per_second) || !std::isfinite(motion->velocity.z_meters_per_second))
      return std::unexpected(contracts::Error{contracts::ErrorCode::kInvalidArgument,
          "Trajectory query returned nonfinite state"});
    auto result = base->get();
    result.motion = *motion;
    return result;
  }
};
}  // namespace ns3_factory::runtime::internal
