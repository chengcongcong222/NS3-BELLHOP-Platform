#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/received_signal.hpp>

namespace ns3_factory::contracts {

// A single future PHY decision context centered on a caller-selected desired
// signal. This is not a receiver's persistent in-flight signal store.
class ReceiverWindow final {
 public:
  [[nodiscard]] static auto Create(
      NodeId receiver_node_id,
      ReceivedSignal desired_signal,
      std::vector<ReceivedSignal> overlapping_signals)
      -> Result<ReceiverWindow>;

  [[nodiscard]] constexpr auto receiver_node_id() const noexcept -> NodeId {
    return receiver_node_id_;
  }

  [[nodiscard]] constexpr auto desired_signal() const noexcept
      -> const ReceivedSignal& {
    return desired_signal_;
  }

  [[nodiscard]] auto overlapping_signals() const noexcept
      -> std::span<const ReceivedSignal> {
    return std::span<const ReceivedSignal>{overlapping_signals_};
  }

  auto operator==(const ReceiverWindow&) const -> bool = default;

 private:
  ReceiverWindow(NodeId receiver_node_id,
                 ReceivedSignal desired_signal,
                 std::vector<ReceivedSignal> overlapping_signals)
      : receiver_node_id_(receiver_node_id),
        desired_signal_(std::move(desired_signal)),
        overlapping_signals_(std::move(overlapping_signals)) {}

  NodeId receiver_node_id_;
  ReceivedSignal desired_signal_;
  std::vector<ReceivedSignal> overlapping_signals_;
};

inline auto ReceiverWindow::Create(
    NodeId receiver_node_id,
    ReceivedSignal desired_signal,
    std::vector<ReceivedSignal> overlapping_signals)
    -> Result<ReceiverWindow> {
  if(receiver_node_id != desired_signal.receiver_node_id()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "ReceiverWindow receiver does not match desired signal"});
  }

  for(std::size_t index = 0; index < overlapping_signals.size(); ++index) {
    const auto& signal = overlapping_signals[index];
    if(signal.receiver_node_id() != receiver_node_id) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "ReceiverWindow overlapping signal has wrong receiver"});
    }
    if(signal.transmission_id() == desired_signal.transmission_id()) {
      return std::unexpected(
          Error{ErrorCode::kAlreadyExists,
                "ReceiverWindow desired TransmissionId appears in overlaps"});
    }
    if(!HasP0SignalOverlap(desired_signal, signal)) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "ReceiverWindow signal does not overlap desired signal"});
    }
    for(std::size_t earlier = 0; earlier < index; ++earlier) {
      if(overlapping_signals[earlier].transmission_id() ==
         signal.transmission_id()) {
        return std::unexpected(
            Error{ErrorCode::kAlreadyExists,
                  "ReceiverWindow contains duplicate TransmissionId"});
      }
    }
  }

  std::sort(overlapping_signals.begin(),
            overlapping_signals.end(),
            [](const ReceivedSignal& lhs, const ReceivedSignal& rhs) {
              if(lhs.first_arrival_at() != rhs.first_arrival_at()) {
                return lhs.first_arrival_at() < rhs.first_arrival_at();
              }
              return lhs.transmission_id() < rhs.transmission_id();
            });

  return ReceiverWindow{receiver_node_id,
                        std::move(desired_signal),
                        std::move(overlapping_signals)};
}

}  // namespace ns3_factory::contracts
