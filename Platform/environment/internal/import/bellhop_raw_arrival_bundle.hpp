#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "bellhop_raw_arrival_dataset.hpp"

namespace ns3_factory::environment::internal::import {

// A canonical multi-frequency collection of raw datasets. It performs no
// resampling, interpolation, coherent sum, aggregate calculation, or channel
// normalization.
class BellhopRawArrivalBundle final {
 public:
  [[nodiscard]] static auto Create(
      std::vector<BellhopRawArrivalDataset> datasets)
      -> contracts::Result<BellhopRawArrivalBundle> {
    if(datasets.empty()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Bellhop raw arrival bundle must not be empty"});
    }
    const auto& reference = datasets.front();
    std::vector<double> frequencies;
    frequencies.reserve(datasets.size());
    for(std::size_t index = 0U; index < datasets.size(); ++index) {
      const auto& dataset = datasets[index];
      if(index != 0U &&
         datasets[index - 1U].frequency_hz() >= dataset.frequency_hz()) {
        return std::unexpected(
            contracts::Error{contracts::ErrorCode::kInvalidArgument,
                             "Bellhop raw bundle frequencies must be "
                             "strictly increasing"});
      }
      if(!std::ranges::equal(reference.source_depths_m(),
                            dataset.source_depths_m()) ||
         !std::ranges::equal(reference.receiver_depths_m(),
                            dataset.receiver_depths_m()) ||
         !std::ranges::equal(reference.receiver_ranges_m(),
                            dataset.receiver_ranges_m())) {
        return std::unexpected(
            contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                             "Bellhop raw bundle datasets must have exactly "
                             "matching spatial axes"});
      }
      frequencies.push_back(dataset.frequency_hz());
    }
    return BellhopRawArrivalBundle{
        std::move(frequencies), std::move(datasets)};
  }

  [[nodiscard]] auto frequencies_hz() const noexcept
      -> std::span<const double> {
    return frequencies_hz_;
  }

  [[nodiscard]] auto datasets() const noexcept
      -> std::span<const BellhopRawArrivalDataset> {
    return datasets_;
  }

  auto operator==(const BellhopRawArrivalBundle&) const -> bool = default;

 private:
  BellhopRawArrivalBundle(
      std::vector<double> frequencies_hz,
      std::vector<BellhopRawArrivalDataset> datasets)
      : frequencies_hz_(std::move(frequencies_hz)),
        datasets_(std::move(datasets)) {}

  std::vector<double> frequencies_hz_;
  std::vector<BellhopRawArrivalDataset> datasets_;
};

}  // namespace ns3_factory::environment::internal::import
