#pragma once

#include <cstddef>
#include <cstdint>

namespace ns3_factory::environment::internal::import {

enum class BellhopReceiverRangeUnit : std::uint8_t {
  kMeters = 1,
  kKilometers = 2,
};

enum class BellhopAsciiDialect : std::uint8_t {
  kAutoDetect = 1,
  kTwoDimensional = 2,
};

struct BellhopArrivalParserLimits final {
  std::size_t max_axis_values{1'000'000U};
  std::size_t max_spatial_cells{10'000'000U};
  std::size_t max_arrivals_per_cell{1'000'000U};
  std::size_t max_total_arrivals{20'000'000U};

  auto operator==(const BellhopArrivalParserLimits&) const -> bool = default;
};

struct BellhopArrivalImportOptions final {
  explicit constexpr BellhopArrivalImportOptions(
      BellhopReceiverRangeUnit range_unit) noexcept
      : receiver_range_unit(range_unit) {}

  BellhopReceiverRangeUnit receiver_range_unit;
  BellhopAsciiDialect dialect{BellhopAsciiDialect::kAutoDetect};
  BellhopArrivalParserLimits limits;

  auto operator==(const BellhopArrivalImportOptions&) const -> bool = default;
};

}  // namespace ns3_factory::environment::internal::import
