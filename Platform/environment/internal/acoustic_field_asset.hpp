#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/time.hpp>

#include "environment_coordinate_frame.hpp"

namespace ns3_factory::environment::internal {

struct AcousticFieldSignalCell final {
  double aggregate_transmission_loss_db;
  contracts::SimDuration first_arrival_delay;
  std::vector<contracts::PropagationPath> paths;

  auto operator==(const AcousticFieldSignalCell&) const -> bool = default;
};

struct AcousticFieldNoArrivalCell final {
  auto operator==(const AcousticFieldNoArrivalCell&) const -> bool = default;
};

using AcousticFieldCell =
    std::variant<AcousticFieldSignalCell, AcousticFieldNoArrivalCell>;

[[nodiscard]] inline auto CheckedGridCellCount(
    std::size_t frequency_count,
    std::size_t source_depth_count,
    std::size_t receiver_depth_count,
    std::size_t range_count) -> contracts::Result<std::size_t> {
  std::size_t result = 1U;
  for(const auto dimension : {frequency_count,
                              source_depth_count,
                              receiver_depth_count,
                              range_count}) {
    if(dimension != 0U &&
       result > std::numeric_limits<std::size_t>::max() / dimension) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Acoustic field grid dimension product "
                           "overflows"});
    }
    result *= dimension;
  }
  return result;
}

// Immutable normalized run asset. Cells use the documented flat order:
// frequency-major -> source-depth -> receiver-depth -> horizontal-range.
class AcousticFieldAsset final {
 public:
  [[nodiscard]] static auto Create(
      std::uint32_t format_version,
      std::string provenance,
      EnvironmentCoordinateFrame coordinate_frame,
      std::vector<double> frequency_hz,
      std::vector<double> source_depth_m,
      std::vector<double> receiver_depth_m,
      std::vector<double> horizontal_range_m,
      std::vector<AcousticFieldCell> cells)
      -> contracts::Result<AcousticFieldAsset>;

  [[nodiscard]] constexpr auto format_version() const noexcept
      -> std::uint32_t {
    return format_version_;
  }

  [[nodiscard]] constexpr auto provenance() const noexcept
      -> const std::string& {
    return provenance_;
  }

  [[nodiscard]] constexpr auto coordinate_frame() const noexcept
      -> const EnvironmentCoordinateFrame& {
    return coordinate_frame_;
  }

  [[nodiscard]] auto frequency_hz() const noexcept
      -> std::span<const double> {
    return frequency_hz_;
  }

  [[nodiscard]] auto source_depth_m() const noexcept
      -> std::span<const double> {
    return source_depth_m_;
  }

  [[nodiscard]] auto receiver_depth_m() const noexcept
      -> std::span<const double> {
    return receiver_depth_m_;
  }

  [[nodiscard]] auto horizontal_range_m() const noexcept
      -> std::span<const double> {
    return horizontal_range_m_;
  }

  [[nodiscard]] auto cells() const noexcept
      -> std::span<const AcousticFieldCell> {
    return cells_;
  }

  [[nodiscard]] auto cell(std::size_t frequency_index,
                          std::size_t source_depth_index,
                          std::size_t receiver_depth_index,
                          std::size_t range_index) const noexcept
      -> const AcousticFieldCell& {
    return cells_[FlattenIndex(frequency_index,
                               source_depth_index,
                               receiver_depth_index,
                               range_index)];
  }

 private:
  AcousticFieldAsset(std::uint32_t format_version,
                     std::string provenance,
                     EnvironmentCoordinateFrame coordinate_frame,
                     std::vector<double> frequency_hz,
                     std::vector<double> source_depth_m,
                     std::vector<double> receiver_depth_m,
                     std::vector<double> horizontal_range_m,
                     std::vector<AcousticFieldCell> cells)
      : format_version_(format_version),
        provenance_(std::move(provenance)),
        coordinate_frame_(coordinate_frame),
        frequency_hz_(std::move(frequency_hz)),
        source_depth_m_(std::move(source_depth_m)),
        receiver_depth_m_(std::move(receiver_depth_m)),
        horizontal_range_m_(std::move(horizontal_range_m)),
        cells_(std::move(cells)) {}

