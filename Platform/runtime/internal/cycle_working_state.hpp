#pragma once

#include <cmath>
#include <memory>
#include <ns3_factory/contracts/delta.hpp>
#include "internal/state_projector.hpp"

namespace ns3_factory::runtime::internal {
class CycleWorkingState final {
 public:
  static auto Create(const contracts::WorldSnapshot& snapshot,
                     contracts::PlanningCycleId cycle_id,
                     contracts::SimTime starts_at,
                     std::shared_ptr<const contracts::INodeTrajectory> trajectories)
      -> contracts::Result<CycleWorkingState> {
    if(starts_at < snapshot.committed_at() || !trajectories)
      return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
          "Working state requires trajectory input and valid cycle start"});
    for(const auto& node : snapshot.nodes()) {
      auto motion = trajectories->Query(node.node_id, snapshot.committed_at());
      if(!motion) return std::unexpected(motion.error());
      // Exact resources may incur last-bit rounding relative to historical
      // accumulated snapshots; explicit absolute tolerance is 1e-8 metres.
      const auto& a = motion->position;
      const auto& b = node.motion.position;
      if(!std::isfinite(a.x_meters) || !std::isfinite(a.y_meters) || !std::isfinite(a.z_meters) ||
         !std::isfinite(b.x_meters) || !std::isfinite(b.y_meters) || !std::isfinite(b.z_meters) ||
         std::abs(a.x_meters-b.x_meters) > 1e-8 ||
         std::abs(a.y_meters-b.y_meters) > 1e-8 ||
         std::abs(a.z_meters-b.z_meters) > 1e-8)
        return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
            "Trajectory initial position differs from committed state"});
    }
    return CycleWorkingState{snapshot,cycle_id,starts_at,std::move(trajectories)};
  }
  auto cycle_id() const noexcept -> contracts::PlanningCycleId { return cycle_id_; }
  auto cycle_started_at() const noexcept -> contracts::SimTime { return starts_at_; }
  auto base_snapshot() const noexcept -> const contracts::WorldSnapshot& { return snapshot_; }
  auto ProjectNodeState(contracts::NodeId node, contracts::SimTime time) const
      -> contracts::Result<contracts::NodeCommittedState> {
    if(time < starts_at_) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition, "Projection precedes cycle"});
    return StateProjector::ProjectNodeState(snapshot_,*trajectories_,node,time);
  }
  auto FinalizeDeltaSet(contracts::SimTime closes_at) const -> contracts::Result<contracts::DeltaSet> {
    if(closes_at < starts_at_) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition, "Close precedes cycle"});
    std::vector<contracts::NodeStateReplacement> replacements;
    for(const auto& node : snapshot_.nodes()) {
      auto state = ProjectNodeState(node.node_id,closes_at);
      if(!state) return std::unexpected(state.error());
      replacements.push_back({*state});
    }
    return contracts::DeltaSet{cycle_id_,snapshot_.version(),closes_at,std::move(replacements)};
  }
 private:
  CycleWorkingState(const contracts::WorldSnapshot& snapshot, contracts::PlanningCycleId cycle,
                    contracts::SimTime start, std::shared_ptr<const contracts::INodeTrajectory> trajectories)
      : snapshot_(snapshot),cycle_id_(cycle),starts_at_(start),trajectories_(std::move(trajectories)) {}
  contracts::WorldSnapshot snapshot_;
  contracts::PlanningCycleId cycle_id_;
  contracts::SimTime starts_at_;
  std::shared_ptr<const contracts::INodeTrajectory> trajectories_;
};
}  // namespace ns3_factory::runtime::internal
