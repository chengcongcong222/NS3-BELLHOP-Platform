#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <locale>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "bellhop_arrival_import_options.hpp"
#include "bellhop_raw_arrival_dataset.hpp"

namespace ns3_factory::environment::internal::import {

class BellhopAsciiArrivalParser final {
 public:
  [[nodiscard]] static auto Parse(
      std::istream& input,
      const BellhopArrivalImportOptions& options)
      -> contracts::Result<BellhopRawArrivalDataset>;

  [[nodiscard]] static auto ParseFile(
      const std::filesystem::path& path,
      const BellhopArrivalImportOptions& options)
      -> contracts::Result<BellhopRawArrivalDataset> {
    std::ifstream input{path};
    if(!input.is_open()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kNotFound,
                           "Bellhop ASCII arrival file could not be opened"});
    }
    return Parse(input, options);
  }
};

namespace detail {

class ClassicLocaleGuard final {
 public:
  explicit ClassicLocaleGuard(std::istream& input)
      : input_(input), previous_locale_(input.getloc()) {
    input_.imbue(std::locale::classic());
  }

  ~ClassicLocaleGuard() noexcept {
    try {
      input_.imbue(previous_locale_);
    } catch(...) {
    }
  }

 private:
  std::istream& input_;
  std::locale previous_locale_;
};

class BellhopTokenReader final {
 public:
  explicit BellhopTokenReader(std::istream& input) noexcept : input_(input) {}

  [[nodiscard]] auto Next(std::string_view label)
      -> contracts::Result<std::string> {
    std::string token;
    if(input_ >> token) return token;
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Bellhop ASCII arrival file ended before " +
                             std::string{label}});
  }

  [[nodiscard]] auto NextDouble(std::string_view label)
      -> contracts::Result<double> {
    auto token = Next(label);
    if(!token) return std::unexpected(token.error());
    double value = 0.0;
    const auto parsed = std::from_chars(token->data(),
                                        token->data() + token->size(),
                                        value,
                                        std::chars_format::general);
    if(parsed.ec == std::errc::result_out_of_range) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Bellhop numeric token is out of range for " +
                               std::string{label}});
    }
    if(parsed.ec != std::errc{} ||
       parsed.ptr != token->data() + token->size()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Bellhop numeric token is invalid for " +
                               std::string{label}});
    }
    return value;
  }

  [[nodiscard]] auto NextCount(std::string_view label,
                               std::size_t upper_bound)
      -> contracts::Result<std::size_t> {
    auto token = Next(label);
    if(!token) return std::unexpected(token.error());
    std::uint64_t value = 0U;
    const auto parsed = std::from_chars(
        token->data(), token->data() + token->size(), value);
    if(parsed.ec == std::errc::result_out_of_range ||
       value > std::numeric_limits<std::size_t>::max()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Bellhop count is out of range for " +
                               std::string{label}});
    }
    if(parsed.ec != std::errc{} ||
       parsed.ptr != token->data() + token->size()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Bellhop count token is invalid for " +
                               std::string{label}});
    }
    const auto count = static_cast<std::size_t>(value);
    if(count > upper_bound) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           "Bellhop count exceeds the configured limit for " +
                               std::string{label}});
    }
    return count;
  }

  [[nodiscard]] auto NextBounceCount(std::string_view label)
      -> contracts::Result<std::uint32_t> {
    auto token = Next(label);
    if(!token) return std::unexpected(token.error());
    std::int64_t value = 0;
    const auto parsed = std::from_chars(
        token->data(), token->data() + token->size(), value);
    if(parsed.ec == std::errc::result_out_of_range ||
       value > std::numeric_limits<std::uint32_t>::max()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Bellhop bounce count is out of range for " +
                               std::string{label}});
    }
    if(parsed.ec != std::errc{} ||
       parsed.ptr != token->data() + token->size() || value < 0) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Bellhop bounce count must be a non-negative "
                           "integer for " +
                               std::string{label}});
    }
    return static_cast<std::uint32_t>(value);
  }

  [[nodiscard]] auto RequireOnlyTrailingWhitespace() -> contracts::Status {
    std::string unexpected_token;
    if(input_ >> unexpected_token) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Bellhop ASCII arrival file contains an "
                           "unexpected trailing token"});
    }
    if(input_.bad()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kUnavailable,
                           "Bellhop ASCII arrival stream failed"});
    }
    return {};
  }

 private:
  std::istream& input_;
};

