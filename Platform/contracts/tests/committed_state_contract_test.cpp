#include <concepts>
#include <cstdlib>
#include <functional>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/delta.hpp>

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
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::Velocity3d;
using ns3_factory::contracts::WorldSnapshot;

template <typename T>
concept HasRoleMember = requires(T value) { value.role; };

template <typename T>
concept HasSinkMember = requires(T value) { value.sink; };

template <typename T>
concept HasRelayMember = requires(T value) { value.relay; };

template <typename T>
concept HasCommitMethod = requires(T value) { value.Commit(); };

template <typename T>
concept HasApplyMethod = requires(T value) { value.Apply(); };

using NodeSpan = std::span<const NodeCommittedState>;
using NodeLookup =
    std::optional<std::reference_wrapper<const NodeCommittedState>>;

static_assert(!HasRoleMember<NodeCapabilityProfile>);
static_assert(!HasSinkMember<NodeCapabilityProfile>);
static_assert(!HasRelayMember<NodeCapabilityProfile>);
static_assert(!HasCommitMethod<DeltaSet>);
static_assert(!HasApplyMethod<DeltaSet>);
static_assert(!std::is_default_constructible_v<WorldSnapshot>);
static_assert(std::same_as<decltype(std::declval<WorldSnapshot&>().nodes()),
                           NodeSpan>);
static_assert(
    std::same_as<decltype(std::declval<WorldSnapshot&>().FindNode(NodeId{0})),
                 NodeLookup>);
static_assert(!std::assignable_from<
              decltype(std::declval<WorldSnapshot&>().nodes()[0]),
              NodeCommittedState>);

constexpr auto MakeNode(ns3_factory::contracts::IdentityValue id,
                        double x_meters) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{x_meters, 0.0, -10.0},
                  Velocity3d{1.0, 0.0, 0.0}}};
}

static_assert(NodeCapabilityProfile{true, false, DuplexMode::kHalfDuplex}
                  .can_communicate());
static_assert(!NodeCapabilityProfile{false, false, DuplexMode::kFullDuplex}
                   .can_communicate());
static_assert(MakeNode(0, 1.0).node_id == NodeId{0});

int main() {
  auto snapshot_result = WorldSnapshot::Create(
      SnapshotVersion{7},
      SimTime::FromNanoseconds(2'000),
      std::vector<NodeCommittedState>{
          MakeNode(2, 20.0), MakeNode(0, 0.0), MakeNode(1, 10.0)});
  if(!snapshot_result) {
    return EXIT_FAILURE;
  }

  WorldSnapshot snapshot = std::move(*snapshot_result);
  const auto nodes = snapshot.nodes();
  if(nodes.size() != 3 || nodes[0].node_id != NodeId{0} ||
     nodes[1].node_id != NodeId{1} || nodes[2].node_id != NodeId{2}) {
    return EXIT_FAILURE;
  }

  const auto node_zero = snapshot.FindNode(NodeId{0});
  const auto missing_node = snapshot.FindNode(NodeId{99});
  if(!node_zero || node_zero->get().node_id != NodeId{0} || missing_node) {
    return EXIT_FAILURE;
  }

  if(snapshot.version() != SnapshotVersion{7} ||
     snapshot.committed_at() != SimTime::FromNanoseconds(2'000)) {
    return EXIT_FAILURE;
  }

  const auto duplicate_result = WorldSnapshot::Create(
      SnapshotVersion{8},
      SimTime::FromNanoseconds(3'000),
      std::vector<NodeCommittedState>{MakeNode(0, 0.0), MakeNode(0, 1.0)});
  if(duplicate_result ||
     duplicate_result.error().code != ErrorCode::kAlreadyExists) {
    return EXIT_FAILURE;
  }

  const DeltaSet delta{
      PlanningCycleId{3},
      SnapshotVersion{7},
      SimTime::FromNanoseconds(4'000),
      std::vector<NodeStateReplacement>{NodeStateReplacement{MakeNode(1, 11.0)}}};
  if(delta.cycle_id != PlanningCycleId{3} ||
     delta.base_version != SnapshotVersion{7} ||
     delta.effective_at != SimTime::FromNanoseconds(4'000) ||
     delta.node_replacements.size() != 1 ||
     delta.node_replacements.front().state.node_id != NodeId{1}) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
