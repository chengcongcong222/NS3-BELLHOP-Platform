#include <cstdlib>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>

#include "internal/cycle_coordinator.hpp"
#include "internal/event_dispatcher.hpp"
#include "internal/ns3_kernel_gateway.hpp"
#include "internal/plan_installer.hpp"

using ns3_factory::contracts::CycleTiming;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::ProtocolCyclePlan;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::Status;
using ns3_factory::contracts::TxOpportunity;
using ns3_factory::kernel::internal::CycleCoordinator;
using ns3_factory::kernel::internal::CycleCoordinatorState;
using ns3_factory::kernel::internal::EventDispatcher;
using ns3_factory::kernel::internal::EventKey;
using ns3_factory::kernel::internal::EventPhase;
using ns3_factory::kernel::internal::IPlanExecutionHook;
using ns3_factory::kernel::internal::Ns3KernelGateway;
using ns3_factory::kernel::internal::PlanInstaller;

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

class RecordingHook final : public IPlanExecutionHook {
 public:
  [[nodiscard]] auto OnTxStart(const TxOpportunity& opportunity,
                               SimTime now) -> Status override {
    if(now != opportunity.eligible_at) {
      return std::unexpected(
          ns3_factory::contracts::Error{
              ErrorCode::kFailedPrecondition,
              "RecordingHook observed a mismatched TxStart time"});
    }
    opportunities.push_back(opportunity);
    callback_times.push_back(now);
    return {};
  }

  [[nodiscard]] auto OnCycleClose(const CycleTiming& timing,
                                  SimTime now) -> Status override {
    if(now != timing.closes_at()) {
      return std::unexpected(
          ns3_factory::contracts::Error{
              ErrorCode::kFailedPrecondition,
              "RecordingHook observed a mismatched CycleClose time"});
    }
    ++close_count;
    close_time = now;
    return {};
  }

  std::vector<TxOpportunity> opportunities;
  std::vector<SimTime> callback_times;
  std::size_t close_count{0};
  SimTime close_time{SimTime::Zero()};
};

struct RunResult final {
  std::vector<TxOpportunity> opportunities;
  std::vector<EventKey> tx_keys;
  EventKey close_key;
};

auto RunCanonicalPlan(std::vector<TxOpportunity> input)
    -> ns3_factory::contracts::Result<RunResult> {
  const auto timing = CycleTiming::Create(
      PlanningCycleId{0}, SnapshotVersion{0}, At(0), At(100));
  if(!timing) {
    return std::unexpected(timing.error());
  }
  const auto plan = ProtocolCyclePlan::Create(*timing, std::move(input));
  if(!plan) {
    return std::unexpected(plan.error());
  }

  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway};
  PlanInstaller installer{dispatcher};
  RecordingHook hook;
  CycleCoordinator coordinator{installer, hook};
  const auto installed = coordinator.InstallPlan(*plan, SnapshotVersion{0});
  if(!installed) {
    gateway.Destroy();
    return std::unexpected(installed.error());
  }
  const auto run = dispatcher.Run();
  if(!run) {
    gateway.Destroy();
    return std::unexpected(run.error());
  }
  if(coordinator.state() != CycleCoordinatorState::kCompleted ||
     hook.close_count != 1 || hook.close_time != At(100)) {
    gateway.Destroy();
    return std::unexpected(
        ns3_factory::contracts::Error{
            ErrorCode::kFailedPrecondition,
            "Canonical plan did not complete exactly once"});
  }

  RunResult result{
      hook.opportunities, installed->tx_start_keys, installed->cycle_close_key};
  gateway.Destroy();
  return result;
}

auto TestCanonicalInstallAndExecutionOrdering() -> bool {
  const std::vector<TxOpportunity> first{
      TxOpportunity{NodeId{9}, At(50)},
      TxOpportunity{NodeId{3}, At(20)},
      TxOpportunity{NodeId{0}, At(20)}};
  const std::vector<TxOpportunity> second{first[2], first[0], first[1]};
  const auto first_run = RunCanonicalPlan(first);
  const auto second_run = RunCanonicalPlan(second);
  if(!first_run || !second_run) {
    return false;
  }

  const std::vector<TxOpportunity> expected{
      TxOpportunity{NodeId{0}, At(20)},
      TxOpportunity{NodeId{3}, At(20)},
      TxOpportunity{NodeId{9}, At(50)}};
  return first_run->opportunities == expected &&
         second_run->opportunities == expected &&
         first_run->tx_keys == second_run->tx_keys &&
         first_run->close_key == second_run->close_key &&
         first_run->tx_keys.size() == 3 &&
         first_run->tx_keys[0].time == At(20) &&
         first_run->tx_keys[0].phase == EventPhase::kTxStart &&
         first_run->tx_keys[1].time == At(20) &&
         first_run->tx_keys[1].phase == EventPhase::kTxStart &&
         first_run->tx_keys[2].time == At(50) &&
         first_run->close_key.time == At(100) &&
         first_run->close_key.phase == EventPhase::kCycleClose;
}

