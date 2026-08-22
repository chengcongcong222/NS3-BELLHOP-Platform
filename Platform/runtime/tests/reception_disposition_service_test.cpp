#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/node_capability.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/received_signal.hpp>
#include <ns3_factory/contracts/reception.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/transmission.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/communication_id_allocator.hpp"
#include "internal/cycle_working_state.hpp"
#include "internal/in_flight_signal_ledger.hpp"
#include "internal/receiver_processor.hpp"
#include "internal/reception_disposition.hpp"
#include "internal/reception_disposition_service.hpp"
#include "internal/reception_session.hpp"
#include "internal/transmission_record.hpp"
#include "internal/transmission_record_store.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::runtime::internal;

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

constexpr auto For(std::int64_t nanoseconds) -> SimDuration {
  return SimDuration::FromNanoseconds(nanoseconds);
}

constexpr auto Node(std::uint64_t id) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, 0.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto Packet(PacketId packet_id,
            NodeId source,
            PacketDestination destination,
            std::vector<std::byte> payload = {std::byte{0x21}})
    -> DigitalPacket {
  return DigitalPacket{
      packet_id, source, std::move(destination), std::move(payload)};
}

auto MakeRecord(TransmissionId transmission_id,
                PacketId packet_id,
                NodeId source,
                NodeId sender,
                PacketDestination destination,
                TransmissionTarget target,
                std::vector<std::byte> payload = {std::byte{0x21}})
    -> Result<TransmissionRecord> {
  return TransmissionRecord::Create(
      Packet(packet_id, source, std::move(destination), std::move(payload)),
      Transmission{transmission_id,
                   packet_id,
                   sender,
                   std::move(target),
                   At(10),
                   At(20)});
}

auto MakeSignal(TransmissionId transmission_id,
                PacketId packet_id,
                NodeId sender,
                NodeId receiver) -> Result<ReceivedSignal> {
  const auto emission = TxEmission::Create(transmission_id,
                                           packet_id,
                                           sender,
                                           At(10),
                                           For(5),
                                           25'000.0,
                                           4'000.0,
                                           180.0);
  const auto response = ChannelFieldResponse::Create(transmission_id,
                                                     receiver,
                                                     70.0,
                                                     For(1),
                                                     {});
  if(!emission || !response) {
    return std::unexpected(!emission ? emission.error()
                                     : response.error());
  }
  return ReceivedSignal::Create(*emission, *response);
}

class FixtureNoise final : public INoiseFieldProvider {
 public:
  auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    return NoiseObservation::Create(query.receiver_node_id(),
                                    query.observed_from(),
                                    query.observed_until(),
                                    query.lower_frequency_hz(),
                                    query.upper_frequency_hz(),
                                    45.0);
  }
};

class FixtureRx final : public IRxPhy {
 public:
  explicit FixtureRx(DecodeOutcome outcome) noexcept : outcome_(outcome) {}

  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    const auto& desired = request.receiver_window().desired_signal();
    return RxDecodeResult::Create(desired.transmission_id(),
                                  desired.emission().packet_id(),
                                  desired.receiver_node_id(),
                                  outcome_);
  }

 private:
  DecodeOutcome outcome_;
};

auto MakeSession(TransmissionId transmission_id,
                 PacketId packet_id,
                 NodeId sender,
                 NodeId receiver,
                 DecodeOutcome outcome,
                 ReceptionId reception_id = ReceptionId{100})
    -> Result<ReceptionSession> {
  auto signal = MakeSignal(transmission_id, packet_id, sender, receiver);
  auto snapshot = WorldSnapshot::Create(
      SnapshotVersion{0},
      SimTime::Zero(),
      {Node(0), Node(1), Node(2), Node(3), Node(4), Node(9)});
  auto working = snapshot
                     ? CycleWorkingState::Create(
                           *snapshot, PlanningCycleId{0}, SimTime::Zero())
                     : Result<CycleWorkingState>{
                           std::unexpected(snapshot.error())};
  if(!signal || !snapshot || !working) {
    return std::unexpected(!signal ? signal.error()
                                   : (!snapshot ? snapshot.error()
                                                : working.error()));
  }

  InFlightSignalLedger ledger;
  const auto inserted = ledger.Insert(*signal);
  if(!inserted) {
    return std::unexpected(inserted.error());
  }
  CommunicationIdAllocator ids{TransmissionId{900}, reception_id};
  const FixtureNoise noise;
  const FixtureRx rx{outcome};
  const ReceiverProcessor processor{ids, noise, rx};
  return processor.ProcessReceivedSignal(*signal, ledger, *working);
}

