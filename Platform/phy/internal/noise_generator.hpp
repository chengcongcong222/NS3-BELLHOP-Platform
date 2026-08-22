#pragma once

#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "waveform.hpp"

namespace ns3_factory::phy::internal {

struct AwgnNoiseConfig final {
  double pressure_psd_db_re_1upa2_per_hz;
  std::uint64_t deterministic_seed;

  auto operator==(const AwgnNoiseConfig&) const -> bool = default;
};

struct WenzNoiseConfig final {
  double shipping_activity;
  double wind_speed_meters_per_second;
  std::uint64_t deterministic_seed;

  auto operator==(const WenzNoiseConfig&) const -> bool = default;
};

using NoiseProfile = std::variant<AwgnNoiseConfig, WenzNoiseConfig>;

class DeterministicGaussian final {
 public:
  explicit DeterministicGaussian(std::uint64_t seed) : engine_(seed) {}

  [[nodiscard]] auto NextUnitComplex() -> std::complex<double> {
    constexpr auto kInverseSqrtTwo = 0.70710678118654752440;
    const auto pair = NextPair();
    return {pair.first * kInverseSqrtTwo,
            pair.second * kInverseSqrtTwo};
  }

 private:
  [[nodiscard]] auto UniformOpen01() -> double {
    constexpr auto kScale = 1.0 / 9007199254740992.0;
    const auto mantissa = engine_() >> 11U;
    return (static_cast<double>(mantissa) + 0.5) * kScale;
  }

  [[nodiscard]] auto NextPair() -> std::pair<double, double> {
    const auto radius = std::sqrt(-2.0 * std::log(UniformOpen01()));
    const auto angle =
        2.0 * std::numbers::pi_v<double> * UniformOpen01();
    return {radius * std::cos(angle), radius * std::sin(angle)};
  }

  std::mt19937_64 engine_;
};

[[nodiscard]] inline auto DbToLinearPower(double decibels) -> double {
  return std::pow(10.0, decibels / 10.0);
}

// Wenz-style component equations. Input is acoustic frequency in Hz; output
// is pressure PSD in dB re 1 uPa^2/Hz.
[[nodiscard]] inline auto WenzCompositePsdDb(
    double acoustic_frequency_hz,
    double shipping_activity,
    double wind_speed_meters_per_second) -> Result<double> {
  if(!std::isfinite(acoustic_frequency_hz) ||
     !std::isfinite(shipping_activity) ||
     !std::isfinite(wind_speed_meters_per_second) ||
     acoustic_frequency_hz <= 0.0 || shipping_activity < 0.0 ||
     shipping_activity > 1.0 || wind_speed_meters_per_second < 0.0) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "Wenz parameters are outside their valid ranges"});
  }

  const auto frequency_khz = acoustic_frequency_hz / 1000.0;
  const auto log_frequency = std::log10(frequency_khz);
  const auto turbulence_db = 17.0 - 30.0 * log_frequency;
  const auto shipping_db =
      40.0 + 20.0 * (shipping_activity - 0.5) +
      26.0 * log_frequency -
      60.0 * std::log10(frequency_khz + 0.03);
  const auto wind_db =
      50.0 + 7.5 * std::sqrt(wind_speed_meters_per_second) +
      20.0 * log_frequency -
      40.0 * std::log10(frequency_khz + 0.4);
  const auto thermal_db = -15.0 + 20.0 * log_frequency;

  const auto composite_linear =
      DbToLinearPower(turbulence_db) +
      DbToLinearPower(shipping_db) +
      DbToLinearPower(wind_db) +
      DbToLinearPower(thermal_db);
  const auto composite_db = 10.0 * std::log10(composite_linear);
  if(!std::isfinite(composite_db)) {
    return std::unexpected(
        Error{ErrorCode::kOverflow, "Wenz composite PSD is not finite"});
  }
  return composite_db;
}

[[nodiscard]] inline auto GenerateAwgn(
    const AwgnNoiseConfig& config,
    std::size_t sample_count,
    double sample_rate_hz) -> Result<WaveformBuffer> {
  if(!std::isfinite(config.pressure_psd_db_re_1upa2_per_hz) ||
     !std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0 ||
     sample_count == 0U) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "AWGN requires finite PSD, positive sample rate, and samples"});
  }

  const auto pressure_psd =
      DbToLinearPower(config.pressure_psd_db_re_1upa2_per_hz);
  const auto rms_amplitude = std::sqrt(pressure_psd * sample_rate_hz);
  if(!std::isfinite(rms_amplitude)) {
    return std::unexpected(
        Error{ErrorCode::kOverflow, "AWGN amplitude is not finite"});
  }

  DeterministicGaussian gaussian(config.deterministic_seed);
  std::vector<WaveformBuffer::Sample> samples;
  samples.reserve(sample_count);
  for(std::size_t index = 0; index < sample_count; ++index) {
    samples.push_back(gaussian.NextUnitComplex() * rms_amplitude);
  }
  return WaveformBuffer::Create(sample_rate_hz, std::move(samples));
}

