#pragma once

#include <optional>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>

#include "internal/plan_installer.hpp"

namespace ns3_factory::kernel::internal {

enum class CycleCoordinatorState {
  kIdle,
  kActive,
  kCompleted,
};

class CycleCoordinator final : public IPlanExecutionHook {
 public:
  CycleCoordinator(PlanInstaller& installer,
                   IPlanExecutionHook& runtime_hook) noexcept
      : installer_(installer), runtime_hook_(runtime_hook) {}

  CycleCoordinator(const CycleCoordinator&) = delete;
  auto operator=(const CycleCoordinator&) -> CycleCoordinator& = delete;
  CycleCoordinator(CycleCoordinator&&) = delete;
  auto operator=(CycleCoordinator&&) -> CycleCoordinator& = delete;

  [[nodiscard]] auto InstallPlan(
      const contracts::ProtocolCyclePlan& plan,
      contracts::SnapshotVersion authoritative_snapshot_version)
      -> contracts::Result<InstalledPlanEvents>;

  [[nodiscard]] auto OnTxStart(
      const contracts::TxOpportunity& opportunity,
      contracts::SimTime now) -> contracts::Status override;

  [[nodiscard]] auto OnCycleClose(
      const contracts::CycleTiming& timing,
      contracts::SimTime now) -> contracts::Status override;

  [[nodiscard]] constexpr auto state() const noexcept
      -> CycleCoordinatorState {
    return state_;
  }

  [[nodiscard]] constexpr auto installed_timing() const noexcept
      -> const std::optional<contracts::CycleTiming>& {
    return installed_timing_;
  }

 private:
  PlanInstaller& installer_;
  IPlanExecutionHook& runtime_hook_;
  CycleCoordinatorState state_{CycleCoordinatorState::kIdle};
  std::optional<contracts::CycleTiming> installed_timing_;
};

inline auto CycleCoordinator::InstallPlan(
    const contracts::ProtocolCyclePlan& plan,
    contracts::SnapshotVersion authoritative_snapshot_version)
    -> contracts::Result<InstalledPlanEvents> {
  if(state_ != CycleCoordinatorState::kIdle) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kAlreadyExists,
                         "CycleCoordinator already installed a plan"});
  }
  if(plan.timing().base_snapshot_version() !=
     authoritative_snapshot_version) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "Plan base version does not match authoritative "
                         "snapshot"});
  }

  auto installed = installer_.Install(plan, *this);
  if(!installed) {
    return std::unexpected(installed.error());
  }
  installed_timing_ = plan.timing();
  state_ = CycleCoordinatorState::kActive;
  return installed;
}

inline auto CycleCoordinator::OnTxStart(
    const contracts::TxOpportunity& opportunity,
    contracts::SimTime now) -> contracts::Status {
  if(state_ != CycleCoordinatorState::kActive || !installed_timing_) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "TxStart requires an active planning cycle"});
  }
  if(now != opportunity.eligible_at) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "TxStart time does not match TxOpportunity"});
  }
  return runtime_hook_.OnTxStart(opportunity, now);
}

inline auto CycleCoordinator::OnCycleClose(
    const contracts::CycleTiming& timing,
    contracts::SimTime now) -> contracts::Status {
  if(state_ == CycleCoordinatorState::kCompleted) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kAlreadyExists,
                         "Planning cycle was already completed"});
  }
  if(state_ != CycleCoordinatorState::kActive || !installed_timing_) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "CycleClose requires an active planning cycle"});
  }
  if(timing != *installed_timing_ || now != timing.closes_at()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "CycleClose timing does not match installed plan"});
  }

  const auto status = runtime_hook_.OnCycleClose(timing, now);
  if(!status) {
    return status;
  }
  state_ = CycleCoordinatorState::kCompleted;
  return {};
}

}  // namespace ns3_factory::kernel::internal
