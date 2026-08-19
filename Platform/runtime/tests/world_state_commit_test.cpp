#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "internal/commit_service.hpp"
#include "internal/cycle_working_state.hpp"

using ns3_factory::contracts::DeltaSet;
using ns3_factory::contracts::DuplexMode;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::MotionState;
using ns3_factory::contracts::NodeCapabilityProfile;
using ns3_factory::contracts::NodeCommittedState;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::NodeStateReplacement;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::Status;
using ns3_factory::contracts::Velocity3d;
using ns3_factory::contracts::WorldSnapshot;
using ns3_factory::runtime::internal::CommitService;
using ns3_factory::runtime::internal::CycleWorkingState;
using ns3_factory::runtime::internal::WorldStateStore;

template <typename T>
concept HasApplyMethod = requires(T value) { value.Apply(); };

template <typename T>
concept HasCommitMethod = requires(T value) { value.Commit(); };

template <typename T>
concept HasReplaceSnapshotMethod =
    requires(T value, WorldSnapshot snapshot) {
      value.ReplaceSnapshot(std::move(snapshot));
    };

template <typename T>
concept HasSetCurrentMethod = requires(T value, WorldSnapshot snapshot) {
  value.SetCurrent(std::move(snapshot));
};

template <typename T>
concept CanCommitWithIndependentTime =
    requires(T value,
             SnapshotVersion version,
             const DeltaSet& delta,
             SimTime committed_at) {
      value.CommitCycle(version, delta, committed_at);
    };

static_assert(!HasApplyMethod<DeltaSet>);
static_assert(!HasCommitMethod<DeltaSet>);
static_assert(!HasReplaceSnapshotMethod<WorldStateStore>);
static_assert(!HasSetCurrentMethod<WorldStateStore>);
static_assert(!CanCommitWithIndependentTime<CommitService>);
static_assert(!std::is_copy_constructible_v<WorldStateStore>);
static_assert(!std::is_move_constructible_v<WorldStateStore>);
static_assert(std::same_as<
              decltype(std::declval<const WorldStateStore&>()
                           .current_snapshot()),
              const WorldSnapshot&>);
static_assert(std::same_as<
              decltype(std::declval<CommitService&>().CommitCycle(
                  std::declval<SnapshotVersion>(),
                  std::declval<const DeltaSet&>())),
              Status>);

constexpr auto Seconds(std::int64_t value) -> SimTime {
  return SimTime::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto MakeNode(std::uint64_t id,
                        double x_meters,
                        double x_velocity = 1.0) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{x_meters, 0.0, -10.0},
                  Velocity3d{x_velocity, 0.0, 0.0}}};
}

auto MakeSnapshot(SnapshotVersion version,
                  SimTime committed_at) -> Result<WorldSnapshot> {
  return WorldSnapshot::Create(
      version,
      committed_at,
      std::vector<NodeCommittedState>{MakeNode(1, 10.0), MakeNode(0, 0.0)});
}

auto MakeFullDelta(const WorldSnapshot& snapshot,
                   PlanningCycleId cycle_id,
                   SimTime effective_at) -> DeltaSet {
  std::vector<NodeStateReplacement> replacements;
  replacements.reserve(snapshot.nodes().size());
  for(const auto& node : snapshot.nodes()) {
    replacements.push_back(NodeStateReplacement{node});
  }
  return DeltaSet{cycle_id,
                  snapshot.version(),
                  effective_at,
                  std::move(replacements)};
}

struct StoreImage final {
  SnapshotVersion version;
  SimTime committed_at;
  std::vector<NodeCommittedState> nodes;
  std::optional<PlanningCycleId> last_cycle;

  auto operator==(const StoreImage&) const -> bool = default;
};

auto Capture(const WorldStateStore& store) -> StoreImage {
  const auto& snapshot = store.current_snapshot();
  return StoreImage{
      snapshot.version(),
      snapshot.committed_at(),
      std::vector<NodeCommittedState>{snapshot.nodes().begin(),
                                      snapshot.nodes().end()},
      store.last_committed_cycle_id()};
}

auto RejectsWithoutMutation(CommitService& service,
                            const WorldStateStore& store,
                            SnapshotVersion expected_version,
                            const DeltaSet& delta,
                            ErrorCode expected_error) -> bool {
  const auto before = Capture(store);
  const auto status = service.CommitCycle(expected_version, delta);
  return !status && status.error().code == expected_error &&
         Capture(store) == before;
}

