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

// Sparse R maps provider path gains to the waveform delay grid.
// R[waveform_delay_index, path_index] is a real interpolation weight.
struct DelayAlignmentEntry final {
  std::size_t waveform_delay_index;
  std::size_t path_index;
  double weight;
};

struct DelayAlignmentMatrix final {
  std::size_t row_count;
  std::size_t column_count;
  std::vector<DelayAlignmentEntry> entries;

  [[nodiscard]] auto Multiply(
      std::span<const std::complex<double>> path_gains) const
      -> Result<std::vector<std::complex<double>>> {
    if(path_gains.size() != column_count) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "Delay alignment matrix column count does not match gains"});
    }
    for(const auto gain : path_gains) {
      if(!std::isfinite(gain.real()) || !std::isfinite(gain.imag())) {
        return std::unexpected(
            Error{ErrorCode::kInvalidArgument,
                  "Delay alignment matrix gains must be finite"});
      }
    }
    std::vector<std::complex<double>> aligned(row_count);
    for(const auto& entry : entries) {
      aligned[entry.waveform_delay_index] +=
          entry.weight * path_gains[entry.path_index];
    }
    return aligned;
  }
};

[[nodiscard]] inline auto BuildDelayAlignmentMatrix(
    std::span<const MultipathTap> taps, double waveform_sample_rate_hz)
    -> Result<DelayAlignmentMatrix> {
  const auto validation = ValidateMultipathTaps(taps);
  if(!validation) return std::unexpected(validation.error());
  if(!std::isfinite(waveform_sample_rate_hz) ||
     waveform_sample_rate_hz <= 0.0) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "Waveform sample rate must be finite and positive"});
  }
  if(taps.size() > std::numeric_limits<std::size_t>::max() / 2U) {
    return std::unexpected(
        Error{ErrorCode::kOverflow, "Too many channel paths"});
  }

  std::vector<DelayAlignmentEntry> entries;
  entries.reserve(taps.size() * 2U);
  long double maximum_position = 0.0L;
  for(std::size_t path_index = 0; path_index < taps.size(); ++path_index) {
    const auto position =
        static_cast<long double>(taps[path_index].excess_delay_seconds) *
        static_cast<long double>(waveform_sample_rate_hz);
    if(!std::isfinite(position) ||
       position > static_cast<long double>(
                      std::numeric_limits<std::size_t>::max() - 1U)) {
      return std::unexpected(
          Error{ErrorCode::kOverflow,
                "Multipath delay exceeds waveform addressable range"});
    }
    maximum_position = std::max(maximum_position, position);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto fraction = static_cast<double>(position - lower);
    entries.push_back({lower, path_index, 1.0 - fraction});
    if(fraction > 0.0) {
      entries.push_back({lower + 1U, path_index, fraction});
    }
  }
  const auto row_count =
      static_cast<std::size_t>(std::ceil(maximum_position)) + 1U;
  return DelayAlignmentMatrix{row_count, taps.size(), std::move(entries)};
}

// Provider path gains are absolute. Aggregate transmission loss is not
// applied again here. First h_wave = R * h_path, then y = h_wave * x.
[[nodiscard]] inline auto ApplyMultipath(
    const WaveformBuffer& transmitted,
    std::span<const MultipathTap> taps) -> Result<WaveformBuffer> {
  const auto alignment =
      BuildDelayAlignmentMatrix(taps, transmitted.sample_rate_hz());
  if(!alignment) return std::unexpected(alignment.error());
  const auto tail_samples = alignment->row_count - 1U;
  if(tail_samples >
     std::numeric_limits<std::size_t>::max() - transmitted.sample_count()) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "Multipath delay exceeds waveform addressable range"});
  }

  std::vector<std::complex<double>> gains;
  gains.reserve(taps.size());
  for(const auto& tap : taps) gains.push_back(tap.pressure_gain);
  const auto aligned = alignment->Multiply(gains);
  if(!aligned) return std::unexpected(aligned.error());

  std::vector<WaveformBuffer::Sample> received(
      transmitted.sample_count() + tail_samples,
      WaveformBuffer::Sample{});

  for(std::size_t delay_index = 0; delay_index < aligned->size();
      ++delay_index) {
    if((*aligned)[delay_index] == WaveformBuffer::Sample{}) continue;
    for(std::size_t source_index = 0;
        source_index < transmitted.sample_count();
        ++source_index) {
      received[source_index + delay_index] +=
          transmitted.samples()[source_index] * (*aligned)[delay_index];
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
