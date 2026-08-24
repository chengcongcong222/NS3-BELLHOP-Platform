#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/time.hpp>

#include "../acoustic_field_asset.hpp"
#include "../environment_coordinate_frame.hpp"
#include "bellhop_raw_arrival_bundle.hpp"

namespace ns3_factory::environment::internal::import {

// Offline normalization boundary from the confirmed Bellhop 2D ASCII raw
// arrival semantics to the immutable runtime acoustic-field asset.
class BellhopRawArrivalNormalizer final {
 public:
  [[nodiscard]] static auto Normalize(
      const BellhopRawArrivalBundle& bundle,
      std::uint32_t format_version,
      std::string provenance,
      EnvironmentCoordinateFrame coordinate_frame)
      -> contracts::Result<AcousticFieldAsset>;
};

namespace detail {

struct NormalizedBellhopPath final {
  contracts::SimDuration absolute_delay;
  double pressure_gain_linear;
  double phase_radians;
};

[[nodiscard]] inline auto BellhopSecondsToNanoseconds(double seconds)
    -> contracts::Result<contracts::SimDuration> {
  constexpr auto kNanosecondsPerSecond = 1'000'000'000.0L;
  const auto nanoseconds =
      static_cast<long double>(seconds) * kNanosecondsPerSecond;
  if(!std::isfinite(nanoseconds)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Bellhop real delay conversion overflowed"});
  }
  const auto rounded = std::round(nanoseconds);
  constexpr auto kMaximum =
      std::numeric_limits<contracts::NanosecondCount>::max();
  if(rounded < 0.0L ||
     rounded > static_cast<long double>(kMaximum)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Bellhop real delay is outside int64 "
                         "nanoseconds"});
  }
  return contracts::SimDuration::FromNanoseconds(
      static_cast<contracts::NanosecondCount>(rounded));
}

[[nodiscard]] inline auto NormalizeBellhopPath(
    const BellhopRawArrival& raw,
    long double omega) -> contracts::Result<NormalizedBellhopPath> {
  const auto exponent =
      omega * static_cast<long double>(raw.raw_delay_imag_seconds());
  if(!std::isfinite(exponent)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Bellhop imaginary-delay exponent is not finite"});
  }
  const auto imaginary_delay_scale = std::exp(exponent);
  if(!std::isfinite(imaginary_delay_scale)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Bellhop imaginary-delay scale overflowed"});
  }
  const auto gain = static_cast<long double>(raw.raw_magnitude()) *
                    imaginary_delay_scale;
  if(!std::isfinite(gain) || gain < 0.0L) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Bellhop effective pressure gain is not finite"});
  }
  const auto double_gain = static_cast<double>(gain);
  if(!std::isfinite(double_gain) ||
     (raw.raw_magnitude() != 0.0 && double_gain == 0.0)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Bellhop effective pressure gain cannot be "
                         "represented as double"});
  }

  constexpr auto kDegreesPerHalfTurn = 180.0L;
  const auto phase =
      static_cast<long double>(raw.raw_phase_degrees()) *
      std::numbers::pi_v<long double> / kDegreesPerHalfTurn;
  const auto double_phase = static_cast<double>(phase);
  if(!std::isfinite(phase) || !std::isfinite(double_phase)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Bellhop phase conversion is not finite"});
  }

  auto delay =
      BellhopSecondsToNanoseconds(raw.raw_delay_real_seconds());
  if(!delay) return std::unexpected(delay.error());
  return NormalizedBellhopPath{*delay, double_gain, double_phase};
}

