#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/packet.hpp>

#include "internal/packet_queue_store.hpp"

using ns3_factory::contracts::BroadcastDestination;
using ns3_factory::contracts::DigitalPacket;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PacketDestination;
using ns3_factory::contracts::PacketId;
using ns3_factory::contracts::UnicastDestination;
using ns3_factory::runtime::internal::PacketQueueStore;

namespace {

auto MakePacket(std::uint64_t packet_id,
                std::uint64_t source_node_id,
                PacketDestination destination) -> DigitalPacket {
  return DigitalPacket{
      PacketId{packet_id},
      NodeId{source_node_id},
      std::move(destination),
      {std::byte{static_cast<unsigned char>(packet_id)}}};
}

auto TestCanonicalUniverseAndFifo() -> bool {
  auto store = PacketQueueStore::Create(
      {NodeId{2}, NodeId{0}, NodeId{1}});
  if(!store) {
    return false;
  }

  const auto first = MakePacket(
      10, 0, UnicastDestination{NodeId{2}});
  const auto second = MakePacket(20, 0, BroadcastDestination{});
  const auto first_enqueue = store->Enqueue(NodeId{0}, first);
  const auto second_enqueue = store->Enqueue(NodeId{0}, second);
  const auto initial_size = store->size(NodeId{0});
  const auto first_peek = store->PeekFront(NodeId{0});
  const auto size_after_peek = store->size(NodeId{0});
  if(!first_enqueue || !second_enqueue || !initial_size ||
     !first_peek || !*first_peek || !size_after_peek) {
    return false;
  }

  const auto wrong_consume =
      store->ConsumeFront(NodeId{0}, PacketId{20});
  const auto front_after_failure = store->PeekFront(NodeId{0});
  const auto size_after_failure = store->size(NodeId{0});
  if(wrong_consume ||
     wrong_consume.error().code != ErrorCode::kFailedPrecondition ||
     !front_after_failure || !*front_after_failure ||
     !size_after_failure) {
    return false;
  }

  const auto consume_first =
      store->ConsumeFront(NodeId{0}, PacketId{10});
  const auto second_peek = store->PeekFront(NodeId{0});
  const auto size_after_consume = store->size(NodeId{0});
  if(!consume_first || !second_peek || !*second_peek ||
     !size_after_consume) {
    return false;
  }

  const auto consume_second =
      store->ConsumeFront(NodeId{0}, PacketId{20});
  const auto empty = store->PeekFront(NodeId{0});
  const auto empty_consume =
      store->ConsumeFront(NodeId{0}, PacketId{20});
  return *initial_size == 2 && *size_after_peek == 2 &&
         (*first_peek)->packet_id == PacketId{10} &&
         *size_after_failure == 2 &&
         (*front_after_failure)->packet_id == PacketId{10} &&
         (*second_peek)->packet_id == PacketId{20} &&
         *size_after_consume == 1 && consume_second && empty && !*empty &&
         !empty_consume &&
         empty_consume.error().code == ErrorCode::kFailedPrecondition;
}

auto TestUnknownOwnerAndPacketUniverseValidation() -> bool {
  auto store = PacketQueueStore::Create({NodeId{0}, NodeId{1}});
  if(!store) {
    return false;
  }

  const auto unknown_owner = store->Enqueue(
      NodeId{9}, MakePacket(1, 0, BroadcastDestination{}));
  const auto unknown_peek = store->PeekFront(NodeId{9});
  const auto unknown_consume =
      store->ConsumeFront(NodeId{9}, PacketId{1});
  const auto unknown_size = store->size(NodeId{9});
  const auto unknown_source = store->Enqueue(
      NodeId{0}, MakePacket(2, 9, BroadcastDestination{}));
  const auto unknown_destination = store->Enqueue(
      NodeId{0}, MakePacket(3, 0, UnicastDestination{NodeId{9}}));
  const auto valid_broadcast = store->Enqueue(
      NodeId{0}, MakePacket(4, 0, BroadcastDestination{}));

  return !unknown_owner &&
         unknown_owner.error().code == ErrorCode::kNotFound &&
         !unknown_peek &&
         unknown_peek.error().code == ErrorCode::kNotFound &&
         !unknown_consume &&
         unknown_consume.error().code == ErrorCode::kNotFound &&
         !unknown_size &&
         unknown_size.error().code == ErrorCode::kNotFound &&
         !unknown_source &&
         unknown_source.error().code == ErrorCode::kNotFound &&
         !unknown_destination &&
         unknown_destination.error().code == ErrorCode::kNotFound &&
         valid_broadcast;
}

auto TestDuplicatePolicyAndRelayOwnership() -> bool {
  auto store = PacketQueueStore::Create(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  if(!store) {
    return false;
  }

  const auto relay_packet = MakePacket(
      7, 0, UnicastDestination{NodeId{3}});
  const auto first_owner = store->Enqueue(NodeId{1}, relay_packet);
  const auto same_owner_duplicate =
      store->Enqueue(NodeId{1}, relay_packet);
  const auto different_owner = store->Enqueue(NodeId{2}, relay_packet);
  const auto selected_from_relay = store->PeekFront(NodeId{1});
  const auto relay_size = store->size(NodeId{1});
  const auto second_owner_size = store->size(NodeId{2});

  return first_owner && !same_owner_duplicate &&
         same_owner_duplicate.error().code == ErrorCode::kAlreadyExists &&
         different_owner && selected_from_relay &&
         *selected_from_relay && relay_size && *relay_size == 1 &&
         second_owner_size && *second_owner_size == 1 &&
         (*selected_from_relay)->source_node_id == NodeId{0} &&
         std::get<UnicastDestination>(
             (*selected_from_relay)->destination)
                 .node_id == NodeId{3};
}

auto TestDuplicateNodeUniverseRejected() -> bool {
  const auto result = PacketQueueStore::Create(
      {NodeId{0}, NodeId{1}, NodeId{0}});
  return !result && result.error().code == ErrorCode::kAlreadyExists;
}

}  // namespace

auto main() -> int {
  return TestCanonicalUniverseAndFifo() &&
                 TestUnknownOwnerAndPacketUniverseValidation() &&
                 TestDuplicatePolicyAndRelayOwnership() &&
                 TestDuplicateNodeUniverseRejected()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
