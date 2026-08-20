#pragma once

#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>

#include "internal/event_dispatcher.hpp"

namespace ns3_factory::kernel::internal {

class IPlanExecutionHook {
 public:
  virtual ~IPlanExecutionHook() = default;

  [[nodiscard]] virtual auto OnTxStart(
      const contracts::TxOpportunity& opportunity,
      contracts::SimTime now) -> contracts::Status = 0;

  [[nodiscard]] virtual auto OnCycleClose(
      const contracts::CycleTiming& timing,
      contracts::SimTime now) -> contracts::Status = 0;
};

struct InstalledPlanEvents final {
  std::vector<EventKey> tx_start_keys;
  EventKey cycle_close_key;
};

class PlanInstaller final {
 public:
  explicit PlanInstaller(EventDispatcher& dispatcher) noexcept
      : dispatcher_(dispatcher) {}

  [[nodiscard]] auto Install(const contracts::ProtocolCyclePlan& plan,
                             IPlanExecutionHook& hook)
      -> contracts::Result<InstalledPlanEvents>;

 private:
  EventDispatcher& dispatcher_;
};

inline auto PlanInstaller::Install(const contracts::ProtocolCyclePlan& plan,
                                   IPlanExecutionHook& hook)
    -> contracts::Result<InstalledPlanEvents> {
  const auto install_now = dispatcher_.PlatformNow();
  if(!install_now) {
    return std::unexpected(install_now.error());
  }
  if(*install_now > plan.timing().starts_at()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "ProtocolCyclePlan starts before PlatformNow"});
  }

  const auto opportunities = plan.mac_plan().tx_opportunities();
  std::vector<ScheduledEventIntent> intents;
  intents.reserve(opportunities.size() + 1);
  auto* const dispatcher = &dispatcher_;
  auto* const execution_hook = &hook;
  for(const auto& opportunity : opportunities) {
    intents.push_back(ScheduledEventIntent{
        opportunity.eligible_at,
        EventPhase::kTxStart,
        [dispatcher, execution_hook, opportunity]() -> contracts::Status {
          const auto now = dispatcher->PlatformNow();
          if(!now) {
            return std::unexpected(now.error());
          }
          return execution_hook->OnTxStart(opportunity, *now);
        }});
  }

  const auto timing = plan.timing();
  intents.push_back(ScheduledEventIntent{
      timing.closes_at(),
      EventPhase::kCycleClose,
      [dispatcher, execution_hook, timing]() -> contracts::Status {
        const auto now = dispatcher->PlatformNow();
        if(!now) {
          return std::unexpected(now.error());
        }
        return execution_hook->OnCycleClose(timing, *now);
      }});

  auto keys = dispatcher_.ScheduleBatch(std::move(intents));
  if(!keys) {
    return std::unexpected(keys.error());
  }

  std::vector<EventKey> tx_start_keys;
  tx_start_keys.reserve(opportunities.size());
  tx_start_keys.insert(tx_start_keys.end(),
                       keys->begin(),
                       keys->end() - 1);
  return InstalledPlanEvents{std::move(tx_start_keys), keys->back()};
}

}  // namespace ns3_factory::kernel::internal
