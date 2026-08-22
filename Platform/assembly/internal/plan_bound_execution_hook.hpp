#pragma once

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

#include "internal/cycle_signal_runtime.hpp"
#include "internal/plan_bound_tx_runtime.hpp"
#include "internal/plan_installer.hpp"

namespace ns3_factory::assembly::internal {

class PlanBoundExecutionHook final
    : public kernel::internal::IPlanExecutionHook {
 public:
  PlanBoundExecutionHook(
      runtime::internal::PlanBoundTxRuntime& tx_runtime,
      runtime::internal::CycleSignalRuntime& signal_runtime) noexcept
      : tx_runtime_(tx_runtime), signal_runtime_(signal_runtime) {}

  [[nodiscard]] auto OnTxStart(
      const contracts::TxOpportunity& opportunity,
      contracts::SimTime now) -> contracts::Status override {
    auto outcome = tx_runtime_.HandleTxStart(opportunity, now);
    if(!outcome) return std::unexpected(outcome.error());
    return {};
  }

  [[nodiscard]] auto OnCycleClose(
      const contracts::CycleTiming&,
      contracts::SimTime now) -> contracts::Status override {
    return signal_runtime_.HandleCycleClose(now);
  }

 private:
  runtime::internal::PlanBoundTxRuntime& tx_runtime_;
  runtime::internal::CycleSignalRuntime& signal_runtime_;
};

}  // namespace ns3_factory::assembly::internal
