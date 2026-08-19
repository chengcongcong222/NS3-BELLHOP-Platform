#include <cstdlib>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include "internal/cycle_working_state.hpp"

using ns3_factory::contracts::DuplexMode;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::MotionState;
using ns3_factory::contracts::NodeCapabilityProfile;
using ns3_factory::contracts::NodeCommittedState;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::Velocity3d;
using ns3_factory::contracts::WorldSnapshot;
using ns3_factory::runtime::internal::CycleWorkingState;
using ns3_factory::runtime::internal::StateProjector;

template <typename T>
concept HasCommitMethod = requires(T value) { value.Commit(); };

template <typename T>
concept HasApplyMethod = requires(T value) { value.Apply(); };

static_assert(std::is_empty_v<StateProjector>);
static_assert(!HasCommitMethod<CycleWorkingState>);
static_assert(!HasApplyMethod<CycleWorkingState>);

constexpr auto Seconds(std::int64_t value) -> SimTime {
  return SimTime::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto MakeNode(std::uint64_t id,
                        double x_meters,
                        double x_velocity) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{x_meters, 0.0, -10.0},
                  Velocity3d{x_velocity, 0.0, 0.0}}};
}

auto main() -> int {
  auto snapshot_result = WorldSnapshot::Create(
      SnapshotVersion{11},
      Seconds(0),
      std::vector<NodeCommittedState>{
          MakeNode(2, 0.0, 1.0),
          MakeNode(0, 0.0, 2.0),
          MakeNode(1, 0.0, 1.0)});
  if(!snapshot_result) {
    return EXIT_FAILURE;
  }
  const WorldSnapshot snapshot = std::move(*snapshot_result);

  auto working_result = CycleWorkingState::Create(
      snapshot, PlanningCycleId{4}, Seconds(0));
  if(!working_result) {
    return EXIT_FAILURE;
  }
  auto working = std::move(*working_result);

  const auto node_zero_at_five =
      working.ProjectNodeState(NodeId{0}, Seconds(5));
  const auto missing = working.ProjectNodeState(NodeId{99}, Seconds(5));
  if(!node_zero_at_five ||
     node_zero_at_five->motion.position.x_meters != 10.0 || missing ||
     missing.error().code != ErrorCode::kNotFound) {
    return EXIT_FAILURE;
  }

  const auto node_one_velocity_update = working.UpdateVelocity(
      NodeId{1}, Velocity3d{2.0, 0.0, 0.0}, Seconds(5));
  if(!node_one_velocity_update) {
    return EXIT_FAILURE;
  }

  const auto before_node_one_anchor =
      working.ProjectNodeState(NodeId{1}, Seconds(4));
  const auto node_one_at_ten =
      working.ProjectNodeState(NodeId{1}, Seconds(10));
  if(before_node_one_anchor ||
     before_node_one_anchor.error().code != ErrorCode::kFailedPrecondition ||
     !node_one_at_ten ||
     node_one_at_ten->motion.position.x_meters != 15.0) {
    return EXIT_FAILURE;
  }

  if(!working.UpdateVelocity(
         NodeId{2}, Velocity3d{2.0, 0.0, 0.0}, Seconds(2)) ||
     !working.UpdateVelocity(
         NodeId{2}, Velocity3d{3.0, 0.0, 0.0}, Seconds(4))) {
    return EXIT_FAILURE;
  }

  const auto delta_result = working.FinalizeDeltaSet(Seconds(10));
  if(!delta_result) {
    return EXIT_FAILURE;
  }
  const auto& delta = *delta_result;
  if(delta.cycle_id != PlanningCycleId{4} ||
     delta.base_version != SnapshotVersion{11} ||
     delta.effective_at != Seconds(10) ||
     delta.node_replacements.size() != 3 ||
     delta.node_replacements[0].state.node_id != NodeId{0} ||
     delta.node_replacements[1].state.node_id != NodeId{1} ||
     delta.node_replacements[2].state.node_id != NodeId{2} ||
     delta.node_replacements[0].state.motion.position.x_meters != 20.0 ||
     delta.node_replacements[1].state.motion.position.x_meters != 15.0 ||
     delta.node_replacements[2].state.motion.position.x_meters != 24.0) {
    return EXIT_FAILURE;
  }

  const auto base_zero = snapshot.FindNode(NodeId{0});
  const auto base_one = snapshot.FindNode(NodeId{1});
  const auto base_two = snapshot.FindNode(NodeId{2});
  if(snapshot.version() != SnapshotVersion{11} || !base_zero || !base_one ||
     !base_two || base_zero->get().motion.position.x_meters != 0.0 ||
     base_one->get().motion.position.x_meters != 0.0 ||
     base_one->get().motion.velocity.x_meters_per_second != 1.0 ||
     base_two->get().motion.position.x_meters != 0.0 ||
     base_two->get().motion.velocity.x_meters_per_second != 1.0) {
    return EXIT_FAILURE;
  }

  const auto invalid_start = CycleWorkingState::Create(
      snapshot, PlanningCycleId{5}, SimTime::FromNanoseconds(-1));
  const auto invalid_close =
      working.FinalizeDeltaSet(SimTime::FromNanoseconds(-1));
  if(invalid_start ||
     invalid_start.error().code != ErrorCode::kFailedPrecondition ||
     invalid_close ||
     invalid_close.error().code != ErrorCode::kFailedPrecondition) {
    return EXIT_FAILURE;
  }

  auto extreme_snapshot_result = WorldSnapshot::Create(
      SnapshotVersion{12},
      SimTime::FromNanoseconds(std::numeric_limits<std::int64_t>::min()),
      std::vector<NodeCommittedState>{MakeNode(0, 0.0, 1.0)});
  if(!extreme_snapshot_result) {
    return EXIT_FAILURE;
  }
  const WorldSnapshot extreme_snapshot = std::move(*extreme_snapshot_result);
  auto extreme_working_result = CycleWorkingState::Create(
      extreme_snapshot,
      PlanningCycleId{6},
      extreme_snapshot.committed_at());
  if(!extreme_working_result) {
    return EXIT_FAILURE;
  }
  const auto overflow = extreme_working_result->ProjectNodeState(
      NodeId{0},
      SimTime::FromNanoseconds(std::numeric_limits<std::int64_t>::max()));
  if(overflow || overflow.error().code != ErrorCode::kOverflow) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
