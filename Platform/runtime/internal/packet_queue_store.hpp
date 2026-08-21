#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/packet.hpp>

namespace ns3_factory::runtime::internal {

class PacketQueueStore final {
 public:
  [[nodiscard]] static auto Create(std::vector<contracts::NodeId> node_ids)
      -> contracts::Result<PacketQueueStore>;

  PacketQueueStore(const PacketQueueStore&) = delete;
  auto operator=(const PacketQueueStore&) -> PacketQueueStore& = delete;
  PacketQueueStore(PacketQueueStore&&) noexcept = default;
  auto operator=(PacketQueueStore&&) noexcept
      -> PacketQueueStore& = default;

  [[nodiscard]] auto Enqueue(contracts::NodeId queue_owner,
                             contracts::DigitalPacket packet)
      -> contracts::Status;

  [[nodiscard]] auto PeekFront(contracts::NodeId queue_owner) const
      -> contracts::Result<std::optional<contracts::DigitalPacket>>;

  [[nodiscard]] auto ConsumeFront(
      contracts::NodeId queue_owner,
      contracts::PacketId expected_packet_id) -> contracts::Status;

  [[nodiscard]] auto size(contracts::NodeId queue_owner) const
      -> contracts::Result<std::size_t>;

 private:
  struct NodeQueue final {
    contracts::NodeId owner_node_id;
    std::deque<contracts::DigitalPacket> packets;
  };

  explicit PacketQueueStore(std::vector<NodeQueue> queues) noexcept
      : queues_(std::move(queues)) {}

  [[nodiscard]] auto FindQueue(contracts::NodeId queue_owner) noexcept
      -> NodeQueue*;

  [[nodiscard]] auto FindQueue(contracts::NodeId queue_owner) const noexcept
      -> const NodeQueue*;

  [[nodiscard]] auto ContainsNode(contracts::NodeId node_id) const noexcept
      -> bool;

  std::vector<NodeQueue> queues_;
};

inline auto PacketQueueStore::Create(
    std::vector<contracts::NodeId> node_ids)
    -> contracts::Result<PacketQueueStore> {
  std::sort(node_ids.begin(), node_ids.end());
  if(std::adjacent_find(node_ids.begin(), node_ids.end()) !=
     node_ids.end()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kAlreadyExists,
                         "PacketQueueStore node universe contains a "
                         "duplicate NodeId"});
  }

  std::vector<NodeQueue> queues;
  queues.reserve(node_ids.size());
  for(const auto node_id : node_ids) {
    queues.push_back(NodeQueue{node_id, {}});
  }
  return PacketQueueStore{std::move(queues)};
}

inline auto PacketQueueStore::FindQueue(
    contracts::NodeId queue_owner) noexcept -> NodeQueue* {
  const auto queue = std::lower_bound(
      queues_.begin(),
      queues_.end(),
      queue_owner,
      [](const NodeQueue& candidate, contracts::NodeId owner) {
        return candidate.owner_node_id < owner;
      });
  if(queue == queues_.end() || queue->owner_node_id != queue_owner) {
    return nullptr;
  }
  return &*queue;
}

inline auto PacketQueueStore::FindQueue(
    contracts::NodeId queue_owner) const noexcept -> const NodeQueue* {
  const auto queue = std::lower_bound(
      queues_.begin(),
      queues_.end(),
      queue_owner,
      [](const NodeQueue& candidate, contracts::NodeId owner) {
        return candidate.owner_node_id < owner;
      });
  if(queue == queues_.end() || queue->owner_node_id != queue_owner) {
    return nullptr;
  }
  return &*queue;
}

inline auto PacketQueueStore::ContainsNode(
    contracts::NodeId node_id) const noexcept -> bool {
  return FindQueue(node_id) != nullptr;
}

inline auto PacketQueueStore::Enqueue(
    contracts::NodeId queue_owner,
    contracts::DigitalPacket packet) -> contracts::Status {
  auto* const queue = FindQueue(queue_owner);
  if(queue == nullptr) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kNotFound,
                         "Packet queue owner is outside the node universe"});
  }
  if(!ContainsNode(packet.source_node_id)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kNotFound,
                         "Packet source is outside the node universe"});
  }
  if(const auto* const destination =
         std::get_if<contracts::UnicastDestination>(&packet.destination);
     destination != nullptr && !ContainsNode(destination->node_id)) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kNotFound,
            "Unicast packet destination is outside the node universe"});
  }
  if(std::any_of(queue->packets.begin(),
                 queue->packets.end(),
                 [&](const contracts::DigitalPacket& queued_packet) {
                   return queued_packet.packet_id == packet.packet_id;
                 })) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kAlreadyExists,
                         "PacketId already exists in this node queue"});
  }

  queue->packets.push_back(std::move(packet));
  return {};
}

inline auto PacketQueueStore::PeekFront(
    contracts::NodeId queue_owner) const
    -> contracts::Result<std::optional<contracts::DigitalPacket>> {
  const auto* const queue = FindQueue(queue_owner);
  if(queue == nullptr) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kNotFound,
                         "Packet queue owner is outside the node universe"});
  }
  if(queue->packets.empty()) {
    return std::nullopt;
  }
  return std::optional<contracts::DigitalPacket>{queue->packets.front()};
}

inline auto PacketQueueStore::ConsumeFront(
    contracts::NodeId queue_owner,
    contracts::PacketId expected_packet_id) -> contracts::Status {
  auto* const queue = FindQueue(queue_owner);
  if(queue == nullptr) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kNotFound,
                         "Packet queue owner is outside the node universe"});
  }
  if(queue->packets.empty() ||
     queue->packets.front().packet_id != expected_packet_id) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Packet queue front does not match expected PacketId"});
  }

  queue->packets.pop_front();
  return {};
}

inline auto PacketQueueStore::size(
    contracts::NodeId queue_owner) const
    -> contracts::Result<std::size_t> {
  const auto* const queue = FindQueue(queue_owner);
  if(queue == nullptr) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kNotFound,
                         "Packet queue owner is outside the node universe"});
  }
  return queue->packets.size();
}

}  // namespace ns3_factory::runtime::internal