auto StoreWith(const TransmissionRecord& record)
    -> Result<TransmissionRecordStore> {
  TransmissionRecordStore store;
  const auto registered = store.Register(record);
  if(!registered) {
    return std::unexpected(registered.error());
  }
  return store;
}

auto TestNotDecodedPrecedesTargetAcceptanceAndIsDeterministic() -> bool {
  const auto record = MakeRecord(TransmissionId{10},
                                 PacketId{10},
                                 NodeId{0},
                                 NodeId{1},
                                 BroadcastDestination{},
                                 UnicastTransmissionTarget{NodeId{2}});
  const auto session = MakeSession(TransmissionId{10},
                                   PacketId{10},
                                   NodeId{1},
                                   NodeId{2},
                                   DecodeOutcome::kNotDecoded);
  const auto store = record ? StoreWith(*record)
                            : Result<TransmissionRecordStore>{
                                  std::unexpected(record.error())};
  if(!record || !session || !store) {
    return false;
  }

  const ReceptionDispositionService service;
  const auto first = service.Decide(*session, *store);
  const auto second = service.Decide(*session, *store);
  return first && second && *first == *second &&
         std::holds_alternative<NotDecodedReception>(*first);
}

auto TestUnicastTargetAcceptanceAndOverhearing() -> bool {
  const auto record = MakeRecord(TransmissionId{20},
                                 PacketId{20},
                                 NodeId{0},
                                 NodeId{0},
                                 UnicastDestination{NodeId{3}},
                                 UnicastTransmissionTarget{NodeId{2}});
  const auto receiver_one = MakeSession(TransmissionId{20},
                                        PacketId{20},
                                        NodeId{0},
                                        NodeId{1},
                                        DecodeOutcome::kDecoded,
                                        ReceptionId{101});
  const auto receiver_two = MakeSession(TransmissionId{20},
                                        PacketId{20},
                                        NodeId{0},
                                        NodeId{2},
                                        DecodeOutcome::kDecoded,
                                        ReceptionId{102});
  const auto receiver_three = MakeSession(TransmissionId{20},
                                          PacketId{20},
                                          NodeId{0},
                                          NodeId{3},
                                          DecodeOutcome::kDecoded,
                                          ReceptionId{103});
  const auto store = record ? StoreWith(*record)
                            : Result<TransmissionRecordStore>{
                                  std::unexpected(record.error())};
  if(!record || !receiver_one || !receiver_two || !receiver_three ||
     !store) {
    return false;
  }

  const ReceptionDispositionService service;
  const auto one = service.Decide(*receiver_one, *store);
  const auto two = service.Decide(*receiver_two, *store);
  const auto three = service.Decide(*receiver_three, *store);
  return one && two && three &&
         std::holds_alternative<OverheardReception>(*one) &&
         std::holds_alternative<RelayEnqueueReception>(*two) &&
         std::holds_alternative<OverheardReception>(*three);
}

auto TestFinalDeliveryAndRelayPacketPreservation() -> bool {
  const auto local_record = MakeRecord(
      TransmissionId{30},
      PacketId{10},
      NodeId{0},
      NodeId{1},
      UnicastDestination{NodeId{3}},
      UnicastTransmissionTarget{NodeId{3}},
      {std::byte{0x31}, std::byte{0x32}});
  const auto relay_record = MakeRecord(
      TransmissionId{31},
      PacketId{11},
      NodeId{0},
      NodeId{1},
      UnicastDestination{NodeId{4}},
      UnicastTransmissionTarget{NodeId{2}},
      {std::byte{0x41}, std::byte{0x42}});
  const auto local_session = MakeSession(TransmissionId{30},
                                         PacketId{10},
                                         NodeId{1},
                                         NodeId{3},
                                         DecodeOutcome::kDecoded);
  const auto relay_session = MakeSession(TransmissionId{31},
                                         PacketId{11},
                                         NodeId{1},
                                         NodeId{2},
                                         DecodeOutcome::kDecoded);
  const auto local_store = local_record ? StoreWith(*local_record)
                                        : Result<TransmissionRecordStore>{
                                              std::unexpected(
                                                  local_record.error())};
  const auto relay_store = relay_record ? StoreWith(*relay_record)
                                        : Result<TransmissionRecordStore>{
                                              std::unexpected(
                                                  relay_record.error())};
  if(!local_record || !relay_record || !local_session || !relay_session ||
     !local_store || !relay_store) {
    return false;
  }

  const ReceptionDispositionService service;
  const auto local = service.Decide(*local_session, *local_store);
  const auto relay = service.Decide(*relay_session, *relay_store);
  if(!local || !relay ||
     !std::holds_alternative<LocalDeliveryReception>(*local) ||
     !std::holds_alternative<RelayEnqueueReception>(*relay)) {
    return false;
  }
  const auto& local_action = std::get<LocalDeliveryReception>(*local);
  const auto& relay_action = std::get<RelayEnqueueReception>(*relay);
  return local_action.reception_id == ReceptionId{100} &&
         local_action.transmission_id == TransmissionId{30} &&
         local_action.receiver_node_id == NodeId{3} &&
         local_action.packet == local_record->packet() &&
         relay_action.reception_id == ReceptionId{100} &&
         relay_action.transmission_id == TransmissionId{31} &&
         relay_action.receiver_node_id == NodeId{2} &&
         relay_action.packet == relay_record->packet();
}