[[nodiscard]] inline auto NormalizeBellhopCell(
    const BellhopRawArrivalCell& raw_cell,
    double frequency_hz) -> contracts::Result<AcousticFieldCell> {
  if(raw_cell.arrivals.empty()) {
    return AcousticFieldCell{AcousticFieldNoArrivalCell{}};
  }

  const auto omega = 2.0L * std::numbers::pi_v<long double> *
                     static_cast<long double>(frequency_hz);
  if(!std::isfinite(omega)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Bellhop angular frequency is not finite"});
  }

  std::vector<NormalizedBellhopPath> normalized;
  normalized.reserve(raw_cell.arrivals.size());
  long double aggregate_gain = 0.0L;
  auto first_arrival = contracts::SimDuration::FromNanoseconds(
      std::numeric_limits<contracts::NanosecondCount>::max());
  for(const auto& raw : raw_cell.arrivals) {
    auto path = NormalizeBellhopPath(raw, omega);
    if(!path) return std::unexpected(path.error());
    aggregate_gain = std::hypot(
        aggregate_gain,
        static_cast<long double>(path->pressure_gain_linear));
    if(!std::isfinite(aggregate_gain)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Bellhop aggregate pressure gain overflowed"});
    }
    if(path->absolute_delay < first_arrival) {
      first_arrival = path->absolute_delay;
    }
    normalized.push_back(*path);
  }

  if(aggregate_gain == 0.0L) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "Non-empty Bellhop arrival cell has zero "
                         "aggregate pressure gain"});
  }
  const auto aggregate_loss = -20.0L * std::log10(aggregate_gain);
  const auto double_loss = static_cast<double>(aggregate_loss);
  if(!std::isfinite(aggregate_loss) || !std::isfinite(double_loss)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Bellhop aggregate transmission loss is not "
                         "finite"});
  }

  std::vector<contracts::PropagationPath> paths;
  paths.reserve(normalized.size());
  for(const auto& normalized_path : normalized) {
    const auto excess = contracts::CheckedSubtract(
        normalized_path.absolute_delay, first_arrival);
    if(!excess) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Bellhop excess-delay subtraction overflowed"});
    }
    auto path = contracts::PropagationPath::Create(
        *excess,
        normalized_path.pressure_gain_linear,
        normalized_path.phase_radians);
    if(!path) return std::unexpected(path.error());
    paths.push_back(std::move(*path));
  }

  return AcousticFieldCell{AcousticFieldSignalCell{
      double_loss, first_arrival, std::move(paths)}};
}

}  // namespace detail

inline auto BellhopRawArrivalNormalizer::Normalize(
    const BellhopRawArrivalBundle& bundle,
    std::uint32_t format_version,
    std::string provenance,
    EnvironmentCoordinateFrame coordinate_frame)
    -> contracts::Result<AcousticFieldAsset> {
  const auto datasets = bundle.datasets();
  const auto& first_dataset = datasets.front();
  const auto cell_count = CheckedGridCellCount(
      datasets.size(),
      first_dataset.source_depths_m().size(),
      first_dataset.receiver_depths_m().size(),
      first_dataset.receiver_ranges_m().size());
  if(!cell_count) return std::unexpected(cell_count.error());

  std::vector<AcousticFieldCell> cells;
  cells.reserve(*cell_count);
  for(const auto& dataset : datasets) {
    for(const auto& raw_cell : dataset.cells()) {
      auto cell = detail::NormalizeBellhopCell(
          raw_cell, dataset.frequency_hz());
      if(!cell) return std::unexpected(cell.error());
      cells.push_back(std::move(*cell));
    }
  }

  return AcousticFieldAsset::Create(
      format_version,
      std::move(provenance),
      coordinate_frame,
      std::vector<double>{bundle.frequencies_hz().begin(),
                          bundle.frequencies_hz().end()},
      std::vector<double>{first_dataset.source_depths_m().begin(),
                          first_dataset.source_depths_m().end()},
      std::vector<double>{first_dataset.receiver_depths_m().begin(),
                          first_dataset.receiver_depths_m().end()},
      std::vector<double>{first_dataset.receiver_ranges_m().begin(),
                          first_dataset.receiver_ranges_m().end()},
      std::move(cells));
}

}  // namespace ns3_factory::environment::internal::import
