#pragma once

#include <variant>

#include <ns3_factory/contracts/identity.hpp>

namespace ns3_factory::contracts {

struct UnicastTransmissionTarget final {
  NodeId node_id;

  constexpr auto operator==(const UnicastTransmissionTarget&) const
      -> bool = default;
};

struct BroadcastTransmissionTarget final {
  constexpr auto operator==(const BroadcastTransmissionTarget&) const
      -> bool = default;
};

using TransmissionTarget =
    std::variant<UnicastTransmissionTarget, BroadcastTransmissionTarget>;

}  // namespace ns3_factory::contracts
