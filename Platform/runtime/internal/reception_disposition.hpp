#pragma once

#include <utility>
#include <variant>

#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/packet.hpp>

namespace ns3_factory::runtime::internal {

struct NotDecodedReception final {
  contracts::ReceptionId reception_id;
  contracts::TransmissionId transmission_id;
  contracts::NodeId receiver_node_id;

  constexpr auto operator==(const NotDecodedReception&) const noexcept
      -> bool = default;
};

struct OverheardReception final {
  contracts::ReceptionId reception_id;
  contracts::TransmissionId transmission_id;
  contracts::NodeId receiver_node_id;

  constexpr auto operator==(const OverheardReception&) const noexcept
      -> bool = default;
};

struct LocalDeliveryReception final {
  contracts::ReceptionId reception_id;
  contracts::TransmissionId transmission_id;
  contracts::NodeId receiver_node_id;
  contracts::DigitalPacket packet;

  auto operator==(const LocalDeliveryReception&) const -> bool = default;
};

struct RelayEnqueueReception final {
  contracts::ReceptionId reception_id;
  contracts::TransmissionId transmission_id;
  contracts::NodeId receiver_node_id;
  contracts::DigitalPacket packet;

  auto operator==(const RelayEnqueueReception&) const -> bool = default;
};

using ReceptionDisposition =
    std::variant<NotDecodedReception,
                 OverheardReception,
                 LocalDeliveryReception,
                 RelayEnqueueReception>;

}  // namespace ns3_factory::runtime::internal
