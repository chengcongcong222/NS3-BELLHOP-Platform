#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/received_signal.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/transmission.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/communication_id_allocator.hpp"
#include "internal/cycle_working_state.hpp"
#include "internal/transmission_session.hpp"

namespace ns3_factory::runtime::internal {

struct TransmissionExecutionRequest final {
  contracts::TxOpportunity opportunity;
  contracts::DigitalPacket selected_packet;
  contracts::TransmissionTarget target;
  contracts::SimTime started_at;
  std::vector<contracts::NodeId> candidate_receivers;
};

class TransmissionExecutor final {
 public:
  TransmissionExecutor(CommunicationIdAllocator& id_allocator,
                       const contracts::ITxPhy& tx_phy,
                       const contracts::IChannelFieldProvider& channel_provider)
      noexcept
      : id_allocator_(id_allocator),
        tx_phy_(tx_phy),
        channel_provider_(channel_provider) {}

  [[nodiscard]] auto ExecuteTransmission(
      const CycleWorkingState& working_state,
      TransmissionExecutionRequest request) const
      -> contracts::Result<TransmissionSession>;

 private:
  CommunicationIdAllocator& id_allocator_;
  const contracts::ITxPhy& tx_phy_;
  const contracts::IChannelFieldProvider& channel_provider_;
};

inline auto TransmissionExecutor::ExecuteTransmission(
    const CycleWorkingState& working_state,
    TransmissionExecutionRequest request) const
    -> contracts::Result<TransmissionSession> {
  if(request.started_at < request.opportunity.eligible_at) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "Transmission started before its opportunity was "
                         "eligible"});
  }

  std::sort(request.candidate_receivers.begin(),
            request.candidate_receivers.end());
  if(std::adjacent_find(request.candidate_receivers.begin(),
                        request.candidate_receivers.end()) !=
     request.candidate_receivers.end()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kAlreadyExists,
                         "Transmission candidate receivers contain a "
                         "duplicate NodeId"});
  }

  const auto sender_state = working_state.ProjectNodeState(
      request.opportunity.sender_node_id, request.started_at);
  if(!sender_state) {
    return std::unexpected(sender_state.error());
  }
  if(!sender_state->capability.can_transmit) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "Transmission sender cannot transmit"});
  }

  std::vector<contracts::NodeCommittedState> receiver_states;
  receiver_states.reserve(request.candidate_receivers.size());
  for(const auto receiver_id : request.candidate_receivers) {
    if(receiver_id == request.opportunity.sender_node_id) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Transmission sender cannot be a candidate "
                           "receiver"});
    }

    auto receiver_state =
        working_state.ProjectNodeState(receiver_id, request.started_at);
    if(!receiver_state) {
      return std::unexpected(receiver_state.error());
    }
    if(!receiver_state->capability.can_receive) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Transmission candidate cannot receive"});
    }
    receiver_states.push_back(std::move(*receiver_state));
  }

  const auto transmission_id = id_allocator_.NextTransmissionId();
  if(!transmission_id) {
    return std::unexpected(transmission_id.error());
  }

  const contracts::TxEncodeRequest encode_request{
      *transmission_id,
      request.opportunity.sender_node_id,
      request.target,
      request.started_at};
  auto emission = tx_phy_.Encode(request.selected_packet, encode_request);
  if(!emission) {
    return std::unexpected(emission.error());
  }

  const auto emission_identity = contracts::ValidateTxEmissionIdentity(
      request.selected_packet, encode_request, *emission);
  if(!emission_identity) {
    return std::unexpected(emission_identity.error());
  }
  if(emission->duration() == contracts::SimDuration::Zero()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "Executed transmission duration must be positive"});
  }

  const auto ended_at =
      contracts::CheckedAdd(emission->started_at(), emission->duration());
  if(!ended_at) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Executed transmission end time overflow"});
  }

  const contracts::Transmission transmission{
      *transmission_id,
      request.selected_packet.packet_id,
      request.opportunity.sender_node_id,
      request.target,
      request.started_at,
      *ended_at};

  std::vector<contracts::ReceivedSignal> received_signals;
  received_signals.reserve(receiver_states.size());
  for(const auto& receiver_state : receiver_states) {
    auto query = contracts::ChannelQuery::Create(
        *transmission_id,
        request.opportunity.sender_node_id,
        receiver_state.node_id,
        sender_state->motion.position,
        receiver_state.motion.position,
        request.started_at,
        emission->center_frequency_hz(),
        emission->bandwidth_hz());
    if(!query) {
      return std::unexpected(query.error());
    }

    auto response = channel_provider_.Query(*query);
    if(!response) {
      return std::unexpected(response.error());
    }
    const auto response_identity =
        contracts::ValidateChannelFieldResponseIdentity(*query, *response);
    if(!response_identity) {
      return std::unexpected(response_identity.error());
    }

    auto received_signal =
        contracts::ReceivedSignal::Create(*emission, *response);
    if(!received_signal) {
      return std::unexpected(received_signal.error());
    }
    received_signals.push_back(std::move(*received_signal));
  }

  return TransmissionSession{std::move(request.selected_packet),
                             transmission,
                             std::move(*emission),
                             std::move(received_signals)};
}

}  // namespace ns3_factory::runtime::internal
