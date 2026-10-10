#pragma once

#include <functional>
#include <optional>
#include <utility>
#include <variant>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/routing.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

#include "internal/packet_queue_store.hpp"
#include "internal/packet_selector.hpp"
#include "internal/transmission_target_resolver.hpp"

namespace ns3_factory::runtime::internal {

struct ReadyTxPreparation final {
  SelectedPacket selected_packet;
  contracts::TransmissionTarget target;

  auto operator==(const ReadyTxPreparation&) const -> bool = default;
};

struct NoPacketTxPreparation final {
  constexpr auto operator==(const NoPacketTxPreparation&) const noexcept
      -> bool = default;
};

struct NoRouteTxPreparation final {
  constexpr auto operator==(const NoRouteTxPreparation&) const noexcept
      -> bool = default;
};

using TxPreparationOutcome =
    std::variant<ReadyTxPreparation,
                 NoPacketTxPreparation,
                 NoRouteTxPreparation>;

class TxPreparationService final {
 public:
  // The caller-owned synchronous selector must outlive this service.
  explicit TxPreparationService(const IPacketSelector& selector) noexcept
      : selector_(selector) {}

  [[nodiscard]] auto Prepare(
      const contracts::TxOpportunity& opportunity,
      const PacketQueueStore& queue_store,
      const std::optional<contracts::RoutingPlan>& routing_plan) const
      -> contracts::Result<TxPreparationOutcome>;

 private:
  std::reference_wrapper<const IPacketSelector> selector_;
  TransmissionTargetResolver target_resolver_;
};

inline auto TxPreparationService::Prepare(
    const contracts::TxOpportunity& opportunity,
    const PacketQueueStore& queue_store,
    const std::optional<contracts::RoutingPlan>& routing_plan) const
    -> contracts::Result<TxPreparationOutcome> {
  auto selected = [&]() -> contracts::Result<std::optional<SelectedPacket>> {
    if(!opportunity.flow_selection) return selector_.get().Select(opportunity.sender_node_id, queue_store);
    auto packet = queue_store.PeekFlow(opportunity.sender_node_id, opportunity.flow_selection->flow_id);
    if(!packet) return std::unexpected(packet.error());
    if(!*packet) return std::nullopt;
    return std::optional{SelectedPacket{opportunity.sender_node_id, std::move(**packet)}};
  }();
  if(!selected) {
    return std::unexpected(selected.error());
  }
  if(!*selected) {
    return NoPacketTxPreparation{};
  }

  auto resolution =
      target_resolver_.Resolve(opportunity, **selected, routing_plan);
  if(!resolution) {
    return std::unexpected(resolution.error());
  }
  if(std::holds_alternative<NoRouteResolution>(*resolution)) {
    if(opportunity.flow_selection) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition, "PlanningExecutionMismatch: frozen flow route disappeared"});
    return NoRouteTxPreparation{};
  }

  if(const auto& constraint = opportunity.flow_selection) {
    const auto* destination = std::get_if<contracts::UnicastDestination>(&(**selected).packet.destination);
    const auto& resolved = std::get<ResolvedTransmissionTarget>(*resolution).target;
    const auto* hop = std::get_if<contracts::UnicastTransmissionTarget>(&resolved);
    if(!destination || !hop || destination->node_id != constraint->destination_node_id ||
       hop->node_id != constraint->next_hop_node_id || !routing_plan ||
       routing_plan->cycle_id() != constraint->cycle_id || routing_plan->base_snapshot_version() != constraint->snapshot_version)
      return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
          "PlanningExecutionMismatch: flow or frozen route mismatch"});
  }

  return ReadyTxPreparation{
      std::move(**selected),
      std::get<ResolvedTransmissionTarget>(*resolution).target};
}

}  // namespace ns3_factory::runtime::internal
