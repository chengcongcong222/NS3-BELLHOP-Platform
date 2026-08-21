#pragma once

#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

namespace ns3_factory::planning::internal {

class MacPlanningResult final {
 public:
  [[nodiscard]] static auto Create(
      contracts::CycleTiming timing,
      std::vector<contracts::TxOpportunity> tx_opportunities)
      -> contracts::Result<MacPlanningResult>;

  [[nodiscard]] constexpr auto timing() const noexcept
      -> const contracts::CycleTiming& {
    return timing_;
  }

  [[nodiscard]] constexpr auto mac_plan() const noexcept
      -> const contracts::MacPlan& {
    return mac_plan_;
  }

  auto operator==(const MacPlanningResult&) const -> bool = default;

 private:
  MacPlanningResult(contracts::CycleTiming timing,
                    contracts::MacPlan mac_plan) noexcept
      : timing_(timing), mac_plan_(std::move(mac_plan)) {}

  contracts::CycleTiming timing_;
  contracts::MacPlan mac_plan_;
};

inline auto MacPlanningResult::Create(
    contracts::CycleTiming timing,
    std::vector<contracts::TxOpportunity> tx_opportunities)
    -> contracts::Result<MacPlanningResult> {
  auto mac_plan = contracts::MacPlan::Create(
      timing, std::move(tx_opportunities));
  if(!mac_plan) {
    return std::unexpected(mac_plan.error());
  }
  return MacPlanningResult{timing, std::move(*mac_plan)};
}

}  // namespace ns3_factory::planning::internal
