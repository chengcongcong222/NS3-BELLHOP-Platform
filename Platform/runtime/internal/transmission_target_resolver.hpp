#pragma once

#include <optional>
#include <variant>

#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/routing.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

#include "internal/packet_selector.hpp"

namespace ns3_factory::runtime::internal {

struct ResolvedTransmissionTarget final {
  contracts::TransmissionTarget target;

  auto operator==(const ResolvedTransmissionTarget&) const
      -> bool = default;
};

struct NoRouteResolution final {
  constexpr auto operator==(const NoRouteResolution&) const noexcept
      -> bool = default;
};

using TransmissionTargetResolution =
    std::variant<ResolvedTransmissionTarget, NoRouteResolution>;

class TransmissionTargetResolver final {
 public:
  [[nodiscard]] auto Resolve(
      const contracts::TxOpportunity& opportunity,
      const SelectedPacket& selected_packet,
      const std::optional<contracts::RoutingPlan>& routing_plan) const
      -> contracts::Result<TransmissionTargetResolution>;
};

inline auto TransmissionTargetResolver::Resolve(
    const contracts::TxOpportunity& opportunity,
    const SelectedPacket& selected_packet,
    const std::optional<contracts::RoutingPlan>& routing_plan) const
    -> contracts::Result<TransmissionTargetResolution> {
  if(selected_packet.queue_owner != opportunity.sender_node_id) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Selected packet queue owner does not match TxOpportunity "
            "sender"});
  }

  if(std::holds_alternative<contracts::BroadcastDestination>(
         selected_packet.packet.destination)) {
    return ResolvedTransmissionTarget{
        contracts::BroadcastTransmissionTarget{}};
  }

  const auto destination =
      std::get<contracts::UnicastDestination>(
          selected_packet.packet.destination)
          .node_id;
  if(destination == opportunity.sender_node_id) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Locally destined packet cannot enter outgoing transmission"});
  }
  if(!routing_plan) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Unicast transmission requires a RoutingPlan binding"});
  }

  const auto next_hop = routing_plan->FindNextHop(
      opportunity.sender_node_id, destination);
  if(!next_hop) {
    return NoRouteResolution{};
  }
  return ResolvedTransmissionTarget{
      contracts::UnicastTransmissionTarget{*next_hop}};
}

}  // namespace ns3_factory::runtime::internal
