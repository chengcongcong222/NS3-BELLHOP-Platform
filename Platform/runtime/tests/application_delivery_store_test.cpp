#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/destination.hpp>

#include "internal/application_delivery_store.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::runtime::internal;

namespace {

auto Packet(std::uint64_t id) -> DigitalPacket {
  return DigitalPacket{PacketId{id},
                       NodeId{0},
                       BroadcastDestination{},
                       {std::byte{static_cast<unsigned char>(id)}}};
}

auto Delivery(std::uint64_t reception,
              std::uint64_t transmission,
              std::uint64_t receiver,
              std::uint64_t packet) -> LocalDeliveryReception {
  return LocalDeliveryReception{ReceptionId{reception},
                                TransmissionId{transmission},
                                NodeId{receiver},
                                Packet(packet)};
}

auto TestUniverseValidationAndNodeZero() -> bool {
  const auto duplicate = ApplicationDeliveryStore::Create(
      {NodeId{0}, NodeId{1}, NodeId{0}});
  auto store = ApplicationDeliveryStore::Create(
      {NodeId{3}, NodeId{0}, NodeId{2}, NodeId{1}});
  if(duplicate || !store ||
     duplicate.error().code != ErrorCode::kAlreadyExists) {
    return false;
  }

  const auto accepted = store->Append(Delivery(10, 20, 0, 7));
  const auto unknown = store->Append(Delivery(11, 21, 9, 7));
  return accepted && !unknown && unknown.error().code == ErrorCode::kNotFound &&
         store->size() == 1 &&
         store->deliveries().front().receiver_node_id == NodeId{0};
}

auto TestReceptionIdentityAndCanonicalOrdering() -> bool {
  auto store = ApplicationDeliveryStore::Create(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
  if(!store || !store->Append(Delivery(30, 40, 2, 7)) ||
     !store->Append(Delivery(20, 50, 0, 7)) ||
     !store->Append(Delivery(21, 41, 0, 7)) ||
     !store->Append(Delivery(31, 42, 3, 7))) {
    return false;
  }

  const auto duplicate = store->Append(Delivery(20, 99, 1, 99));
  const auto deliveries = store->deliveries();
  return !duplicate && duplicate.error().code == ErrorCode::kAlreadyExists &&
         deliveries.size() == 4 &&
         deliveries[0].receiver_node_id == NodeId{0} &&
         deliveries[0].reception_id == ReceptionId{20} &&
         deliveries[1].receiver_node_id == NodeId{0} &&
         deliveries[1].reception_id == ReceptionId{21} &&
         deliveries[2].receiver_node_id == NodeId{2} &&
         deliveries[3].receiver_node_id == NodeId{3} &&
         deliveries[0].packet.packet_id == PacketId{7} &&
         deliveries[1].packet.packet_id == PacketId{7} &&
         deliveries[2].packet.packet_id == PacketId{7} &&
         deliveries[3].packet.packet_id == PacketId{7};
}

}  // namespace

auto main() -> int {
  return TestUniverseValidationAndNodeZero() &&
                 TestReceptionIdentityAndCanonicalOrdering()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
