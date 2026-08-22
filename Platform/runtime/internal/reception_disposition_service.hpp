#pragma once

#include <variant>

#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/received_signal.hpp>
#include <ns3_factory/contracts/reception.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>

#include "internal/reception_disposition.hpp"
#include "internal/reception_session.hpp"
#include "internal/transmission_record.hpp"
#include "internal/transmission_record_store.hpp"

namespace ns3_factory::runtime::internal {

class ReceptionDispositionService final {
 public:
  [[nodiscard]] auto Decide(
      const ReceptionSession& reception_session,
      const TransmissionRecordStore& record_store) const
      -> contracts::Result<ReceptionDisposition>;

  // Exposed within runtime-internal so malformed evidence can be rejected
  // even though ReceiverProcessor normally constructs consistent sessions.
  [[nodiscard]] static auto ValidateIdentity(
      const contracts::Reception& reception,
      const contracts::ReceivedSignal& desired_signal,
      const contracts::RxDecodeResult& decode_result,
      const TransmissionRecord& record) -> contracts::Status;
};

inline auto ReceptionDispositionService::ValidateIdentity(
    const contracts::Reception& reception,
    const contracts::ReceivedSignal& desired_signal,
    const contracts::RxDecodeResult& decode_result,
    const TransmissionRecord& record) -> contracts::Status {
  const auto& packet = record.packet();
  const auto& transmission = record.transmission();
  if(reception.transmission_id != decode_result.transmission_id() ||
     reception.transmission_id != transmission.transmission_id ||
     decode_result.transmission_id() != transmission.transmission_id ||
     decode_result.packet_id() != packet.packet_id ||
     transmission.packet_id != packet.packet_id ||
     reception.receiver_node_id != decode_result.receiver_node_id() ||
     desired_signal.receiver_node_id() != reception.receiver_node_id ||
     desired_signal.transmission_id() != transmission.transmission_id ||
     desired_signal.emission().packet_id() != packet.packet_id ||
     desired_signal.emission().sender_node_id() !=
         transmission.sender_node_id) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Reception disposition identity does not match transmission "
            "record"});
  }
  return {};
}

inline auto ReceptionDispositionService::Decide(
    const ReceptionSession& reception_session,
    const TransmissionRecordStore& record_store) const
    -> contracts::Result<ReceptionDisposition> {
  const auto& reception = reception_session.reception();
  const auto record = record_store.Find(reception.transmission_id);
  if(!record) {
    return std::unexpected(record.error());
  }

  const auto& transmission_record = record->get();
  const auto identity = ValidateIdentity(
      reception,
      reception_session.desired_signal(),
      reception_session.decode_result(),
      transmission_record);
  if(!identity) {
    return std::unexpected(identity.error());
  }

  const auto& transmission = transmission_record.transmission();
  if(reception.receiver_node_id == transmission.sender_node_id) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Reception receiver must differ from transmission sender"});
  }

  if(reception_session.decode_result().outcome() ==
     contracts::DecodeOutcome::kNotDecoded) {
    return NotDecodedReception{reception.reception_id,
                               reception.transmission_id,
                               reception.receiver_node_id};
  }

  const auto& packet = transmission_record.packet();
  if(const auto* const target =
         std::get_if<contracts::UnicastTransmissionTarget>(
             &transmission.target)) {
    const auto* const destination =
        std::get_if<contracts::UnicastDestination>(&packet.destination);
    if(destination == nullptr) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kFailedPrecondition,
              "Unicast transmission target requires unicast packet "
              "destination"});
    }
    if(reception.receiver_node_id != target->node_id) {
      return OverheardReception{reception.reception_id,
                                reception.transmission_id,
                                reception.receiver_node_id};
    }
    if(target->node_id == destination->node_id) {
      return LocalDeliveryReception{reception.reception_id,
                                    reception.transmission_id,
                                    reception.receiver_node_id,
                                    packet};
    }
    return RelayEnqueueReception{reception.reception_id,
                                 reception.transmission_id,
                                 reception.receiver_node_id,
                                 packet};
  }

  if(!std::holds_alternative<contracts::BroadcastDestination>(
         packet.destination)) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Broadcast transmission target requires broadcast packet "
            "destination"});
  }
  return LocalDeliveryReception{reception.reception_id,
                                reception.transmission_id,
                                reception.receiver_node_id,
                                packet};
}

}  // namespace ns3_factory::runtime::internal