auto TestCoordinatorGuards() -> bool {
  const auto timing = CycleTiming::Create(
      PlanningCycleId{5}, SnapshotVersion{7}, At(0), At(30));
  const auto other_timing = CycleTiming::Create(
      PlanningCycleId{6}, SnapshotVersion{7}, At(0), At(30));
  if(!timing || !other_timing) {
    return false;
  }
  const auto plan = ProtocolCyclePlan::Create(
      *timing, {TxOpportunity{NodeId{1}, At(10)}});
  if(!plan) {
    return false;
  }

  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway};
  PlanInstaller installer{dispatcher};
  RecordingHook hook;
  CycleCoordinator coordinator{installer, hook};

  const auto mismatch = coordinator.InstallPlan(*plan, SnapshotVersion{8});
  const bool mismatch_ok =
      !mismatch && mismatch.error().code == ErrorCode::kFailedPrecondition &&
      coordinator.state() == CycleCoordinatorState::kIdle &&
      dispatcher.pending_event_count() == 0;
  const auto installed = coordinator.InstallPlan(*plan, SnapshotVersion{7});
  const auto double_install =
      coordinator.InstallPlan(*plan, SnapshotVersion{7});
  const auto forged_close = coordinator.OnCycleClose(*other_timing, At(30));
  if(!mismatch_ok || !installed || double_install ||
     double_install.error().code != ErrorCode::kAlreadyExists ||
     forged_close ||
     forged_close.error().code != ErrorCode::kFailedPrecondition ||
     coordinator.state() != CycleCoordinatorState::kActive ||
     !coordinator.installed_timing() ||
     *coordinator.installed_timing() != *timing) {
    gateway.Destroy();
    return false;
  }

  const auto run = dispatcher.Run();
  const auto double_close = coordinator.OnCycleClose(*timing, At(30));
  const auto completed_reuse =
      coordinator.InstallPlan(*plan, SnapshotVersion{7});
  const bool success =
      run && coordinator.state() == CycleCoordinatorState::kCompleted &&
      hook.opportunities ==
          std::vector<TxOpportunity>{TxOpportunity{NodeId{1}, At(10)}} &&
      hook.close_count == 1 && !double_close &&
      double_close.error().code == ErrorCode::kAlreadyExists &&
      !completed_reuse &&
      completed_reuse.error().code == ErrorCode::kAlreadyExists;
  gateway.Destroy();
  return success;
}

auto TestPlanInstallSequenceFailureIsAtomic() -> bool {
  const auto timing = CycleTiming::Create(
      PlanningCycleId{8}, SnapshotVersion{9}, At(0), At(100));
  if(!timing) {
    return false;
  }
  const auto plan = ProtocolCyclePlan::Create(
      *timing,
      {TxOpportunity{NodeId{1}, At(10)},
       TxOpportunity{NodeId{2}, At(20)},
       TxOpportunity{NodeId{3}, At(30)}});
  if(!plan) {
    return false;
  }

  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{
      gateway,
      ns3_factory::kernel::internal::EventSequenceId{
          std::numeric_limits<std::uint64_t>::max() - 1}};
  PlanInstaller installer{dispatcher};
  RecordingHook hook;
  CycleCoordinator coordinator{installer, hook};
  const auto pending_before = dispatcher.pending_event_count();
  const auto installed = coordinator.InstallPlan(*plan, SnapshotVersion{9});
  const auto pending_after = dispatcher.pending_event_count();
  const auto run = dispatcher.Run();
  const bool success =
      !installed && installed.error().code == ErrorCode::kOverflow &&
      coordinator.state() == CycleCoordinatorState::kIdle &&
      !coordinator.installed_timing() && pending_before == 0 &&
      pending_after == pending_before && run && hook.opportunities.empty() &&
      hook.close_count == 0;
  gateway.Destroy();
  return success;
}

auto TestFailedInstallThenValidRetry() -> bool {
  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway};
  PlanInstaller installer{dispatcher};
  RecordingHook hook;
  CycleCoordinator coordinator{installer, hook};

  const auto advance = gateway.ScheduleAt(At(5), []() {});
  if(!advance) {
    gateway.Destroy();
    return false;
  }
  gateway.Run();

  const auto stale_timing = CycleTiming::Create(
      PlanningCycleId{10}, SnapshotVersion{11}, At(0), At(30));
  const auto valid_timing = CycleTiming::Create(
      PlanningCycleId{10}, SnapshotVersion{11}, At(5), At(30));
  if(!stale_timing || !valid_timing) {
    gateway.Destroy();
    return false;
  }
  const auto stale_plan = ProtocolCyclePlan::Create(
      *stale_timing, {TxOpportunity{NodeId{0}, At(10)}});
  const auto valid_plan = ProtocolCyclePlan::Create(
      *valid_timing, {TxOpportunity{NodeId{0}, At(10)}});
  if(!stale_plan || !valid_plan) {
    gateway.Destroy();
    return false;
  }

  const auto pending_before = dispatcher.pending_event_count();
  const auto rejected =
      coordinator.InstallPlan(*stale_plan, SnapshotVersion{11});
  if(rejected || rejected.error().code != ErrorCode::kFailedPrecondition ||
     coordinator.state() != CycleCoordinatorState::kIdle ||
     coordinator.installed_timing() ||
     dispatcher.pending_event_count() != pending_before) {
    gateway.Destroy();
    return false;
  }

  const auto installed =
      coordinator.InstallPlan(*valid_plan, SnapshotVersion{11});
  if(!installed || coordinator.state() != CycleCoordinatorState::kActive ||
     dispatcher.pending_event_count() != 2) {
    gateway.Destroy();
    return false;
  }
  const auto run = dispatcher.Run();
  const bool success =
      run && coordinator.state() == CycleCoordinatorState::kCompleted &&
      hook.opportunities ==
          std::vector<TxOpportunity>{TxOpportunity{NodeId{0}, At(10)}} &&
      hook.close_count == 1 && hook.close_time == At(30);
  gateway.Destroy();
  return success;
}

}  // namespace

auto main() -> int {
  if(!TestCanonicalInstallAndExecutionOrdering() ||
     !TestCoordinatorGuards() ||
     !TestPlanInstallSequenceFailureIsAtomic() ||
     !TestFailedInstallThenValidRetry()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