auto TestBroadcastLocalDeliveryOnly() -> bool {
  const auto record = MakeRecord(TransmissionId{40},
                                 PacketId{40},
                                 NodeId{0},
                                 NodeId{0},
                                 BroadcastDestination{},
                                 BroadcastTransmissionTarget{});
  const auto store = record ? StoreWith(*record)
                            : Result<TransmissionRecordStore>{
                                  std::unexpected(record.error())};
  if(!record || !store) {
    return false;
  }

  const ReceptionDispositionService service;
  for(std::uint64_t receiver = 1; receiver <= 3; ++receiver) {
    const auto session = MakeSession(TransmissionId{40},
                                     PacketId{40},
                                     NodeId{0},
                                     NodeId{receiver},
                                     DecodeOutcome::kDecoded,
                                     ReceptionId{200 + receiver});
    if(!session) {
      return false;
    }
    const auto disposition = service.Decide(*session, *store);
    if(!disposition ||
       !std::holds_alternative<LocalDeliveryReception>(*disposition)) {
      return false;
    }
  }
  return true;
}

auto TestUnsupportedCombinationsAndSelfReception() -> bool {
  const auto unicast_target_broadcast_packet = MakeRecord(
      TransmissionId{50},
      PacketId{50},
      NodeId{0},
      NodeId{0},
      BroadcastDestination{},
      UnicastTransmissionTarget{NodeId{2}});
  const auto broadcast_target_unicast_packet = MakeRecord(
      TransmissionId{51},
      PacketId{51},
      NodeId{0},
      NodeId{0},
      UnicastDestination{NodeId{3}},
      BroadcastTransmissionTarget{});
  const auto self_record = MakeRecord(TransmissionId{52},
                                      PacketId{52},
                                      NodeId{0},
                                      NodeId{1},
                                      BroadcastDestination{},
                                      BroadcastTransmissionTarget{});
  const auto first_session = MakeSession(TransmissionId{50},
                                         PacketId{50},
                                         NodeId{0},
                                         NodeId{2},
                                         DecodeOutcome::kDecoded);
  const auto second_session = MakeSession(TransmissionId{51},
                                          PacketId{51},
                                          NodeId{0},
                                          NodeId{2},
                                          DecodeOutcome::kDecoded);
  const auto self_session = MakeSession(TransmissionId{52},
                                        PacketId{52},
                                        NodeId{1},
                                        NodeId{1},
                                        DecodeOutcome::kDecoded);
  if(!unicast_target_broadcast_packet ||
     !broadcast_target_unicast_packet || !self_record || !first_session ||
     !second_session || !self_session) {
    return false;
  }
  auto first_store = StoreWith(*unicast_target_broadcast_packet);
  auto second_store = StoreWith(*broadcast_target_unicast_packet);
  auto self_store = StoreWith(*self_record);
  if(!first_store || !second_store || !self_store) {
    return false;
  }

  const ReceptionDispositionService service;
  const auto first = service.Decide(*first_session, *first_store);
  const auto second = service.Decide(*second_session, *second_store);
  const auto self = service.Decide(*self_session, *self_store);
  return !first && first.error().code == ErrorCode::kFailedPrecondition &&
         !second && second.error().code == ErrorCode::kFailedPrecondition &&
         !self && self.error().code == ErrorCode::kFailedPrecondition;
}