auto TestSuccessfulCommitAndStaleReuse() -> bool {
  auto initial_result = MakeSnapshot(SnapshotVersion{0}, Seconds(0));
  if(!initial_result) {
    return false;
  }

  WorldStateStore store{*initial_result};
  auto working_result = CycleWorkingState::Create(
      store.current_snapshot(), PlanningCycleId{0}, Seconds(0));
  if(!working_result) {
    return false;
  }
  auto working = std::move(*working_result);
  if(!working.UpdateVelocity(
         NodeId{0}, Velocity3d{2.0, 0.0, 0.0}, Seconds(5))) {
    return false;
  }

  const auto delta_result = working.FinalizeDeltaSet(Seconds(10));
  if(!delta_result || delta_result->base_version != SnapshotVersion{0} ||
     delta_result->effective_at != Seconds(10)) {
    return false;
  }
  const DeltaSet delta = *delta_result;

  CommitService service{store};
  if(!service.CommitCycle(SnapshotVersion{0}, delta)) {
    return false;
  }

  const auto& committed = store.current_snapshot();
  const auto committed_zero = committed.FindNode(NodeId{0});
  const auto old_working_zero = working.base_snapshot().FindNode(NodeId{0});
  if(committed.version() != SnapshotVersion{1} ||
     committed.committed_at() != delta.effective_at || !committed_zero ||
     committed_zero->get().motion.position.x_meters != 15.0 ||
     store.last_committed_cycle_id() != PlanningCycleId{0} ||
     working.base_snapshot().version() != SnapshotVersion{0} ||
     working.base_snapshot().committed_at() != Seconds(0) ||
     !old_working_zero ||
     old_working_zero->get().motion.position.x_meters != 0.0) {
    return false;
  }

  if(!RejectsWithoutMutation(service,
                             store,
                             SnapshotVersion{0},
                             delta,
                             ErrorCode::kFailedPrecondition)) {
    return false;
  }
  return RejectsWithoutMutation(service,
                                store,
                                SnapshotVersion{1},
                                delta,
                                ErrorCode::kFailedPrecondition);
}

auto TestVersionTimeAndReplacementValidation() -> bool {
  auto initial_result = MakeSnapshot(SnapshotVersion{7}, Seconds(10));
  if(!initial_result) {
    return false;
  }
  WorldStateStore store{*initial_result};
  CommitService service{store};

  const auto valid =
      MakeFullDelta(store.current_snapshot(), PlanningCycleId{3}, Seconds(20));
  if(!RejectsWithoutMutation(service,
                             store,
                             SnapshotVersion{8},
                             valid,
                             ErrorCode::kFailedPrecondition)) {
    return false;
  }

  auto wrong_base = valid;
  wrong_base.base_version = SnapshotVersion{6};
  if(!RejectsWithoutMutation(service,
                             store,
                             SnapshotVersion{7},
                             wrong_base,
                             ErrorCode::kFailedPrecondition)) {
    return false;
  }

  auto reverse_time = valid;
  reverse_time.effective_at = Seconds(9);
  if(!RejectsWithoutMutation(service,
                             store,
                             SnapshotVersion{7},
                             reverse_time,
                             ErrorCode::kFailedPrecondition)) {
    return false;
  }

  auto duplicate = valid;
  duplicate.node_replacements[1] = duplicate.node_replacements[0];
  if(!RejectsWithoutMutation(service,
                             store,
                             SnapshotVersion{7},
                             duplicate,
                             ErrorCode::kAlreadyExists)) {
    return false;
  }

  auto missing = valid;
  missing.node_replacements.pop_back();
  if(!RejectsWithoutMutation(service,
                             store,
                             SnapshotVersion{7},
                             missing,
                             ErrorCode::kFailedPrecondition)) {
    return false;
  }

  auto additional = valid;
  additional.node_replacements.push_back(
      NodeStateReplacement{MakeNode(99, 99.0)});
  if(!RejectsWithoutMutation(service,
                             store,
                             SnapshotVersion{7},
                             additional,
                             ErrorCode::kInvalidArgument)) {
    return false;
  }

  auto non_canonical = valid;
  std::swap(non_canonical.node_replacements[0],
            non_canonical.node_replacements[1]);
  return RejectsWithoutMutation(service,
                                store,
                                SnapshotVersion{7},
                                non_canonical,
                                ErrorCode::kInvalidArgument);
}

auto TestCyclePolicy() -> bool {
  auto initial_result = MakeSnapshot(SnapshotVersion{0}, Seconds(0));
  if(!initial_result) {
    return false;
  }
  WorldStateStore store{*initial_result};
  CommitService service{store};

  const auto first =
      MakeFullDelta(store.current_snapshot(), PlanningCycleId{5}, Seconds(1));
  if(!service.CommitCycle(SnapshotVersion{0}, first) ||
     store.last_committed_cycle_id() != PlanningCycleId{5}) {
    return false;
  }

  const auto duplicate =
      MakeFullDelta(store.current_snapshot(), PlanningCycleId{5}, Seconds(2));
  if(!RejectsWithoutMutation(service,
                             store,
                             SnapshotVersion{1},
                             duplicate,
                             ErrorCode::kAlreadyExists)) {
    return false;
  }

  const auto older =
      MakeFullDelta(store.current_snapshot(), PlanningCycleId{4}, Seconds(2));
  return RejectsWithoutMutation(service,
                                store,
                                SnapshotVersion{1},
                                older,
                                ErrorCode::kFailedPrecondition);
}

auto TestSnapshotVersionOverflow() -> bool {
  constexpr auto kMaxVersion =
      std::numeric_limits<SnapshotVersion::value_type>::max();
  auto initial_result =
      MakeSnapshot(SnapshotVersion{kMaxVersion}, Seconds(0));
  if(!initial_result) {
    return false;
  }
  WorldStateStore store{*initial_result};
  CommitService service{store};
  const auto delta =
      MakeFullDelta(store.current_snapshot(), PlanningCycleId{0}, Seconds(1));
  return RejectsWithoutMutation(service,
                                store,
                                SnapshotVersion{kMaxVersion},
                                delta,
                                ErrorCode::kOverflow);
}

auto main() -> int {
  return TestSuccessfulCommitAndStaleReuse() &&
                 TestVersionTimeAndReplacementValidation() &&
                 TestCyclePolicy() && TestSnapshotVersionOverflow()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
