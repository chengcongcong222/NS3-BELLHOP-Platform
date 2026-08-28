#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/rx_quality.hpp>

namespace ns3_factory::phy::internal {

// Computes P0 modeled quality from the aggregate scalar received level and
// the already-integrated equivalent noise power for the occupied band.
// PropagationPath gains are intentionally not applied a second time.
[[nodiscard]] inline auto ComputeP0ModeledBpskAwgnQuality(
    const contracts::RxDecodeRequest& request,
    std::uint64_t bit_rate_bits_per_second)
    -> contracts::Result<contracts::RxQualityEvidence> {
  if(bit_rate_bits_per_second == 0) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Scalar BER model requires a positive bit rate"});
  }
  const auto& desired = request.receiver_window().desired_signal();
  const auto received_level =
      contracts::ComputeP0ScalarReceivedLevelDbRe1upa(desired);
  if(!received_level) return std::unexpected(received_level.error());
  const auto noise_level =
      request.noise_observation().equivalent_noise_power_db_re_1upa2();
  const auto bandwidth_hz = desired.emission().bandwidth_hz();
  if(!std::isfinite(noise_level) || !std::isfinite(bandwidth_hz) ||
     bandwidth_hz <= 0.0) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOutOfRange,
                         "Scalar BER noise and bandwidth must be finite and "
                         "bandwidth must be positive"});
  }

  const auto signal_to_noise_ratio_db = *received_level - noise_level;
  const auto bandwidth_to_rate =
      static_cast<long double>(bandwidth_hz) /
      static_cast<long double>(bit_rate_bits_per_second);
  const auto eb_n0_db =
      static_cast<long double>(signal_to_noise_ratio_db) +
      10.0L * std::log10(bandwidth_to_rate);
  const auto eb_n0_linear = std::pow(10.0L, eb_n0_db / 10.0L);
  if(!std::isfinite(signal_to_noise_ratio_db) ||
     !std::isfinite(bandwidth_to_rate) || bandwidth_to_rate <= 0.0L ||
     !std::isfinite(eb_n0_db) || !std::isfinite(eb_n0_linear) ||
     eb_n0_linear < 0.0L) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Scalar BER intermediate value is not finite"});
  }
  const auto bit_error_rate =
      0.5L * std::erfc(std::sqrt(eb_n0_linear));
  if(!std::isfinite(bit_error_rate) || bit_error_rate < 0.0L ||
     bit_error_rate > 1.0L) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Scalar BER result is outside its finite domain"});
  }
  return contracts::RxQualityEvidence::Create(
      signal_to_noise_ratio_db,
      static_cast<double>(eb_n0_db),
      static_cast<double>(bit_error_rate),
      contracts::RxQualityEvidenceSource::kModeled);
}

class ScalarBerRxPhyDecorator final : public contracts::IRxPhy {
 public:
  [[nodiscard]] static auto Create(
      const contracts::IRxPhy& inner,
      std::uint64_t bit_rate_bits_per_second)
      -> contracts::Result<ScalarBerRxPhyDecorator> {
    if(bit_rate_bits_per_second == 0) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Scalar BER decorator requires a positive bit "
                           "rate"});
    }
    return ScalarBerRxPhyDecorator{inner, bit_rate_bits_per_second};
  }

  [[nodiscard]] auto Decode(
      const contracts::RxDecodeRequest& request) const
      -> contracts::Result<contracts::RxDecodeResult> override {
    auto inner_result = inner_.get().Decode(request);
    if(!inner_result) return std::unexpected(inner_result.error());
    const auto identity =
        contracts::ValidateRxDecodeResultIdentity(request, *inner_result);
    if(!identity) return std::unexpected(identity.error());
    const auto without_quality = [&inner_result] {
      return contracts::RxDecodeResult::Create(
          inner_result->transmission_id(),
          inner_result->packet_id(),
          inner_result->receiver_node_id(),
          inner_result->outcome());
    };
    if(!request.receiver_window().overlapping_signals().empty()) {
      return without_quality();
    }
    auto quality = ComputeP0ModeledBpskAwgnQuality(
        request, bit_rate_bits_per_second_);
    if(!quality) return without_quality();
    return contracts::RxDecodeResult::Create(
        inner_result->transmission_id(),
        inner_result->packet_id(),
        inner_result->receiver_node_id(),
        inner_result->outcome(),
        std::move(*quality));
  }

  [[nodiscard]] constexpr auto bit_rate_bits_per_second() const noexcept
      -> std::uint64_t {
    return bit_rate_bits_per_second_;
  }

 private:
  constexpr ScalarBerRxPhyDecorator(
      const contracts::IRxPhy& inner,
      std::uint64_t bit_rate_bits_per_second) noexcept
      : inner_(inner), bit_rate_bits_per_second_(bit_rate_bits_per_second) {}

  std::reference_wrapper<const contracts::IRxPhy> inner_;
  std::uint64_t bit_rate_bits_per_second_;
};

}  // namespace ns3_factory::phy::internal