inline void InverseFft(std::vector<std::complex<double>>& values) {
  const auto size = values.size();
  for(std::size_t index = 1U, reverse = 0U; index < size; ++index) {
    auto bit = size >> 1U;
    for(; (reverse & bit) != 0U; bit >>= 1U) {
      reverse ^= bit;
    }
    reverse ^= bit;
    if(index < reverse) {
      std::swap(values[index], values[reverse]);
    }
  }

  for(std::size_t length = 2U; length <= size; length <<= 1U) {
    const auto angle =
        2.0 * std::numbers::pi_v<double> /
        static_cast<double>(length);
    const auto step = std::polar(1.0, angle);
    for(std::size_t begin = 0U; begin < size; begin += length) {
      auto rotation = std::complex<double>{1.0, 0.0};
      for(std::size_t offset = 0U; offset < length / 2U; ++offset) {
        const auto even = values[begin + offset];
        const auto odd =
            values[begin + offset + length / 2U] * rotation;
        values[begin + offset] = even + odd;
        values[begin + offset + length / 2U] = even - odd;
        rotation *= step;
      }
    }
  }

  for(auto& value : values) {
    value /= static_cast<double>(size);
  }
}

[[nodiscard]] inline auto GenerateWenzNoise(
    const WenzNoiseConfig& config,
    std::size_t sample_count,
    double sample_rate_hz,
    double acoustic_carrier_hz) -> Result<WaveformBuffer> {
  if(!std::isfinite(sample_rate_hz) ||
     !std::isfinite(acoustic_carrier_hz) || sample_rate_hz <= 0.0 ||
     acoustic_carrier_hz <= sample_rate_hz / 2.0 ||
     sample_count == 0U) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "Wenz synthesis requires a carrier above baseband Nyquist"});
  }
  const auto parameter_check = WenzCompositePsdDb(
      acoustic_carrier_hz,
      config.shipping_activity,
      config.wind_speed_meters_per_second);
  if(!parameter_check) {
    return std::unexpected(parameter_check.error());
  }

  const auto fft_size = std::bit_ceil(sample_count);
  if(fft_size < sample_count || fft_size == 0U) {
    return std::unexpected(
        Error{ErrorCode::kOverflow, "Wenz FFT size overflow"});
  }

  DeterministicGaussian gaussian(config.deterministic_seed);
  std::vector<std::complex<double>> spectrum(fft_size);
  for(std::size_t bin = 0U; bin < fft_size; ++bin) {
    const auto signed_bin =
        bin <= fft_size / 2U
            ? static_cast<double>(bin)
            : static_cast<double>(bin) -
                  static_cast<double>(fft_size);
    const auto baseband_frequency =
        signed_bin * sample_rate_hz / static_cast<double>(fft_size);
    const auto acoustic_frequency =
        acoustic_carrier_hz + baseband_frequency;
    const auto psd_db = WenzCompositePsdDb(
        acoustic_frequency,
        config.shipping_activity,
        config.wind_speed_meters_per_second);
    if(!psd_db) {
      return std::unexpected(psd_db.error());
    }
    const auto spectrum_rms =
        std::sqrt(static_cast<double>(fft_size) * sample_rate_hz *
                  DbToLinearPower(*psd_db));
    if(!std::isfinite(spectrum_rms)) {
      return std::unexpected(
          Error{ErrorCode::kOverflow,
                "Wenz spectrum amplitude is not finite"});
    }
    spectrum[bin] =
        gaussian.NextUnitComplex() * spectrum_rms;
  }

  InverseFft(spectrum);
  spectrum.resize(sample_count);
  return WaveformBuffer::Create(sample_rate_hz, std::move(spectrum));
}

[[nodiscard]] inline auto GenerateNoise(
    const NoiseProfile& profile,
    std::size_t sample_count,
    double sample_rate_hz,
    double acoustic_carrier_hz) -> Result<WaveformBuffer> {
  return std::visit(
      [&](const auto& config) -> Result<WaveformBuffer> {
        using Config = std::decay_t<decltype(config)>;
        if constexpr(std::is_same_v<Config, AwgnNoiseConfig>) {
          return GenerateAwgn(config, sample_count, sample_rate_hz);
        } else {
          return GenerateWenzNoise(config,
                                   sample_count,
                                   sample_rate_hz,
                                   acoustic_carrier_hz);
        }
      },
      profile);
}

}  // namespace ns3_factory::phy::internal
