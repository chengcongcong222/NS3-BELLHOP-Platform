#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/received_signal.hpp>

namespace ns3_factory::runtime::internal {

// Mutable runtime registry for signals that may participate in receiver-side
// processing. Scheduler-integrated expiry/cleanup is intentionally deferred.
class InFlightSignalLedger final {
 public:
  [[nodiscard]] auto Insert(contracts::ReceivedSignal signal)
      -> contracts::Status;

  [[nodiscard]] auto Contains(
      const contracts::ReceivedSignal& signal) const noexcept -> bool;

  [[nodiscard]] auto SignalsForReceiver(
      contracts::NodeId receiver_node_id) const noexcept
      -> std::span<const contracts::ReceivedSignal>;

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return signals_.size();
  }

  [[nodiscard]] auto empty() const noexcept -> bool {
    return signals_.empty();
  }

  [[nodiscard]] auto ValidateReadyForCycleClose(
      contracts::SimTime close_time) const -> contracts::Status;

  auto ClearForCycleClose() noexcept -> void {
    signals_.clear();
  }

 private:
  [[nodiscard]] static auto CanonicalLess(
      const contracts::ReceivedSignal& lhs,
      const contracts::ReceivedSignal& rhs) noexcept -> bool;

  std::vector<contracts::ReceivedSignal> signals_;
};

inline auto InFlightSignalLedger::CanonicalLess(
    const contracts::ReceivedSignal& lhs,
    const contracts::ReceivedSignal& rhs) noexcept -> bool {
  if(lhs.receiver_node_id() != rhs.receiver_node_id()) {
    return lhs.receiver_node_id() < rhs.receiver_node_id();
  }
  if(lhs.first_arrival_at() != rhs.first_arrival_at()) {
    return lhs.first_arrival_at() < rhs.first_arrival_at();
  }
  return lhs.transmission_id() < rhs.transmission_id();
}

inline auto InFlightSignalLedger::Insert(
    contracts::ReceivedSignal signal) -> contracts::Status {
  for(const auto& existing : signals_) {
    if(existing.receiver_node_id() == signal.receiver_node_id() &&
       existing.transmission_id() == signal.transmission_id()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kAlreadyExists,
                           "InFlightSignalLedger already contains this "
                           "receiver/TransmissionId"});
    }
  }

  signals_.push_back(std::move(signal));
  std::sort(signals_.begin(), signals_.end(), CanonicalLess);
  return {};
}

inline auto InFlightSignalLedger::Contains(
    const contracts::ReceivedSignal& signal) const noexcept -> bool {
  for(const auto& existing : SignalsForReceiver(signal.receiver_node_id())) {
    if(existing.transmission_id() == signal.transmission_id()) {
      return existing == signal;
    }
  }
  return false;
}

inline auto InFlightSignalLedger::SignalsForReceiver(
    contracts::NodeId receiver_node_id) const noexcept
    -> std::span<const contracts::ReceivedSignal> {
  const auto first = std::lower_bound(
      signals_.begin(),
      signals_.end(),
      receiver_node_id,
      [](const contracts::ReceivedSignal& signal, contracts::NodeId id) {
        return signal.receiver_node_id() < id;
      });
  const auto last = std::upper_bound(
      first,
      signals_.end(),
      receiver_node_id,
      [](contracts::NodeId id, const contracts::ReceivedSignal& signal) {
        return id < signal.receiver_node_id();
      });
  return std::span<const contracts::ReceivedSignal>{first, last};
}

inline auto InFlightSignalLedger::ValidateReadyForCycleClose(
    contracts::SimTime close_time) const -> contracts::Status {
  for(const auto& signal : signals_) {
    if(signal.last_effect_end_at() > close_time) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "In-flight signal extends beyond cycle close"});
    }
  }
  return {};
}

}  // namespace ns3_factory::runtime::internal
