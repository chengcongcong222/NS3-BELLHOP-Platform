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
  auto selected =
      selector_.get().Select(opportunity.sender_node_id, queue_store);
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
    return NoRouteTxPreparation{};
  }

  return ReadyTxPreparation{
      std::move(**selected),
      std::get<ResolvedTransmissionTarget>(*resolution).target};
}

}  // namespace ns3_factory::runtime::internal
