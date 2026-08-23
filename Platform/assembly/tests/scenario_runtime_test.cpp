#include <cstdlib>
#include <limits>
#include <vector>

#include "scenario_runtime_test_support.hpp"

using namespace ns3_factory::assembly::internal;
using namespace ns3_factory::assembly::test;

namespace {

auto TestArgumentPreflightAndTerminalState() -> bool {
  auto zero = RuntimeFixture::Create(PlanningCycleId{7}, {NodeId{0}});
  if(!zero) return false;
  const auto zero_result = zero->runtime.RunCycles(0);
  const auto zero_repeat = zero->runtime.RunCycles(1);
  if(zero_result || zero_result.error().code != ErrorCode::kInvalidArgument ||
     zero_repeat ||
     zero_repeat.error().code != ErrorCode::kFailedPrecondition ||
     zero->runtime.state() != ScenarioRuntimeState::kFailed ||
     zero->planner.build_count != 0 || !zero->estimator.observed_at.empty() ||
     zero->world.current_snapshot().version() != SnapshotVersion{0} ||
     zero->gateway.PlatformNow() != Result<SimTime>{SimTime::Zero()}) {
    return false;
  }

  constexpr auto kMaximumCycle =
      std::numeric_limits<PlanningCycleId::value_type>::max();
  auto overflow = RuntimeFixture::Create(
      PlanningCycleId{kMaximumCycle}, {NodeId{0}, NodeId{1}});
  if(!overflow) return false;
  const auto overflow_result = overflow->runtime.RunCycles(2);
  return !overflow_result &&
         overflow_result.error().code == ErrorCode::kOverflow &&
         overflow->runtime.state() == ScenarioRuntimeState::kFailed &&
         overflow->planner.build_count == 0 &&
         overflow->estimator.observed_at.empty() &&
         overflow->world.current_snapshot().version() == SnapshotVersion{0};
}

auto TestSuccessfulLifecycle() -> bool {
  auto fixture = RuntimeFixture::Create(PlanningCycleId{7}, {NodeId{0}});
  if(!fixture) return false;
  const auto run = fixture->runtime.RunCycles(1);
  const auto repeat = fixture->runtime.RunCycles(1);
  const auto now = fixture->gateway.PlatformNow();
  return run && !repeat &&
         repeat.error().code == ErrorCode::kFailedPrecondition &&
         fixture->runtime.state() == ScenarioRuntimeState::kCompleted &&
         fixture->planner.build_count == 1 &&
         fixture->planner.cycle_ids ==
             std::vector<PlanningCycleId>{PlanningCycleId{7}} &&
         fixture->planner.base_versions ==
             std::vector<SnapshotVersion>{SnapshotVersion{0}} &&
         fixture->planner.kernel_times ==
             std::vector<SimTime>{SimTime::Zero()} &&
         fixture->world.current_snapshot().version() == SnapshotVersion{1} &&
         fixture->world.current_snapshot().committed_at() == Seconds(10) &&
         fixture->world.last_committed_cycle_id() == PlanningCycleId{7} &&
         now && *now == SimTime::Zero();
}

auto TestMixedArrivalScenarioRuntime() -> bool {
  auto fixture = RuntimeFixture::Create(PlanningCycleId{20}, {NodeId{0}});
  const auto packet = TestPacket();
  if(!fixture || !fixture->Enqueue(packet)) return false;
  fixture->channel.no_arrival_receivers = {NodeId{2}};
  const auto run = fixture->runtime.RunCycles(1);
  const auto next_transmission = fixture->ids.NextTransmissionId();
  const auto next_reception = fixture->ids.NextReceptionId();
  return run && fixture->runtime.state() == ScenarioRuntimeState::kCompleted &&
         fixture->tx_phy.audit.size() == 1U &&
         fixture->channel.receiver_audit ==
             std::vector<NodeId>{NodeId{1}, NodeId{2}, NodeId{3}} &&
         fixture->noise.count == 2U && fixture->rx_phy.count == 2U &&
         fixture->noise.receiver_audit ==
             std::vector<NodeId>{NodeId{1}, NodeId{3}} &&
         fixture->rx_phy.receiver_audit ==
             std::vector<NodeId>{NodeId{1}, NodeId{3}} &&
         fixture->QueueHasOnly(NodeId{1}, packet) &&
         fixture->deliveries.size() == 0U &&
         fixture->world.current_snapshot().version() == SnapshotVersion{1} &&
         fixture->world.last_committed_cycle_id() == PlanningCycleId{20} &&
         next_transmission && *next_transmission == TransmissionId{101} &&
         next_reception && *next_reception == ReceptionId{1'002};
}

auto TestAllNoArrivalScenarioRuntime() -> bool {
  auto fixture = RuntimeFixture::Create(PlanningCycleId{30}, {NodeId{0}});
  const auto packet = TestPacket();
  if(!fixture || !fixture->Enqueue(packet)) return false;
  fixture->channel.no_arrival_receivers =
      {NodeId{1}, NodeId{2}, NodeId{3}};
  const auto run = fixture->runtime.RunCycles(1);
  const auto next_transmission = fixture->ids.NextTransmissionId();
  const auto next_reception = fixture->ids.NextReceptionId();
  return run && fixture->runtime.state() == ScenarioRuntimeState::kCompleted &&
         fixture->tx_phy.audit.size() == 1U &&
         fixture->channel.receiver_audit ==
             std::vector<NodeId>{NodeId{1}, NodeId{2}, NodeId{3}} &&
         fixture->noise.count == 0U && fixture->rx_phy.count == 0U &&
         fixture->noise.receiver_audit.empty() &&
         fixture->rx_phy.receiver_audit.empty() &&
         fixture->QueueHasOnly(std::nullopt, packet) &&
         fixture->deliveries.size() == 0U &&
         fixture->world.current_snapshot().version() == SnapshotVersion{1} &&
         fixture->world.last_committed_cycle_id() == PlanningCycleId{30} &&
         next_transmission && *next_transmission == TransmissionId{101} &&
         next_reception && *next_reception == ReceptionId{1'000};
}

auto TestKernelAheadOfSnapshotFailsBeforePlanning() -> bool {
  auto fixture = RuntimeFixture::Create(PlanningCycleId{0}, {NodeId{0}});
  if(!fixture) return false;
  const auto scheduled = fixture->gateway.ScheduleAt(Seconds(1), [] {});
  if(!scheduled) return false;
  fixture->gateway.Run();
  const auto before = fixture->gateway.PlatformNow();
  const auto run = fixture->runtime.RunCycles(1);
  const auto after = fixture->gateway.PlatformNow();
  return before && *before == Seconds(1) && !run &&
         run.error().code == ErrorCode::kFailedPrecondition &&
         fixture->runtime.state() == ScenarioRuntimeState::kFailed &&
         fixture->planner.build_count == 0 &&
         fixture->estimator.observed_at.empty() && after &&
         *after == SimTime::Zero();
}

auto TestZeroDelayRemainsFatalWithoutTimeShift() -> bool {
  auto fixture = RuntimeFixture::Create(PlanningCycleId{0}, {NodeId{0}});
  const auto packet = TestPacket();
  if(!fixture || !fixture->Enqueue(packet)) return false;
  fixture->channel.propagation_delay = SimDuration::Zero();
  const auto run = fixture->runtime.RunCycles(1);
  const auto now = fixture->gateway.PlatformNow();
  return !run &&
         run.error().code == ErrorCode::kFailedPrecondition &&
         fixture->runtime.state() == ScenarioRuntimeState::kFailed &&
         fixture->planner.build_count == 1 &&
         fixture->tx_phy.audit.size() == 1 &&
         fixture->world.current_snapshot().version() == SnapshotVersion{0} &&
         fixture->world.current_snapshot().committed_at() == Seconds(0) &&
         fixture->QueueHasOnly(NodeId{0}, packet) &&
         fixture->deliveries.size() == 0 && now &&
         *now == SimTime::Zero();
}

auto TestFailureStopsAndPreservesSuccessfulPrefix() -> bool {
  auto fixture = RuntimeFixture::Create(
      PlanningCycleId{0},
      {NodeId{0}, NodeId{1}, NodeId{2}},
      FeasibilityMode::kHysteresisFailureCandidate,
      2);
  const auto packet = TestPacket();
  if(!fixture || !fixture->Enqueue(packet)) return false;
  const auto run = fixture->runtime.RunCycles(3);
  if(run || run.error().code != ErrorCode::kInternal ||
     fixture->runtime.state() != ScenarioRuntimeState::kFailed ||
     fixture->planner.build_count != 2 ||
     fixture->planner.connectivity_graphs.size() != 2 ||
     fixture->planner.kernel_times !=
         std::vector<SimTime>{Seconds(0), Seconds(10)} ||
     fixture->estimator.observed_at.size() != 24 ||
     fixture->tx_phy.audit.size() != 1 ||
     fixture->world.current_snapshot().version() != SnapshotVersion{1} ||
     fixture->world.current_snapshot().committed_at() != Seconds(10) ||
     fixture->world.last_committed_cycle_id() != PlanningCycleId{0} ||
     !fixture->QueueHasOnly(NodeId{1}, packet) ||
     fixture->deliveries.size() != 0) {
    return false;
  }

  const auto& cycle_zero = fixture->planner.connectivity_graphs[0];
  const auto& failed_candidate = fixture->planner.connectivity_graphs[1];
  const auto& retained = fixture->runtime.previous_connectivity();
  if(!cycle_zero.HasEdge(NodeId{0}, NodeId{1}) ||
     cycle_zero.HasEdge(NodeId{0}, NodeId{2}) ||
     !failed_candidate.HasEdge(NodeId{0}, NodeId{1}) ||
     !failed_candidate.HasEdge(NodeId{0}, NodeId{2}) || !retained ||
     *retained != cycle_zero || retained->HasEdge(NodeId{0}, NodeId{2})) {
    return false;
  }

  const auto repeat = fixture->runtime.RunCycles(1);
  const auto now = fixture->gateway.PlatformNow();
  return !repeat &&
         repeat.error().code == ErrorCode::kFailedPrecondition &&
         fixture->planner.build_count == 2 &&
         fixture->estimator.observed_at.size() == 24 &&
         fixture->tx_phy.audit.size() == 1 && now &&
         *now == SimTime::Zero();
}

}  // namespace

auto main() -> int {
  return TestArgumentPreflightAndTerminalState() &&
                 TestSuccessfulLifecycle() &&
                 TestMixedArrivalScenarioRuntime() &&
                 TestAllNoArrivalScenarioRuntime() &&
                 TestKernelAheadOfSnapshotFailsBeforePlanning() &&
                 TestZeroDelayRemainsFatalWithoutTimeShift() &&
                 TestFailureStopsAndPreservesSuccessfulPrefix()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
