#include <cstdlib>
#include <utility>
#include <vector>

#include "internal/import/bellhop_raw_arrival_bundle.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::environment::internal::import;

namespace {

auto Dataset(double frequency_hz,
             std::vector<double> source_depths = {10.0},
             std::vector<double> receiver_depths = {20.0},
             std::vector<double> ranges = {100.0})
    -> Result<BellhopRawArrivalDataset> {
  auto arrival = BellhopRawArrival::Create(
      1.0, -180.0, 0.1, -0.01, 5.0, -5.0, 0U, 1U);
  if(!arrival) return std::unexpected(arrival.error());
  return BellhopRawArrivalDataset::Create(
      frequency_hz,
      std::move(source_depths),
      std::move(receiver_depths),
      std::move(ranges),
      {BellhopRawArrivalCell{{*arrival}}});
}

auto TestMatchingAxesAndIncreasingFrequencies() -> bool {
  auto low = Dataset(20'000.0);
  auto high = Dataset(30'000.0);
  if(!low || !high) return false;
  auto bundle = BellhopRawArrivalBundle::Create(
      {std::move(*low), std::move(*high)});
  return bundle && bundle->datasets().size() == 2U &&
         bundle->frequencies_hz()[0] == 20'000.0 &&
         bundle->frequencies_hz()[1] == 30'000.0 &&
         bundle->datasets()[0].cell(0U, 0U, 0U).arrivals[0]
                 .raw_phase_degrees() == -180.0;
}

auto BundleFrom(Result<BellhopRawArrivalDataset> first,
                Result<BellhopRawArrivalDataset> second)
    -> Result<BellhopRawArrivalBundle> {
  if(!first) return std::unexpected(first.error());
  if(!second) return std::unexpected(second.error());
  return BellhopRawArrivalBundle::Create(
      {std::move(*first), std::move(*second)});
}

auto TestFrequencyOrdering() -> bool {
  const auto duplicate = BundleFrom(Dataset(20'000.0), Dataset(20'000.0));
  const auto decreasing = BundleFrom(Dataset(30'000.0), Dataset(20'000.0));
  const auto empty = BellhopRawArrivalBundle::Create({});
  return !duplicate && !decreasing && !empty;
}

auto TestSpatialAxisMismatch() -> bool {
  const auto source_mismatch = BundleFrom(
      Dataset(20'000.0), Dataset(30'000.0, {11.0}));
  const auto receiver_mismatch = BundleFrom(
      Dataset(20'000.0), Dataset(30'000.0, {10.0}, {21.0}));
  const auto range_mismatch = BundleFrom(
      Dataset(20'000.0),
      Dataset(30'000.0, {10.0}, {20.0}, {101.0}));
  return !source_mismatch && !receiver_mismatch && !range_mismatch;
}

}  // namespace

auto main() -> int {
  return TestMatchingAxesAndIncreasingFrequencies() &&
                 TestFrequencyOrdering() && TestSpatialAxisMismatch()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
