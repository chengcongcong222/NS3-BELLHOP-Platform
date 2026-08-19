#pragma once

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/delta.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/time.hpp>

#include "internal/state_projector.hpp"

namespace ns3_factory::runtime::internal {

class CycleWorkingState final {
 public:
  [[nodiscard]] static auto Create(
      const contracts::WorldSnapshot& base_snapshot,
      contracts::PlanningCycleId cycle_id,
      contracts::SimTime cycle_started_at)
      -> contracts::Result<CycleWorkingState>;

  [[nodiscard]] constexpr auto cycle_id() const noexcept
      -> contracts::PlanningCycleId {
    return cycle_id_;
  }

  [[nodiscard]] constexpr auto cycle_started_at() const noexcept
      -> contracts::SimTime {
    return cycle_started_at_;
  }

  [[nodiscard]] auto base_snapshot() const noexcept
      -> const contracts::WorldSnapshot& {
    return base_snapshot_;
  }

  [[nodiscard]] auto ProjectNodeState(contracts::NodeId node_id,
                                      contracts::SimTime query_time) const
      -> contracts::Result<contracts::NodeCommittedState>;

  [[nodiscard]] auto UpdateVelocity(
      contracts::NodeId node_id,
      contracts::Velocity3d velocity,
      contracts::SimTime effective_at) -> contracts::Status;

  [[nodiscard]] auto FinalizeDeltaSet(
      contracts::SimTime cycle_close_time) const
      -> contracts::Result<contracts::DeltaSet>;

 private:
  CycleWorkingState(const contracts::WorldSnapshot& base_snapshot,
                    contracts::PlanningCycleId cycle_id,
                    contracts::SimTime cycle_started_at)
      : base_snapshot_(base_snapshot),
        cycle_id_(cycle_id),
        cycle_started_at_(cycle_started_at) {}

  [[nodiscard]] auto FindOverlay(contracts::NodeId node_id)
      -> std::vector<WorkingNodeOverlay>::iterator;

  contracts::WorldSnapshot base_snapshot_;
  contracts::PlanningCycleId cycle_id_;
  contracts::SimTime cycle_started_at_;
  std::vector<WorkingNodeOverlay> overlays_;
};

inline auto CycleWorkingState::Create(
    const contracts::WorldSnapshot& base_snapshot,
    contracts::PlanningCycleId cycle_id,
    contracts::SimTime cycle_started_at)
    -> contracts::Result<CycleWorkingState> {
  if(cycle_started_at < base_snapshot.committed_at()) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "CycleWorkingState cannot start before its base snapshot"});
  }
  return CycleWorkingState{base_snapshot, cycle_id, cycle_started_at};
}

inline auto CycleWorkingState::ProjectNodeState(
    contracts::NodeId node_id,
    contracts::SimTime query_time) const
    -> contracts::Result<contracts::NodeCommittedState> {
  if(query_time < cycle_started_at_) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "CycleWorkingState query_time precedes the cycle start"});
  }
  return StateProjector::ProjectNodeState(
      base_snapshot_, overlays_, node_id, query_time);
}

inline auto CycleWorkingState::UpdateVelocity(
    contracts::NodeId node_id,
    contracts::Velocity3d velocity,
    contracts::SimTime effective_at) -> contracts::Status {
  const auto projected = ProjectNodeState(node_id, effective_at);
  if(!projected) {
    return std::unexpected(projected.error());
  }

  auto updated_state = *projected;
  updated_state.motion.velocity = velocity;

  const auto overlay = FindOverlay(node_id);
  if(overlay != overlays_.end() && overlay->state.node_id == node_id) {
    overlay->state = std::move(updated_state);
    overlay->motion_anchor = effective_at;
    return {};
  }

  overlays_.insert(
      overlay, WorkingNodeOverlay{std::move(updated_state), effective_at});
  return {};
}

inline auto CycleWorkingState::FinalizeDeltaSet(
    contracts::SimTime cycle_close_time) const
    -> contracts::Result<contracts::DeltaSet> {
  if(cycle_close_time < cycle_started_at_) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "CycleWorkingState cannot finalize before the cycle start"});
  }

  std::vector<contracts::NodeStateReplacement> replacements;
  replacements.reserve(base_snapshot_.nodes().size());
  for(const auto& base_node : base_snapshot_.nodes()) {
    const auto projected = StateProjector::ProjectNodeState(
        base_snapshot_, overlays_, base_node.node_id, cycle_close_time);
    if(!projected) {
      return std::unexpected(projected.error());
    }
    replacements.push_back(contracts::NodeStateReplacement{*projected});
  }

  return contracts::DeltaSet{cycle_id_,
                             base_snapshot_.version(),
                             cycle_close_time,
                             std::move(replacements)};
}

inline auto CycleWorkingState::FindOverlay(contracts::NodeId node_id)
    -> std::vector<WorkingNodeOverlay>::iterator {
  return std::lower_bound(
      overlays_.begin(),
      overlays_.end(),
      node_id,
      [](const WorkingNodeOverlay& candidate, contracts::NodeId id) {
        return candidate.state.node_id < id;
      });
}

}  // namespace ns3_factory::runtime::internal
