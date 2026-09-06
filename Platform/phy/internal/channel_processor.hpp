#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/errors.hpp>

#include "waveform.hpp"

namespace ns3_factory::phy::internal {

struct MultipathTap final {
  double excess_delay_seconds;
  std::complex<double> pressure_gain;

  auto operator==(const MultipathTap&) const -> bool = default;
};

[[nodiscard]] inline auto BuildMultipathTaps(
    const contracts::ChannelFieldResponse& response)
    -> Result<std::vector<MultipathTap>> {
  if(response.paths().empty()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Waveform processing requires path-aware channel data"});
  }

  std::vector<MultipathTap> taps;
  taps.reserve(response.paths().size());
  for(const auto& path : response.paths()) {
    taps.push_back(MultipathTap{
        static_cast<double>(path.excess_delay().nanoseconds()) / 1.0e9,
        std::polar(path.pressure_gain_linear(), path.phase_radians())});
  }
  return taps;
}

// Normalized Bellhop phase excludes real propagation delay (ADR-0001 2.39).
// A complex envelope still needs the carrier's relative-delay phase. Work in
// the first-arrival reference frame: the common first-arrival carrier phase is
// removed by ideal synchronization, while relative path interference remains.
[[nodiscard]] inline auto BuildBasebandMultipathTaps(
    const contracts::ChannelFieldResponse& response, double carrier_hz)
    -> Result<std::vector<MultipathTap>> {
  if(!std::isfinite(carrier_hz) || carrier_hz <= 0.0) {
    return std::unexpected(Error{ErrorCode::kInvalidArgument,
                                 "Baseband carrier must be finite and positive"});
  }
  auto taps = BuildMultipathTaps(response);
  if(!taps) return std::unexpected(taps.error());
  for(auto& tap : *taps) {
    const auto cycles = static_cast<long double>(carrier_hz) *
                        static_cast<long double>(tap.excess_delay_seconds);
    if(!std::isfinite(cycles)) {
      return std::unexpected(Error{ErrorCode::kOverflow,
                                   "Carrier propagation phase overflow"});
    }
    const auto phase = -2.0L * std::numbers::pi_v<long double> *
                       std::remainder(cycles, 1.0L);
    tap.pressure_gain *= std::polar(1.0, static_cast<double>(phase));
  }
  return taps;
}

[[nodiscard]] inline auto ValidateMultipathTaps(
    std::span<const MultipathTap> taps) -> contracts::Status {
  if(taps.empty()) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "Multipath profile must contain at least one tap"});
  }

  auto contains_zero_delay = false;
  for(const auto& tap : taps) {
    if(!std::isfinite(tap.excess_delay_seconds) ||
       !std::isfinite(tap.pressure_gain.real()) ||
       !std::isfinite(tap.pressure_gain.imag()) ||
       tap.excess_delay_seconds < 0.0) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "Multipath delay and gain must be finite"});
    }
    contains_zero_delay =
        contains_zero_delay || tap.excess_delay_seconds == 0.0;
  }
  if(!contains_zero_delay) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Multipath profile must contain a zero-excess-delay tap"});
  }
  return {};
}

// Provider path gains are absolute. Aggregate transmission loss is not
// applied again here. Fractional path delays use deterministic interpolation.
[[nodiscard]] inline auto ApplyMultipath(
    const WaveformBuffer& transmitted,
    std::span<const MultipathTap> taps) -> Result<WaveformBuffer> {
  const auto validation = ValidateMultipathTaps(taps);
  if(!validation) {
    return std::unexpected(validation.error());
  }

  double maximum_delay_samples = 0.0;
  for(const auto& tap : taps) {
    maximum_delay_samples =
        std::max(maximum_delay_samples,
                 tap.excess_delay_seconds * transmitted.sample_rate_hz());
  }
  if(!std::isfinite(maximum_delay_samples) ||
     maximum_delay_samples >
         static_cast<double>(
             std::numeric_limits<std::size_t>::max() -
             transmitted.sample_count())) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "Multipath delay exceeds waveform addressable range"});
  }

  const auto tail_samples =
      static_cast<std::size_t>(std::ceil(maximum_delay_samples));
  std::vector<WaveformBuffer::Sample> received(
      transmitted.sample_count() + tail_samples,
      WaveformBuffer::Sample{});

  for(const auto& tap : taps) {
    const auto delay_samples =
        tap.excess_delay_seconds * transmitted.sample_rate_hz();
    const auto integer_delay =
        static_cast<std::size_t>(std::floor(delay_samples));
    const auto fraction =
        delay_samples - static_cast<double>(integer_delay);

    for(std::size_t source_index = 0;
        source_index < transmitted.sample_count();
        ++source_index) {
      const auto delayed =
          transmitted.samples()[source_index] * tap.pressure_gain;
      const auto target_index = source_index + integer_delay;
      received[target_index] += delayed * (1.0 - fraction);
      if(fraction > 0.0 && target_index + 1U < received.size()) {
        received[target_index + 1U] += delayed * fraction;
      }
    }
  }

  return WaveformBuffer::Create(transmitted.sample_rate_hz(),
                                std::move(received));
}

[[nodiscard]] inline auto IdealReferencePhase(
    std::span<const MultipathTap> taps) -> Result<double> {
  const auto validation = ValidateMultipathTaps(taps);
  if(!validation) {
    return std::unexpected(validation.error());
  }

  const MultipathTap* reference = nullptr;
  for(const auto& tap : taps) {
    if(tap.excess_delay_seconds == 0.0 &&
       (reference == nullptr ||
        std::norm(tap.pressure_gain) >
            std::norm(reference->pressure_gain))) {
      reference = &tap;
    }
  }
  if(reference == nullptr || std::norm(reference->pressure_gain) == 0.0) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Ideal phase sync requires a non-zero first tap"});
  }
  return std::arg(reference->pressure_gain);
}

}  // namespace ns3_factory::phy::internal
