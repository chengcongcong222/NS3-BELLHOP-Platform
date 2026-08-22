#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include <ns3_factory/contracts/destination.hpp>

#include "internal/application_delivery_store.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/reception_disposition_applier.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::runtime::internal;

namespace {

auto Packet(std::uint64_t id) -> DigitalPacket {
  return DigitalPacket{PacketId{id},
                       NodeId{0},
                       UnicastDestination{NodeId{3}},
                       {std::byte{0xA5}, std::byte{0x5A}}};
}

auto TestNoEffectAndLocalDelivery() -> bool {
  auto queues = PacketQueueStore::Create(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  auto deliveries = ApplicationDeliveryStore::Create(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  if(!queues || !deliveries) return false;
  ReceptionDispositionApplier applier{*queues, *deliveries};

  if(!applier.Apply(NotDecodedReception{
         ReceptionId{1}, TransmissionId{1}, NodeId{1}}) ||
     !applier.Apply(OverheardReception{
         ReceptionId{2}, TransmissionId{1}, NodeId{2}}) ||
     !applier.Apply(LocalDeliveryReception{
         ReceptionId{3}, TransmissionId{2}, NodeId{0}, Packet(5)})) {
    return false;
  }

  return deliveries->size() == 1 && *queues->size(NodeId{0}) == 0 &&
         *queues->size(NodeId{1}) == 0 && *queues->size(NodeId{2}) == 0 &&
         deliveries->deliveries().front().packet == Packet(5);
}

auto TestRelayPreservationVisibilityAndDuplicateFailure() -> bool {
  auto queues = PacketQueueStore::Create(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  auto deliveries = ApplicationDeliveryStore::Create(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  if(!queues || !deliveries) return false;
  ReceptionDispositionApplier applier{*queues, *deliveries};
  const auto original = Packet(7);
  if(!applier.Apply(RelayEnqueueReception{
         ReceptionId{10}, TransmissionId{20}, NodeId{1}, original})) {
    return false;
  }

  const auto visible = queues->PeekFront(NodeId{1});
  const auto duplicate = applier.Apply(RelayEnqueueReception{
      ReceptionId{11}, TransmissionId{21}, NodeId{1}, original});
  return visible && *visible && **visible == original &&
         (*visible)->packet_id == PacketId{7} &&
         (*visible)->source_node_id == NodeId{0} &&
         std::get<UnicastDestination>((*visible)->destination).node_id ==
             NodeId{3} &&
         (*visible)->payload == original.payload && deliveries->size() == 0 &&
         !duplicate && duplicate.error().code == ErrorCode::kAlreadyExists &&
         *queues->size(NodeId{1}) == 1;
}

}  // namespace

auto main() -> int {
  return TestNoEffectAndLocalDelivery() &&
                 TestRelayPreservationVisibilityAndDuplicateFailure()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
