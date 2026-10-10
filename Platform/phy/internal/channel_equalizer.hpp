#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "channel_processor.hpp"
#include "fft.hpp"
#include "waveform.hpp"

namespace ns3_factory::phy::internal {

enum class ChannelEqualizationMode : std::uint8_t {
  kDisabled = 0,
  kIdealFrequencyDomainMmse = 1,
};

struct ChannelEqualizationConfig final {
  ChannelEqualizationMode mode;

  auto operator==(const ChannelEqualizationConfig&) const -> bool = default;
};

namespace channel_equalizer_detail {

[[nodiscard]] inline auto MeanPower(const WaveformBuffer& waveform) -> double {
  long double sum = 0.0L;
  for(const auto sample : waveform.samples()) {
    sum += static_cast<long double>(std::norm(sample));
  }
  return static_cast<double>(sum /
                             static_cast<long double>(waveform.sample_count()));
}

}  // namespace channel_equalizer_detail

// Ideal simulation equalizer: the receiver knows the channel impulse response
// and the generated noise power. Zero padding turns the causal linear
// convolution into an equivalent frequency-domain product. The MMSE term is
// noise power divided by transmitted symbol power; in the noiseless case a
// machine-precision floor only protects spectral nulls from division by zero.
[[nodiscard]] inline auto EqualizeKnownChannel(
    const WaveformBuffer& received,
    std::span<const MultipathTap> taps,
    std::size_t expected_sample_count,
    double transmitted_symbol_power_upa2,
    double noise_power_upa2,
    const ChannelEqualizationConfig& config) -> Result<WaveformBuffer> {
  if(config.mode == ChannelEqualizationMode::kDisabled) return received;
  if(config.mode != ChannelEqualizationMode::kIdealFrequencyDomainMmse) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "Unknown channel equalization mode"});
  }
  if(expected_sample_count == 0U ||
     expected_sample_count > received.sample_count() ||
     !std::isfinite(transmitted_symbol_power_upa2) ||
     transmitted_symbol_power_upa2 <= 0.0 ||
     !std::isfinite(noise_power_upa2) || noise_power_upa2 < 0.0) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "MMSE equalization requires valid signal, noise, and length"});
  }

  const auto impulse_response =
      BuildSampledImpulseResponse(taps, received.sample_rate_hz());
  if(!impulse_response) return std::unexpected(impulse_response.error());
  if(impulse_response->size() - 1U >
     std::numeric_limits<std::size_t>::max() - expected_sample_count) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "Equalized convolution length exceeds addressable range"});
  }
  const auto convolution_count =
      expected_sample_count + impulse_response->size() - 1U;
  if(received.sample_count() < convolution_count) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Received waveform is shorter than the known convolution"});
  }
  constexpr auto maximum_fft_size =
      std::size_t{1}
      << (std::numeric_limits<std::size_t>::digits - 1U);
  if(convolution_count > maximum_fft_size) {
    return std::unexpected(
        Error{ErrorCode::kOverflow, "Equalizer FFT size overflow"});
  }
  const auto fft_size = std::bit_ceil(convolution_count);
  if(fft_size < convolution_count || fft_size == 0U) {
    return std::unexpected(
        Error{ErrorCode::kOverflow, "Equalizer FFT size overflow"});
  }

  std::vector<std::complex<double>> received_spectrum(fft_size);
  std::copy_n(received.samples().begin(), convolution_count,
              received_spectrum.begin());
  std::vector<std::complex<double>> channel_spectrum(fft_size);
  std::copy(impulse_response->begin(), impulse_response->end(),
            channel_spectrum.begin());
  auto transformed = TransformRadix2(received_spectrum, false);
  if(!transformed) return std::unexpected(transformed.error());
  transformed = TransformRadix2(channel_spectrum, false);
  if(!transformed) return std::unexpected(transformed.error());

  const auto physical_regularization =
      noise_power_upa2 / transmitted_symbol_power_upa2;
  if(!std::isfinite(physical_regularization)) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "Equalizer regularization is not finite"});
  }
  auto maximum_channel_power = 0.0;
  for(const auto value : channel_spectrum) {
    maximum_channel_power = std::max(maximum_channel_power, std::norm(value));
  }
  if(!std::isfinite(maximum_channel_power) || maximum_channel_power <= 0.0) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Known channel has no recoverable spectral energy"});
  }
  const auto numerical_floor =
      std::max(maximum_channel_power *
                   std::numeric_limits<double>::epsilon(),
               std::numeric_limits<double>::min());
  const auto regularization =
      std::max(physical_regularization, numerical_floor);

  for(std::size_t index = 0U; index < fft_size; ++index) {
    const auto denominator =
        std::norm(channel_spectrum[index]) + regularization;
    received_spectrum[index] =
        std::conj(channel_spectrum[index]) * received_spectrum[index] /
        denominator;
  }
  transformed = TransformRadix2(received_spectrum, true);
  if(!transformed) return std::unexpected(transformed.error());
  received_spectrum.resize(expected_sample_count);
  return WaveformBuffer::Create(received.sample_rate_hz(),
                                std::move(received_spectrum));
}

}  // namespace ns3_factory::phy::internal
