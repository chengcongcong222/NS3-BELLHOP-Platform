#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "bellhop_raw_arrival.hpp"

namespace ns3_factory::environment::internal::import {

struct BellhopRawArrivalCell final {
  std::vector<BellhopRawArrival> arrivals;

  auto operator==(const BellhopRawArrivalCell&) const -> bool = default;
};

[[nodiscard]] inline auto CheckedRawArrivalCellCount(
    std::size_t source_depth_count,
    std::size_t receiver_depth_count,
    std::size_t receiver_range_count)
    -> contracts::Result<std::size_t> {
  std::size_t result = 1U;
  for(const auto dimension : {source_depth_count,
                              receiver_depth_count,
                              receiver_range_count}) {
    if(dimension != 0U &&
       result > std::numeric_limits<std::size_t>::max() / dimension) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Bellhop raw arrival grid dimension product "
                           "overflows"});
    }
    result *= dimension;
  }
  return result;
}

// Canonical cell order is source-depth -> receiver-depth -> receiver-range,
// independent of how a future parser dialect may present its loops.
class BellhopRawArrivalDataset final {
 public:
  [[nodiscard]] static auto Create(
      double frequency_hz,
      std::vector<double> source_depths_m,
      std::vector<double> receiver_depths_m,
      std::vector<double> receiver_ranges_m,
      std::vector<BellhopRawArrivalCell> cells)
      -> contracts::Result<BellhopRawArrivalDataset>;

  [[nodiscard]] constexpr auto frequency_hz() const noexcept -> double {
    return frequency_hz_;
  }

  [[nodiscard]] auto source_depths_m() const noexcept
      -> std::span<const double> {
    return source_depths_m_;
  }

  [[nodiscard]] auto receiver_depths_m() const noexcept
      -> std::span<const double> {
    return receiver_depths_m_;
  }

  [[nodiscard]] auto receiver_ranges_m() const noexcept
      -> std::span<const double> {
    return receiver_ranges_m_;
  }

  [[nodiscard]] auto cells() const noexcept
      -> std::span<const BellhopRawArrivalCell> {
    return cells_;
  }

  [[nodiscard]] auto cell(std::size_t source_depth_index,
                          std::size_t receiver_depth_index,
                          std::size_t receiver_range_index) const noexcept
      -> const BellhopRawArrivalCell& {
    return cells_[FlattenIndex(source_depth_index,
                               receiver_depth_index,
                               receiver_range_index)];
  }

  [[nodiscard]] constexpr auto total_arrival_count() const noexcept
      -> std::size_t {
    return total_arrival_count_;
  }

  auto operator==(const BellhopRawArrivalDataset&) const -> bool = default;

 private:
  BellhopRawArrivalDataset(double frequency_hz,
                           std::vector<double> source_depths_m,
                           std::vector<double> receiver_depths_m,
                           std::vector<double> receiver_ranges_m,
                           std::vector<BellhopRawArrivalCell> cells,
                           std::size_t total_arrival_count)
      : frequency_hz_(frequency_hz),
        source_depths_m_(std::move(source_depths_m)),
        receiver_depths_m_(std::move(receiver_depths_m)),
        receiver_ranges_m_(std::move(receiver_ranges_m)),
        cells_(std::move(cells)),
        total_arrival_count_(total_arrival_count) {}

  [[nodiscard]] auto FlattenIndex(
      std::size_t source_depth_index,
      std::size_t receiver_depth_index,
      std::size_t receiver_range_index) const noexcept -> std::size_t {
    return (source_depth_index * receiver_depths_m_.size() +
            receiver_depth_index) *
               receiver_ranges_m_.size() +
           receiver_range_index;
  }

  double frequency_hz_;
  std::vector<double> source_depths_m_;
  std::vector<double> receiver_depths_m_;
  std::vector<double> receiver_ranges_m_;
  std::vector<BellhopRawArrivalCell> cells_;
  std::size_t total_arrival_count_;
};

namespace detail {

[[nodiscard]] inline auto ValidateRawAxis(std::span<const double> axis,
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
    if(axis[index] < 0.0) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           std::string{label} +
                               " axis values must be non-negative"});
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

}  // namespace detail

inline auto BellhopRawArrivalDataset::Create(
    double frequency_hz,
    std::vector<double> source_depths_m,
    std::vector<double> receiver_depths_m,
    std::vector<double> receiver_ranges_m,
    std::vector<BellhopRawArrivalCell> cells)
    -> contracts::Result<BellhopRawArrivalDataset> {
  if(!std::isfinite(frequency_hz)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Bellhop raw dataset frequency must be finite"});
  }
  if(frequency_hz <= 0.0) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOutOfRange,
                         "Bellhop raw dataset frequency must be positive"});
  }
  for(const auto& validation :
      {detail::ValidateRawAxis(source_depths_m, "Source depth"),
       detail::ValidateRawAxis(receiver_depths_m, "Receiver depth"),
       detail::ValidateRawAxis(receiver_ranges_m, "Receiver range")}) {
    if(!validation) return std::unexpected(validation.error());
  }

  const auto expected_cells = CheckedRawArrivalCellCount(
      source_depths_m.size(),
      receiver_depths_m.size(),
      receiver_ranges_m.size());
  if(!expected_cells) return std::unexpected(expected_cells.error());
  if(cells.size() != *expected_cells) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Bellhop raw arrival cell count does not match "
                         "axes"});
  }

  std::size_t total_arrivals = 0U;
  for(const auto& cell : cells) {
    if(cell.arrivals.size() >
       std::numeric_limits<std::size_t>::max() - total_arrivals) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Bellhop raw arrival total count overflows"});
    }
    total_arrivals += cell.arrivals.size();
  }
  return BellhopRawArrivalDataset{frequency_hz,
                                  std::move(source_depths_m),
                                  std::move(receiver_depths_m),
                                  std::move(receiver_ranges_m),
                                  std::move(cells),
                                  total_arrivals};
}

}  // namespace ns3_factory::environment::internal::import