[[nodiscard]] inline auto ValidateParserOptions(
    const BellhopArrivalImportOptions& options) -> contracts::Status {
  if(options.receiver_range_unit != BellhopReceiverRangeUnit::kMeters &&
     options.receiver_range_unit !=
         BellhopReceiverRangeUnit::kKilometers) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Bellhop receiver range unit is invalid"});
  }
  if(options.dialect != BellhopAsciiDialect::kAutoDetect &&
     options.dialect != BellhopAsciiDialect::kTwoDimensional) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Bellhop ASCII dialect option is invalid"});
  }
  if(options.limits.max_axis_values == 0U ||
     options.limits.max_spatial_cells == 0U ||
     options.limits.max_arrivals_per_cell == 0U ||
     options.limits.max_total_arrivals == 0U) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Bellhop parser safety limits must be positive"});
  }
  return {};
}

[[nodiscard]] inline auto ReadAxis(BellhopTokenReader& reader,
                                   std::string_view label,
                                   std::size_t maximum_count)
    -> contracts::Result<std::vector<double>> {
  const auto count = reader.NextCount(label, maximum_count);
  if(!count) return std::unexpected(count.error());
  if(*count == 0U) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Bellhop axis count must be positive for " +
                             std::string{label}});
  }
  std::vector<double> axis;
  axis.reserve(*count);
  for(std::size_t index = 0U; index < *count; ++index) {
    auto value = reader.NextDouble(label);
    if(!value) return std::unexpected(value.error());
    axis.push_back(*value);
  }
  return axis;
}

[[nodiscard]] inline auto ReadRawArrival(BellhopTokenReader& reader)
    -> contracts::Result<BellhopRawArrival> {
  const auto magnitude = reader.NextDouble("raw magnitude");
  if(!magnitude) return std::unexpected(magnitude.error());
  const auto phase = reader.NextDouble("raw phase degrees");
  if(!phase) return std::unexpected(phase.error());
  const auto delay_real = reader.NextDouble("raw real delay");
  if(!delay_real) return std::unexpected(delay_real.error());
  const auto delay_imag = reader.NextDouble("raw imaginary delay");
  if(!delay_imag) return std::unexpected(delay_imag.error());
  const auto source_angle = reader.NextDouble("raw source angle");
  if(!source_angle) return std::unexpected(source_angle.error());
  const auto receiver_angle = reader.NextDouble("raw receiver angle");
  if(!receiver_angle) return std::unexpected(receiver_angle.error());
  const auto top_bounces = reader.NextBounceCount("top bounce count");
  if(!top_bounces) return std::unexpected(top_bounces.error());
  const auto bottom_bounces = reader.NextBounceCount("bottom bounce count");
  if(!bottom_bounces) return std::unexpected(bottom_bounces.error());
  return BellhopRawArrival::Create(*magnitude,
                                   *phase,
                                   *delay_real,
                                   *delay_imag,
                                   *source_angle,
                                   *receiver_angle,
                                   *top_bounces,
                                   *bottom_bounces);
}

}  // namespace detail

