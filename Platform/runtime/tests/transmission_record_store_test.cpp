#include <cstddef>
#include <cstdlib>
#include <utility>

#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/transmission.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>

#include "internal/transmission_record.hpp"
#include "internal/transmission_record_store.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::runtime::internal;

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

auto Packet(PacketId packet_id,
            NodeId source,
            PacketDestination destination) -> DigitalPacket {
  return DigitalPacket{
      packet_id, source, std::move(destination), {std::byte{0x12}}};
}

auto Tx(TransmissionId transmission_id,
        PacketId packet_id,
        NodeId sender,
        TransmissionTarget target) -> Transmission {
  return Transmission{transmission_id,
                      packet_id,
                      sender,
                      std::move(target),
                      At(10),
                      At(20)};
}

auto MakeRecord(TransmissionId transmission_id,
                PacketId packet_id,
                NodeId source,
                NodeId sender,
                PacketDestination destination,
                TransmissionTarget target)
    -> Result<TransmissionRecord> {
  return TransmissionRecord::Create(
      Packet(packet_id, source, std::move(destination)),
      Tx(transmission_id, packet_id, sender, std::move(target)));
}

auto TestRecordIdentityAndRelaySource() -> bool {
  const auto relay = MakeRecord(
      TransmissionId{7},
      PacketId{7},
      NodeId{0},
      NodeId{1},
      UnicastDestination{NodeId{4}},
      UnicastTransmissionTarget{NodeId{2}});
  const auto mismatch = TransmissionRecord::Create(
      Packet(PacketId{8}, NodeId{0}, BroadcastDestination{}),
      Tx(TransmissionId{8},
         PacketId{9},
         NodeId{1},
         BroadcastTransmissionTarget{}));
  const auto intentionally_different_semantics = MakeRecord(
      TransmissionId{9},
      PacketId{9},
      NodeId{0},
      NodeId{1},
      BroadcastDestination{},
      UnicastTransmissionTarget{NodeId{2}});
  return relay && relay->packet().source_node_id == NodeId{0} &&
         relay->transmission().sender_node_id == NodeId{1} &&
         !mismatch &&
         mismatch.error().code == ErrorCode::kFailedPrecondition &&
         intentionally_different_semantics;
}

auto TestRegisterLookupDuplicateAndMissing() -> bool {
  const auto first = MakeRecord(
      TransmissionId{7},
      PacketId{70},
      NodeId{0},
      NodeId{1},
      UnicastDestination{NodeId{4}},
      UnicastTransmissionTarget{NodeId{2}});
  const auto second = MakeRecord(
      TransmissionId{3},
      PacketId{30},
      NodeId{1},
      NodeId{1},
      BroadcastDestination{},
      BroadcastTransmissionTarget{});
  const auto duplicate = MakeRecord(
      TransmissionId{7},
      PacketId{71},
      NodeId{2},
      NodeId{2},
      BroadcastDestination{},
      BroadcastTransmissionTarget{});
  if(!first || !second || !duplicate) {
    return false;
  }

  TransmissionRecordStore store;
  const auto registered_first = store.Register(*first);
  const auto registered_second = store.Register(*second);
  const auto duplicate_result = store.Register(*duplicate);
  const auto found_first = store.Find(TransmissionId{7});
  const auto found_second = store.Find(TransmissionId{3});
  const auto missing = store.Find(TransmissionId{99});
  return registered_first && registered_second && !duplicate_result &&
         duplicate_result.error().code == ErrorCode::kAlreadyExists &&
         store.size() == 2 && found_first && found_second &&
         found_first->get() == *first && found_second->get() == *second &&
         !missing && missing.error().code == ErrorCode::kNotFound;
}

}  // namespace

auto main() -> int {
  return TestRecordIdentityAndRelaySource() &&
                 TestRegisterLookupDuplicateAndMissing()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
