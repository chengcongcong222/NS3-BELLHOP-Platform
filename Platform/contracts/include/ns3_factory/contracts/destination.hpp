#pragma once

#include <variant>

#include <ns3_factory/contracts/identity.hpp>

namespace ns3_factory::contracts {

struct UnicastDestination final {
  NodeId node_id;

  constexpr auto operator==(const UnicastDestination&) const
      -> bool = default;
};

struct BroadcastDestination final {
  constexpr auto operator==(const BroadcastDestination&) const
      -> bool = default;
};

using PacketDestination =
    std::variant<UnicastDestination, BroadcastDestination>;

}  // namespace ns3_factory::contracts
