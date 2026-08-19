#pragma once

#include <utility>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/reception.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>

#include "internal/communication_id_allocator.hpp"
#include "internal/cycle_working_state.hpp"
#include "internal/in_flight_signal_ledger.hpp"
#include "internal/receiver_window_builder.hpp"
#include "internal/reception_session.hpp"

namespace ns3_factory::runtime::internal {

class ReceiverProcessor final {
 public:
  ReceiverProcessor(CommunicationIdAllocator& id_allocator,
                    const contracts::INoiseFieldProvider& noise_provider,
                    const contracts::IRxPhy& rx_phy) noexcept
      : id_allocator_(id_allocator),
        noise_provider_(noise_provider),
        rx_phy_(rx_phy) {}

  [[nodiscard]] auto ProcessReceivedSignal(
      const contracts::ReceivedSignal& desired_signal,
      const InFlightSignalLedger& ledger,
      const CycleWorkingState& working_state) const
      -> contracts::Result<ReceptionSession>;

 private:
  CommunicationIdAllocator& id_allocator_;
  const contracts::INoiseFieldProvider& noise_provider_;
  const contracts::IRxPhy& rx_phy_;
};

inline auto ReceiverProcessor::ProcessReceivedSignal(
    const contracts::ReceivedSignal& desired_signal,
    const InFlightSignalLedger& ledger,
    const CycleWorkingState& working_state) const
    -> contracts::Result<ReceptionSession> {
  if(!ledger.Contains(desired_signal)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kNotFound,
                         "Desired signal is not present in the in-flight "
                         "ledger"});
  }

  auto receiver_window = ReceiverWindowBuilder::Build(desired_signal, ledger);
  if(!receiver_window) {
    return std::unexpected(receiver_window.error());
  }

  const auto receiver_state = working_state.ProjectNodeState(
      desired_signal.receiver_node_id(), desired_signal.first_arrival_at());
  if(!receiver_state) {
    return std::unexpected(receiver_state.error());
  }

  auto noise_query = contracts::CreateNoiseQueryForDesiredSignal(
      *receiver_window, receiver_state->motion.position);
  if(!noise_query) {
    return std::unexpected(noise_query.error());
  }

  auto noise_observation = noise_provider_.Query(*noise_query);
  if(!noise_observation) {
    return std::unexpected(noise_observation.error());
  }
  const auto noise_identity = contracts::ValidateNoiseObservationIdentity(
      *noise_query, *noise_observation);
  if(!noise_identity) {
    return std::unexpected(noise_identity.error());
  }

  auto decode_request = contracts::RxDecodeRequest::Create(
      *receiver_window, *noise_observation);
  if(!decode_request) {
    return std::unexpected(decode_request.error());
  }

  auto decode_result = rx_phy_.Decode(*decode_request);
  if(!decode_result) {
    return std::unexpected(decode_result.error());
  }
  const auto decode_identity = contracts::ValidateRxDecodeResultIdentity(
      *decode_request, *decode_result);
  if(!decode_identity) {
    return std::unexpected(decode_identity.error());
  }

  const auto reception_id = id_allocator_.NextReceptionId();
  if(!reception_id) {
    return std::unexpected(reception_id.error());
  }

  const contracts::Reception reception{
      *reception_id,
      desired_signal.transmission_id(),
      desired_signal.receiver_node_id(),
      desired_signal.first_arrival_at()};
  return ReceptionSession{reception,
                          desired_signal,
                          std::move(*receiver_window),
                          std::move(*noise_observation),
                          std::move(*decode_result)};
}

}  // namespace ns3_factory::runtime::internal
