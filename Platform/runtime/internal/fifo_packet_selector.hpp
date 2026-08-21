#pragma once

#include <optional>
#include <utility>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>

#include "internal/packet_queue_store.hpp"
#include "internal/packet_selector.hpp"

namespace ns3_factory::runtime::internal {

class FifoPacketSelector final : public IPacketSelector {
 public:
  [[nodiscard]] auto Select(
      contracts::NodeId sender,
      const PacketQueueStore& queue_store) const
      -> contracts::Result<std::optional<SelectedPacket>> override;
};

inline auto FifoPacketSelector::Select(
    contracts::NodeId sender,
    const PacketQueueStore& queue_store) const
    -> contracts::Result<std::optional<SelectedPacket>> {
  auto packet = queue_store.PeekFront(sender);
  if(!packet) {
    return std::unexpected(packet.error());
  }
  if(!*packet) {
    return std::nullopt;
  }
  return std::optional<SelectedPacket>{
      SelectedPacket{sender, std::move(**packet)}};
}

}  // namespace ns3_factory::runtime::internal