auto TestIdentityMismatchesAndMissingRecord() -> bool {
  const auto record = MakeRecord(TransmissionId{60},
                                 PacketId{60},
                                 NodeId{0},
                                 NodeId{1},
                                 UnicastDestination{NodeId{3}},
                                 UnicastTransmissionTarget{NodeId{3}});
  const auto valid_signal =
      MakeSignal(TransmissionId{60}, PacketId{60}, NodeId{1}, NodeId{3});
  const auto wrong_sender_signal =
      MakeSignal(TransmissionId{60}, PacketId{60}, NodeId{2}, NodeId{3});
  const auto wrong_packet_signal =
      MakeSignal(TransmissionId{60}, PacketId{61}, NodeId{1}, NodeId{3});
  const auto wrong_receiver_signal =
      MakeSignal(TransmissionId{60}, PacketId{60}, NodeId{1}, NodeId{2});
  const auto wrong_transmission_signal =
      MakeSignal(TransmissionId{61}, PacketId{60}, NodeId{1}, NodeId{3});
  const auto valid_decode = RxDecodeResult::Create(TransmissionId{60},
                                                   PacketId{60},
                                                   NodeId{3},
                                                   DecodeOutcome::kDecoded);
  const auto wrong_packet_decode = RxDecodeResult::Create(
      TransmissionId{60},
      PacketId{61},
      NodeId{3},
      DecodeOutcome::kDecoded);
  const auto wrong_receiver_decode = RxDecodeResult::Create(
      TransmissionId{60},
      PacketId{60},
      NodeId{2},
      DecodeOutcome::kDecoded);
  if(!record || !valid_signal || !wrong_sender_signal ||
     !wrong_packet_signal || !wrong_receiver_signal ||
     !wrong_transmission_signal || !valid_decode || !wrong_packet_decode ||
     !wrong_receiver_decode) {
    return false;
  }

  const Reception valid_reception{
      ReceptionId{300}, TransmissionId{60}, NodeId{3}, At(11)};
  const Reception wrong_transmission{
      ReceptionId{300}, TransmissionId{61}, NodeId{3}, At(11)};
  const auto valid = ReceptionDispositionService::ValidateIdentity(
      valid_reception, *valid_signal, *valid_decode, *record);
  const auto wrong_tx = ReceptionDispositionService::ValidateIdentity(
      wrong_transmission, *valid_signal, *valid_decode, *record);
  const auto wrong_packet = ReceptionDispositionService::ValidateIdentity(
      valid_reception, *valid_signal, *wrong_packet_decode, *record);
  const auto wrong_receiver = ReceptionDispositionService::ValidateIdentity(
      valid_reception, *valid_signal, *wrong_receiver_decode, *record);
  const auto wrong_sender = ReceptionDispositionService::ValidateIdentity(
      valid_reception, *wrong_sender_signal, *valid_decode, *record);
  const auto wrong_signal_packet =
      ReceptionDispositionService::ValidateIdentity(
          valid_reception, *wrong_packet_signal, *valid_decode, *record);
  const auto wrong_signal_receiver =
      ReceptionDispositionService::ValidateIdentity(
          valid_reception, *wrong_receiver_signal, *valid_decode, *record);
  const auto wrong_signal_tx = ReceptionDispositionService::ValidateIdentity(
      valid_reception, *wrong_transmission_signal, *valid_decode, *record);

  const auto session = MakeSession(TransmissionId{60},
                                   PacketId{60},
                                   NodeId{1},
                                   NodeId{3},
                                   DecodeOutcome::kDecoded);
  const TransmissionRecordStore empty_store;
  const auto missing = session
                           ? ReceptionDispositionService{}.Decide(
                                 *session, empty_store)
                           : Result<ReceptionDisposition>{
                                 std::unexpected(session.error())};
  return valid && !wrong_tx && !wrong_packet && !wrong_receiver &&
         !wrong_sender && !wrong_signal_packet && !wrong_signal_receiver &&
         !wrong_signal_tx &&
         wrong_tx.error().code == ErrorCode::kFailedPrecondition &&
         wrong_packet.error().code == ErrorCode::kFailedPrecondition &&
         wrong_receiver.error().code == ErrorCode::kFailedPrecondition &&
         wrong_sender.error().code == ErrorCode::kFailedPrecondition &&
         wrong_signal_packet.error().code ==
             ErrorCode::kFailedPrecondition &&
         wrong_signal_receiver.error().code ==
             ErrorCode::kFailedPrecondition &&
         wrong_signal_tx.error().code == ErrorCode::kFailedPrecondition &&
         !missing && missing.error().code == ErrorCode::kNotFound;
}

}  // namespace

auto main() -> int {
  return TestNotDecodedPrecedesTargetAcceptanceAndIsDeterministic() &&
                 TestUnicastTargetAcceptanceAndOverhearing() &&
                 TestFinalDeliveryAndRelayPacketPreservation() &&
                 TestBroadcastLocalDeliveryOnly() &&
                 TestUnsupportedCombinationsAndSelfReception() &&
                 TestIdentityMismatchesAndMissingRecord()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
