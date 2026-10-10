#pragma once

#include <cstddef>
#include <vector>
#include <optional>

#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/identity.hpp>

namespace ns3_factory::contracts {

struct DigitalPacket final {
  PacketId packet_id;
  NodeId source_node_id;
  // End-to-end logical destination, independent of any current-hop target.
  PacketDestination destination;
  std::vector<std::byte> payload;
  // Global-simulation metadata, never serialized into acoustic payload bytes.
  // Absent for all legacy packets. Relay/recovered-payload copies preserve it.
  std::optional<FlowId> flow_id{};

  auto operator==(const DigitalPacket&) const -> bool = default;
};

}  // namespace ns3_factory::contracts
