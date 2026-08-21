#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

namespace ns3_factory::contracts {

class CycleTiming final {
 public:
  [[nodiscard]] static auto Create(PlanningCycleId cycle_id,
                                   SnapshotVersion base_snapshot_version,
                                   SimTime starts_at,
                                   SimTime closes_at)
      -> Result<CycleTiming> {
    if(starts_at >= closes_at) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "CycleTiming requires starts_at before closes_at"});
    }
    return CycleTiming{
        cycle_id, base_snapshot_version, starts_at, closes_at};
  }

  [[nodiscard]] constexpr auto cycle_id() const noexcept -> PlanningCycleId {
    return cycle_id_;
  }

  [[nodiscard]] constexpr auto base_snapshot_version() const noexcept
      -> SnapshotVersion {
    return base_snapshot_version_;
  }

  [[nodiscard]] constexpr auto starts_at() const noexcept -> SimTime {
    return starts_at_;
  }

  [[nodiscard]] constexpr auto closes_at() const noexcept -> SimTime {
    return closes_at_;
  }

  constexpr auto operator==(const CycleTiming&) const noexcept
      -> bool = default;

 private:
  constexpr CycleTiming(PlanningCycleId cycle_id,
                        SnapshotVersion base_snapshot_version,
                        SimTime starts_at,
                        SimTime closes_at) noexcept
      : cycle_id_(cycle_id),
        base_snapshot_version_(base_snapshot_version),
        starts_at_(starts_at),
        closes_at_(closes_at) {}

  PlanningCycleId cycle_id_;
  SnapshotVersion base_snapshot_version_;
  SimTime starts_at_;
  SimTime closes_at_;
};

class MacPlan final {
 public:
  [[nodiscard]] static auto Create(
      const CycleTiming& timing,
      std::vector<TxOpportunity> tx_opportunities) -> Result<MacPlan>;

  [[nodiscard]] auto tx_opportunities() const noexcept
      -> std::span<const TxOpportunity> {
    return std::span<const TxOpportunity>{tx_opportunities_};
  }

  auto operator==(const MacPlan&) const -> bool = default;

 private:
  friend class ProtocolCyclePlan;

  explicit MacPlan(std::vector<TxOpportunity> tx_opportunities) noexcept
      : tx_opportunities_(std::move(tx_opportunities)) {}

  std::vector<TxOpportunity> tx_opportunities_;
};

inline auto MacPlan::Create(
    const CycleTiming& timing,
    std::vector<TxOpportunity> tx_opportunities) -> Result<MacPlan> {
  std::sort(
      tx_opportunities.begin(),
      tx_opportunities.end(),
      [](const TxOpportunity& lhs, const TxOpportunity& rhs) {
        if(lhs.eligible_at != rhs.eligible_at) {
          return lhs.eligible_at < rhs.eligible_at;
        }
        return lhs.sender_node_id < rhs.sender_node_id;
      });

  for(const auto& opportunity : tx_opportunities) {
    if(opportunity.eligible_at < timing.starts_at() ||
       opportunity.eligible_at >= timing.closes_at()) {
      return std::unexpected(
          Error{ErrorCode::kOutOfRange,
                "TxOpportunity must be within [starts_at, closes_at)"});
    }
  }

  for(std::size_t index = 1; index < tx_opportunities.size(); ++index) {
    const auto& previous = tx_opportunities[index - 1];
    const auto& current = tx_opportunities[index];
    if(previous.eligible_at == current.eligible_at &&
       previous.sender_node_id == current.sender_node_id) {
      return std::unexpected(
          Error{ErrorCode::kAlreadyExists,
                "MacPlan contains duplicate sender/time TxOpportunity"});
    }
  }

  return MacPlan{std::move(tx_opportunities)};
}

class ProtocolCyclePlan final {
 public:
  [[nodiscard]] static auto Create(
      CycleTiming timing,
      std::vector<TxOpportunity> tx_opportunities)
      -> Result<ProtocolCyclePlan>;

  [[nodiscard]] constexpr auto timing() const noexcept
      -> const CycleTiming& {
    return timing_;
  }

  [[nodiscard]] constexpr auto mac_plan() const noexcept
      -> const MacPlan& {
    return mac_plan_;
  }

  auto operator==(const ProtocolCyclePlan&) const -> bool = default;

 private:
  ProtocolCyclePlan(CycleTiming timing, MacPlan mac_plan) noexcept
      : timing_(timing), mac_plan_(std::move(mac_plan)) {}

  CycleTiming timing_;
  MacPlan mac_plan_;
};

inline auto ProtocolCyclePlan::Create(
    CycleTiming timing,
    std::vector<TxOpportunity> tx_opportunities)
    -> Result<ProtocolCyclePlan> {
  auto mac_plan = MacPlan::Create(timing, std::move(tx_opportunities));
  if(!mac_plan) {
    return std::unexpected(mac_plan.error());
  }

  return ProtocolCyclePlan{timing, std::move(*mac_plan)};
}

}  // namespace ns3_factory::contracts
