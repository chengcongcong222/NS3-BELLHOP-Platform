#pragma once

#include <cstddef>
#include <vector>

#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/identity.hpp>

namespace ns3_factory::contracts {

struct DigitalPacket final {
  PacketId packet_id;
  NodeId source_node_id;
  // End-to-end logical destination, independent of any current-hop target.
  PacketDestination destination;
  std::vector<std::byte> payload;

  auto operator==(const DigitalPacket&) const -> bool = default;
};

}  // namespace ns3_factory::contracts
