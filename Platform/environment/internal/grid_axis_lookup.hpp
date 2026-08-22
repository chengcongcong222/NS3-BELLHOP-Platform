#pragma once

#include <algorithm>
#include <cstddef>
#include <span>

#include <ns3_factory/contracts/errors.hpp>

namespace ns3_factory::environment::internal {

struct GridAxisLookup final {
  std::size_t lower_index;
  std::size_t upper_index;
  std::size_t nearest_index;
  long double upper_weight;
};

// The axis is validated by AcousticFieldAsset. A singleton axis describes
// exactly one coordinate, not a constant field over an unbounded dimension.
[[nodiscard]] inline auto ResolveGridAxis(std::span<const double> axis,
                                          double coordinate)
    -> contracts::Result<GridAxisLookup> {
  if(coordinate < axis.front() || coordinate > axis.back()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOutOfRange,
                         "Acoustic field query is outside an axis domain"});
  }
  if(axis.size() == 1U) {
    if(coordinate != axis.front()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           "Acoustic field singleton axis requires an exact "
                           "coordinate"});
    }
    return GridAxisLookup{0U, 0U, 0U, 0.0L};
  }

  const auto upper = std::lower_bound(axis.begin(), axis.end(), coordinate);
  if(upper != axis.end() && *upper == coordinate) {
    const auto index =
        static_cast<std::size_t>(std::distance(axis.begin(), upper));
    return GridAxisLookup{index, index, index, 0.0L};
  }

  const auto upper_index =
      static_cast<std::size_t>(std::distance(axis.begin(), upper));
  const auto lower_index = upper_index - 1U;
  const auto lower_distance = coordinate - axis[lower_index];
  const auto upper_distance = axis[upper_index] - coordinate;
  const auto nearest_index =
      lower_distance <= upper_distance ? lower_index : upper_index;
  const auto upper_weight =
      static_cast<long double>(lower_distance) /
      static_cast<long double>(axis[upper_index] - axis[lower_index]);
  return GridAxisLookup{lower_index,
                        upper_index,
                        nearest_index,
                        upper_weight};
}

}  // namespace ns3_factory::environment::internal
