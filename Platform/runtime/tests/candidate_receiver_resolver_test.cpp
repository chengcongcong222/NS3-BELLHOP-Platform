#include <cstdint>
#include <cstdlib>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/node_capability.hpp>
#include <ns3_factory/contracts/state.hpp>

#include "internal/candidate_receiver_resolver.hpp"
#include "internal/cycle_working_state.hpp"

using namespace ns3_factory::contracts;
using ns3_factory::runtime::internal::CandidateReceiverResolver;
using ns3_factory::runtime::internal::CycleWorkingState;

namespace {

constexpr auto Node(std::uint64_t id, bool can_receive)
    -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, can_receive, DuplexMode::kFullDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, 0.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto TestCanonicalCapabilityOnlyFanOut() -> bool {
  const auto snapshot = WorldSnapshot::Create(
      SnapshotVersion{0},
      SimTime::Zero(),
      {Node(4, false), Node(3, true), Node(2, true), Node(1, true)});
  const auto working = snapshot
                           ? CycleWorkingState::Create(
                                 *snapshot,
                                 PlanningCycleId{0},
                                 SimTime::Zero())
                           : Result<CycleWorkingState>{
                                 std::unexpected(snapshot.error())};
  if(!snapshot || !working) {
    return false;
  }

  const CandidateReceiverResolver resolver;
  const auto first = resolver.Resolve(*working, NodeId{2});
  const auto second = resolver.Resolve(*working, NodeId{2});
  const std::vector<NodeId> expected{NodeId{1}, NodeId{3}};
  return first && second && *first == expected && *second == expected;
}

auto TestZeroCandidatesAndUnknownSender() -> bool {
  const auto snapshot = WorldSnapshot::Create(
      SnapshotVersion{0},
      SimTime::Zero(),
      {Node(0, true), Node(1, false)});
  const auto working = snapshot
                           ? CycleWorkingState::Create(
                                 *snapshot,
                                 PlanningCycleId{0},
                                 SimTime::Zero())
                           : Result<CycleWorkingState>{
                                 std::unexpected(snapshot.error())};
  if(!snapshot || !working) {
    return false;
  }

  const CandidateReceiverResolver resolver;
  const auto zero = resolver.Resolve(*working, NodeId{0});
  const auto unknown = resolver.Resolve(*working, NodeId{9});
  return zero && zero->empty() && !unknown &&
         unknown.error().code == ErrorCode::kNotFound;
}

}  // namespace

auto main() -> int {
  return TestCanonicalCapabilityOnlyFanOut() &&
                 TestZeroCandidatesAndUnknownSender()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