  [[nodiscard]] auto FlattenIndex(
      std::size_t frequency_index,
      std::size_t source_depth_index,
      std::size_t receiver_depth_index,
      std::size_t range_index) const noexcept -> std::size_t {
    return (((frequency_index * source_depth_m_.size() +
              source_depth_index) *
                 receiver_depth_m_.size() +
             receiver_depth_index) *
                horizontal_range_m_.size() +
            range_index);
  }

  std::uint32_t format_version_;
  std::string provenance_;
  EnvironmentCoordinateFrame coordinate_frame_;
  std::vector<double> frequency_hz_;
  std::vector<double> source_depth_m_;
  std::vector<double> receiver_depth_m_;
  std::vector<double> horizontal_range_m_;
  std::vector<AcousticFieldCell> cells_;
};

namespace detail {

[[nodiscard]] inline auto ValidateAxis(std::span<const double> axis,
                                       bool require_positive,
                                       const char* label)
    -> contracts::Status {
  if(axis.empty()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         std::string{label} + " axis must not be empty"});
  }
  for(std::size_t index = 0U; index < axis.size(); ++index) {
    if(!std::isfinite(axis[index])) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           std::string{label} +
                               " axis values must be finite"});
    }
    if((require_positive && axis[index] <= 0.0) ||
       (!require_positive && axis[index] < 0.0)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           std::string{label} +
                               " axis values are outside their domain"});
    }
    if(index != 0U && axis[index - 1U] >= axis[index]) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           std::string{label} +
                               " axis must be strictly increasing"});
    }
  }
  return {};
}

inline auto CanonicalPathLess(const contracts::PropagationPath& lhs,
                              const contracts::PropagationPath& rhs) -> bool {
  if(lhs.excess_delay() != rhs.excess_delay()) {
    return lhs.excess_delay() < rhs.excess_delay();
  }
  if(lhs.pressure_gain_linear() != rhs.pressure_gain_linear()) {
    return lhs.pressure_gain_linear() < rhs.pressure_gain_linear();
  }
  return lhs.phase_radians() < rhs.phase_radians();
}

}  // namespace detail

inline auto AcousticFieldAsset::Create(
    std::uint32_t format_version,
    std::string provenance,
    EnvironmentCoordinateFrame coordinate_frame,
    std::vector<double> frequency_hz,
    std::vector<double> source_depth_m,
    std::vector<double> receiver_depth_m,
    std::vector<double> horizontal_range_m,
    std::vector<AcousticFieldCell> cells)
    -> contracts::Result<AcousticFieldAsset> {
  if(format_version == 0U) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Acoustic field format version must be non-zero"});
  }
  for(const auto& validation :
      {detail::ValidateAxis(frequency_hz, true, "Frequency"),
       detail::ValidateAxis(source_depth_m, false, "Source depth"),
       detail::ValidateAxis(receiver_depth_m, false, "Receiver depth"),
       detail::ValidateAxis(horizontal_range_m, false,
                            "Horizontal range")}) {
    if(!validation) return std::unexpected(validation.error());
  }

  const auto expected_cell_count =
      CheckedGridCellCount(frequency_hz.size(),
                           source_depth_m.size(),
                           receiver_depth_m.size(),
                           horizontal_range_m.size());
  if(!expected_cell_count) {
    return std::unexpected(expected_cell_count.error());
  }
  if(cells.size() != *expected_cell_count) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Acoustic field cell count does not match axes"});
  }

  for(auto& cell : cells) {
    auto* signal = std::get_if<AcousticFieldSignalCell>(&cell);
    if(signal == nullptr) continue;
    if(!std::isfinite(signal->aggregate_transmission_loss_db)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Acoustic field transmission loss must be "
                           "finite"});
    }
    if(signal->first_arrival_delay < contracts::SimDuration::Zero()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           "Acoustic field first arrival delay must be "
                           "non-negative"});
    }
    std::sort(signal->paths.begin(),
              signal->paths.end(),
              detail::CanonicalPathLess);
    if(!signal->paths.empty() &&
       signal->paths.front().excess_delay() !=
           contracts::SimDuration::Zero()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Acoustic field multipath cell requires a "
                           "zero-excess-delay path"});
    }
  }

  return AcousticFieldAsset{format_version,
                            std::move(provenance),
                            coordinate_frame,
                            std::move(frequency_hz),
                            std::move(source_depth_m),
                            std::move(receiver_depth_m),
                            std::move(horizontal_range_m),
                            std::move(cells)};
}

}  // namespace ns3_factory::environment::internal