inline auto BellhopAsciiArrivalParser::Parse(
    std::istream& input,
    const BellhopArrivalImportOptions& options)
    -> contracts::Result<BellhopRawArrivalDataset> {
  const auto options_status = detail::ValidateParserOptions(options);
  if(!options_status) return std::unexpected(options_status.error());
  detail::ClassicLocaleGuard locale_guard{input};
  detail::BellhopTokenReader reader{input};

  const auto header = reader.Next("dialect header");
  if(!header) return std::unexpected(header.error());
  if(*header != "'2D'") {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kUnsupported,
                         "Only the confirmed quoted Bellhop '2D' ASCII "
                         "arrival dialect is supported"});
  }

  const auto frequency_hz = reader.NextDouble("frequency in Hz");
  if(!frequency_hz) return std::unexpected(frequency_hz.error());
  auto source_depths_m = detail::ReadAxis(
      reader, "source-depth axis", options.limits.max_axis_values);
  if(!source_depths_m) return std::unexpected(source_depths_m.error());
  auto receiver_depths_m = detail::ReadAxis(
      reader, "receiver-depth axis", options.limits.max_axis_values);
  if(!receiver_depths_m) {
    return std::unexpected(receiver_depths_m.error());
  }
  auto receiver_ranges_m = detail::ReadAxis(
      reader, "receiver-range axis", options.limits.max_axis_values);
  if(!receiver_ranges_m) {
    return std::unexpected(receiver_ranges_m.error());
  }
  for(auto& range : *receiver_ranges_m) {
    if(!std::isfinite(range)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Bellhop receiver range must be finite"});
    }
    if(options.receiver_range_unit ==
       BellhopReceiverRangeUnit::kKilometers) {
      constexpr auto kMetersPerKilometer = 1'000.0;
      if(std::abs(range) >
         std::numeric_limits<double>::max() / kMetersPerKilometer) {
        return std::unexpected(
            contracts::Error{contracts::ErrorCode::kOverflow,
                             "Bellhop receiver range conversion "
                             "overflows"});
      }
      range *= kMetersPerKilometer;
    }
  }

  const auto spatial_cell_count = CheckedRawArrivalCellCount(
      source_depths_m->size(),
      receiver_depths_m->size(),
      receiver_ranges_m->size());
  if(!spatial_cell_count) {
    return std::unexpected(spatial_cell_count.error());
  }
  if(*spatial_cell_count > options.limits.max_spatial_cells) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOutOfRange,
                         "Bellhop spatial cell count exceeds the configured "
                         "limit"});
  }

  std::vector<BellhopRawArrivalCell> cells;
  cells.reserve(*spatial_cell_count);
  std::size_t total_arrivals = 0U;
  for(std::size_t source = 0U; source < source_depths_m->size(); ++source) {
    const auto maximum_arrivals = reader.NextCount(
        "Narrmx", options.limits.max_arrivals_per_cell);
    if(!maximum_arrivals) {
      return std::unexpected(maximum_arrivals.error());
    }
    for(std::size_t receiver = 0U;
        receiver < receiver_depths_m->size();
        ++receiver) {
      for(std::size_t range = 0U; range < receiver_ranges_m->size();
          ++range) {
        const auto arrival_count = reader.NextCount(
            "per-cell Narr", options.limits.max_arrivals_per_cell);
        if(!arrival_count) return std::unexpected(arrival_count.error());
        if(*arrival_count > *maximum_arrivals) {
          return std::unexpected(
              contracts::Error{
                  contracts::ErrorCode::kFailedPrecondition,
                  "Bellhop per-cell Narr exceeds source Narrmx"});
        }
        if(*arrival_count >
           options.limits.max_total_arrivals - total_arrivals) {
          return std::unexpected(
              contracts::Error{contracts::ErrorCode::kOutOfRange,
                               "Bellhop total arrivals exceed the "
                               "configured limit"});
        }
        BellhopRawArrivalCell cell;
        cell.arrivals.reserve(*arrival_count);
        for(std::size_t arrival_index = 0U;
            arrival_index < *arrival_count;
            ++arrival_index) {
          auto arrival = detail::ReadRawArrival(reader);
          if(!arrival) return std::unexpected(arrival.error());
          cell.arrivals.push_back(std::move(*arrival));
        }
        total_arrivals += *arrival_count;
        cells.push_back(std::move(cell));
      }
    }
  }

  const auto complete = reader.RequireOnlyTrailingWhitespace();
  if(!complete) return std::unexpected(complete.error());
  return BellhopRawArrivalDataset::Create(
      *frequency_hz,
      std::move(*source_depths_m),
      std::move(*receiver_depths_m),
      std::move(*receiver_ranges_m),
      std::move(cells));
}

}  // namespace ns3_factory::environment::internal::import
