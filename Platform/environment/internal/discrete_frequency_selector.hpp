#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

#include <ns3_factory/contracts/errors.hpp>

namespace ns3_factory::environment::internal {

// Selects one discrete field profile. Equal-distance ties select the lower
// frequency; no continuous frequency interpolation is performed.
class DiscreteFrequencySelectionPolicy final {
 public:
  [[nodiscard]] static auto Create(double max_frequency_offset_hz)
      -> contracts::Result<DiscreteFrequencySelectionPolicy> {
    if(!std::isfinite(max_frequency_offset_hz)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Maximum frequency offset must be finite"});
    }
    if(max_frequency_offset_hz < 0.0) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           "Maximum frequency offset must be non-negative"});
    }
    return DiscreteFrequencySelectionPolicy{max_frequency_offset_hz};
  }

  [[nodiscard]] auto Select(std::span<const double> frequency_axis_hz,
                            double query_frequency_hz) const
      -> contracts::Result<std::size_t> {
    if(!std::isfinite(query_frequency_hz) || query_frequency_hz <= 0.0) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Query frequency must be finite and positive"});
    }

    const auto upper = std::lower_bound(frequency_axis_hz.begin(),
                                        frequency_axis_hz.end(),
                                        query_frequency_hz);
    std::size_t selected = 0U;
    if(upper == frequency_axis_hz.begin()) {
      selected = 0U;
    } else if(upper == frequency_axis_hz.end()) {
      selected = frequency_axis_hz.size() - 1U;
    } else {
      const auto upper_index = static_cast<std::size_t>(
          std::distance(frequency_axis_hz.begin(), upper));
      const auto lower_index = upper_index - 1U;
      const auto lower_offset =
          query_frequency_hz - frequency_axis_hz[lower_index];
      const auto upper_offset =
          frequency_axis_hz[upper_index] - query_frequency_hz;
      selected = lower_offset <= upper_offset ? lower_index : upper_index;
    }

    const auto offset =
        std::abs(frequency_axis_hz[selected] - query_frequency_hz);
    if(offset > max_frequency_offset_hz_) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           "No acoustic frequency profile lies within the "
                           "configured maximum offset"});
    }
    return selected;
  }

  [[nodiscard]] constexpr auto max_frequency_offset_hz() const noexcept
      -> double {
    return max_frequency_offset_hz_;
  }

 private:
  explicit constexpr DiscreteFrequencySelectionPolicy(
      double max_frequency_offset_hz) noexcept
      : max_frequency_offset_hz_(max_frequency_offset_hz) {}

  double max_frequency_offset_hz_;
};

}  // namespace ns3_factory::environment::internal
