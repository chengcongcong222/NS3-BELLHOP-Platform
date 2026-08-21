#pragma once

#include <optional>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/packet.hpp>

namespace ns3_factory::runtime::internal {

class PacketQueueStore;

struct SelectedPacket final {
  contracts::NodeId queue_owner;
  contracts::DigitalPacket packet;

  auto operator==(const SelectedPacket&) const -> bool = default;
};

class IPacketSelector {
 public:
  virtual ~IPacketSelector() = default;

  [[nodiscard]] virtual auto Select(
      contracts::NodeId sender,
      const PacketQueueStore& queue_store) const
      -> contracts::Result<std::optional<SelectedPacket>> = 0;
};

}  // namespace ns3_factory::runtime::internal
