#pragma once

#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/received_signal.hpp>
#include <ns3_factory/contracts/receiver_window.hpp>

#include "internal/in_flight_signal_ledger.hpp"

namespace ns3_factory::runtime::internal {

class ReceiverWindowBuilder final {
 public:
  [[nodiscard]] static auto Build(
      const contracts::ReceivedSignal& desired_signal,
      const InFlightSignalLedger& ledger)
      -> contracts::Result<contracts::ReceiverWindow>;
};

inline auto ReceiverWindowBuilder::Build(
    const contracts::ReceivedSignal& desired_signal,
    const InFlightSignalLedger& ledger)
    -> contracts::Result<contracts::ReceiverWindow> {
  if(!ledger.Contains(desired_signal)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kNotFound,
                         "Desired signal is not present in the in-flight "
                         "ledger"});
  }

  std::vector<contracts::ReceivedSignal> overlapping_signals;
  for(const auto& candidate :
      ledger.SignalsForReceiver(desired_signal.receiver_node_id())) {
    if(candidate.transmission_id() == desired_signal.transmission_id()) {
      continue;
    }
    if(contracts::HasP0SignalOverlap(desired_signal, candidate)) {
      overlapping_signals.push_back(candidate);
    }
  }

  return contracts::ReceiverWindow::Create(
      desired_signal.receiver_node_id(),
      desired_signal,
      std::move(overlapping_signals));
}

}  // namespace ns3_factory::runtime::internal
