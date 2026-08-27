#pragma once

#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

namespace ns3_factory::assembly::internal {

struct AppliedTxSlot final {
  contracts::NodeId sender_node_id;
  contracts::SimDuration offset_from_cycle_start;

  constexpr auto operator==(const AppliedTxSlot&) const noexcept
      -> bool = default;
};

// Value-owned execution template captured only on a network-update cycle.
// Bind preserves applied routing and slot semantics while reconstructing all
// absolute times and provenance for the current cycle.
class AppliedMacSchedule final {
 public:
  [[nodiscard]] static auto Capture(
      const contracts::ProtocolCyclePlan& candidate_plan)
      -> contracts::Result<AppliedMacSchedule>;

  [[nodiscard]] auto Bind(
      const contracts::StructureSnapshot& current_structure,
      contracts::SimTime current_cycle_start) const
      -> contracts::Result<contracts::ProtocolCyclePlan>;

  [[nodiscard]] constexpr auto cycle_duration() const noexcept
      -> contracts::SimDuration {
    return cycle_duration_;
  }

  [[nodiscard]] auto slots() const noexcept
      -> std::span<const AppliedTxSlot> {
    return std::span<const AppliedTxSlot>{slots_};
  }

 private:
  AppliedMacSchedule(contracts::SimDuration cycle_duration,
                     std::vector<AppliedTxSlot> slots,
                     std::optional<std::vector<contracts::RouteEntry>>
                         routing_entries) noexcept
      : cycle_duration_(cycle_duration),
        slots_(std::move(slots)),
        routing_entries_(std::move(routing_entries)) {}

  contracts::SimDuration cycle_duration_;
  std::vector<AppliedTxSlot> slots_;
  std::optional<std::vector<contracts::RouteEntry>> routing_entries_;
};

inline auto AppliedMacSchedule::Capture(
    const contracts::ProtocolCyclePlan& candidate_plan)
    -> contracts::Result<AppliedMacSchedule> {
  const auto& timing = candidate_plan.timing();
  const auto cycle_duration =
      contracts::CheckedSubtract(timing.closes_at(), timing.starts_at());
  if(!cycle_duration ||
     *cycle_duration <= contracts::SimDuration::Zero()) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Applied schedule candidate has invalid cycle duration"});
  }
  std::vector<AppliedTxSlot> slots;
  const auto opportunities = candidate_plan.mac_plan().tx_opportunities();
  slots.reserve(opportunities.size());
  for(const auto& opportunity : opportunities) {
    const auto offset = contracts::CheckedSubtract(
        opportunity.eligible_at, timing.starts_at());
    if(!offset || *offset < contracts::SimDuration::Zero()) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kFailedPrecondition,
              "Applied schedule candidate contains an invalid slot offset"});
    }
    slots.push_back(AppliedTxSlot{opportunity.sender_node_id, *offset});
  }
  std::optional<std::vector<contracts::RouteEntry>> routing_entries;
  if(const auto& routing = candidate_plan.routing_plan(); routing) {
    const auto entries = routing->entries();
    routing_entries.emplace(entries.begin(), entries.end());
  }
  return AppliedMacSchedule{
      *cycle_duration, std::move(slots), std::move(routing_entries)};
}

inline auto AppliedMacSchedule::Bind(
    const contracts::StructureSnapshot& current_structure,
    contracts::SimTime current_cycle_start) const
    -> contracts::Result<contracts::ProtocolCyclePlan> {
  const auto closes_at = contracts::CheckedAdd(
      current_cycle_start, cycle_duration_);
  if(!closes_at) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Applied schedule cycle close time overflowed"});
  }
  auto timing = contracts::CycleTiming::Create(
      current_structure.cycle_id(),
      current_structure.base_snapshot_version(),
      current_cycle_start,
      *closes_at);
  if(!timing) return std::unexpected(timing.error());

  std::vector<contracts::TxOpportunity> opportunities;
  opportunities.reserve(slots_.size());
  for(const auto& slot : slots_) {
    const auto eligible_at = contracts::CheckedAdd(
        timing->starts_at(), slot.offset_from_cycle_start);
    if(!eligible_at) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kOverflow,
              "Applied schedule TxOpportunity time overflowed"});
    }
    opportunities.push_back(
        contracts::TxOpportunity{slot.sender_node_id, *eligible_at});
  }
  auto mac_plan = contracts::MacPlan::Create(*timing,
                                             std::move(opportunities));
  if(!mac_plan) return std::unexpected(mac_plan.error());
  if(routing_entries_) {
    auto routing = contracts::RoutingPlan::Create(
        *routing_entries_, current_structure);
    if(!routing) return std::unexpected(routing.error());
    return contracts::ProtocolCyclePlan::Create(
        std::move(*routing), *timing, std::move(*mac_plan));
  }
  const auto rebound_opportunities = mac_plan->tx_opportunities();
  return contracts::ProtocolCyclePlan::Create(
      *timing,
      std::vector<contracts::TxOpportunity>{rebound_opportunities.begin(),
                                            rebound_opportunities.end()});
}

}  // namespace ns3_factory::assembly::internal
