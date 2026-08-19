#pragma once

#include <cmath>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

namespace ns3_factory::contracts {

// One transmission's physical signal at one receiver before any decode
// decision. This is not a Reception or a decode result.
class ReceivedSignal final {
 public:
  [[nodiscard]] static auto Create(
      const TxEmission& emission,
      const ChannelFieldResponse& channel_response)
      -> Result<ReceivedSignal>;

  [[nodiscard]] constexpr auto emission() const noexcept
      -> const TxEmission& {
    return emission_;
  }

  [[nodiscard]] auto channel_response() const noexcept
      -> const ChannelFieldResponse& {
    return channel_response_;
  }

  [[nodiscard]] constexpr auto transmission_id() const noexcept
      -> TransmissionId {
    return emission_.transmission_id();
  }

  [[nodiscard]] constexpr auto receiver_node_id() const noexcept -> NodeId {
    return channel_response_.receiver_node_id();
  }

  [[nodiscard]] auto first_arrival_at() const noexcept -> SimTime {
    return *CheckedAdd(emission_.started_at(),
                       channel_response_.first_arrival_delay());
  }

  [[nodiscard]] auto latest_excess_delay() const noexcept -> SimDuration {
    auto latest = SimDuration::Zero();
    for(const auto& path : channel_response_.paths()) {
      if(path.excess_delay() > latest) {
        latest = path.excess_delay();
      }
    }
    return latest;
  }

  // Complete physical influence occupies
  // [first_arrival_at, last_effect_end_at).
  [[nodiscard]] auto last_effect_end_at() const noexcept -> SimTime {
    const auto latest_path_start =
        CheckedAdd(first_arrival_at(), latest_excess_delay());
    return *CheckedAdd(*latest_path_start, emission_.duration());
  }

  // P0 uses an ideal rectangular occupied band derived from Tx metadata.
  [[nodiscard]] constexpr auto lower_frequency_hz() const noexcept
      -> double {
    return emission_.center_frequency_hz() -
           emission_.bandwidth_hz() / 2.0;
  }

  [[nodiscard]] constexpr auto upper_frequency_hz() const noexcept
      -> double {
    return emission_.center_frequency_hz() +
           emission_.bandwidth_hz() / 2.0;
  }

  auto operator==(const ReceivedSignal&) const -> bool = default;

 private:
  ReceivedSignal(const TxEmission& emission,
                 const ChannelFieldResponse& channel_response)
      : emission_(emission), channel_response_(channel_response) {}

  TxEmission emission_;
  ChannelFieldResponse channel_response_;
};

inline auto ReceivedSignal::Create(
    const TxEmission& emission,
    const ChannelFieldResponse& channel_response)
    -> Result<ReceivedSignal> {
  if(emission.transmission_id() != channel_response.transmission_id()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "TxEmission identity does not match ChannelFieldResponse"});
  }

  const auto first_arrival_at =
      CheckedAdd(emission.started_at(),
                 channel_response.first_arrival_delay());
  if(!first_arrival_at) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "ReceivedSignal first arrival time overflow"});
  }

  auto latest_excess_delay = SimDuration::Zero();
  for(const auto& path : channel_response.paths()) {
    if(path.excess_delay() > latest_excess_delay) {
      latest_excess_delay = path.excess_delay();
    }
  }
  const auto latest_path_start =
      CheckedAdd(*first_arrival_at, latest_excess_delay);
  if(!latest_path_start ||
     !CheckedAdd(*latest_path_start, emission.duration())) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "ReceivedSignal physical influence time overflow"});
  }

  const auto lower_frequency_hz =
      emission.center_frequency_hz() - emission.bandwidth_hz() / 2.0;
  const auto upper_frequency_hz =
      emission.center_frequency_hz() + emission.bandwidth_hz() / 2.0;
  if(!std::isfinite(lower_frequency_hz) ||
     !std::isfinite(upper_frequency_hz)) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "ReceivedSignal occupied frequency boundary is not finite"});
  }

  return ReceivedSignal{emission, channel_response};
}

// Half-open temporal intervals overlap only when their intersection has
// positive duration. Touching end/start boundaries do not overlap.
[[nodiscard]] inline auto HasTemporalOverlap(const ReceivedSignal& lhs,
                                             const ReceivedSignal& rhs)
    noexcept -> bool {
  const auto overlap_start = lhs.first_arrival_at() < rhs.first_arrival_at()
                                 ? rhs.first_arrival_at()
                                 : lhs.first_arrival_at();
  const auto overlap_end = lhs.last_effect_end_at() <
                                   rhs.last_effect_end_at()
                               ? lhs.last_effect_end_at()
                               : rhs.last_effect_end_at();
  return overlap_start < overlap_end;
}

// Rectangular occupied bands overlap only when their intersection has
// positive width. Touching upper/lower boundaries do not overlap.
[[nodiscard]] constexpr auto HasSpectralOverlap(
    const ReceivedSignal& lhs,
    const ReceivedSignal& rhs) noexcept -> bool {
  const auto overlap_lower =
      lhs.lower_frequency_hz() < rhs.lower_frequency_hz()
          ? rhs.lower_frequency_hz()
          : lhs.lower_frequency_hz();
  const auto overlap_upper =
      lhs.upper_frequency_hz() < rhs.upper_frequency_hz()
          ? lhs.upper_frequency_hz()
          : rhs.upper_frequency_hz();
  return overlap_lower < overlap_upper;
}

// P0 overlap identifies candidates for later interference evaluation. It is
// not a decode failure or a statement about interference strength.
[[nodiscard]] inline auto HasP0SignalOverlap(const ReceivedSignal& lhs,
                                             const ReceivedSignal& rhs)
    noexcept -> bool {
  return lhs.receiver_node_id() == rhs.receiver_node_id() &&
         HasTemporalOverlap(lhs, rhs) && HasSpectralOverlap(lhs, rhs);
}

}  // namespace ns3_factory::contracts
