#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>

#include "internal/acoustic_field_asset.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::environment::internal;

namespace {

auto Cell(double loss_db = 70.0) -> AcousticFieldCell {
  return AcousticFieldCell{loss_db,
                           SimDuration::FromNanoseconds(1'000),
                           {}};
}

auto Build(std::vector<double> frequencies,
           std::vector<double> source_depths,
           std::vector<double> receiver_depths,
           std::vector<double> ranges,
           std::vector<AcousticFieldCell> cells)
    -> Result<AcousticFieldAsset> {
  const auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!frame) return std::unexpected(frame.error());
  return AcousticFieldAsset::Create(1U,
                                    "deterministic fixture",
                                    *frame,
                                    std::move(frequencies),
                                    std::move(source_depths),
                                    std::move(receiver_depths),
                                    std::move(ranges),
                                    std::move(cells));
}

auto TestValidAssetAndLayout() -> bool {
  std::vector<AcousticFieldCell> cells;
  for(auto frequency = 0U; frequency < 2U; ++frequency) {
    for(auto source = 0U; source < 2U; ++source) {
      for(auto receiver = 0U; receiver < 2U; ++receiver) {
        for(auto range = 0U; range < 2U; ++range) {
          cells.push_back(Cell(static_cast<double>(
              1000U * frequency + 100U * source + 10U * receiver +
              range)));
        }
      }
    }
  }
  auto asset = Build({20'000.0, 30'000.0},
                     {10.0, 20.0},
                     {30.0, 40.0},
                     {0.0, 100.0},
                     std::move(cells));
  return asset && asset->format_version() == 1U &&
         asset->provenance() == "deterministic fixture" &&
         asset->coordinate_frame().surface_z_meters() == 0.0 &&
         asset->cells().size() == 16U &&
         asset->cell(1U, 0U, 1U, 0U)
                 .aggregate_transmission_loss_db == 1010.0;
}

auto TestAxisValidation() -> bool {
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto empty = Build({}, {10.0}, {10.0}, {0.0}, {Cell()});
  const auto nonfinite = Build({20'000.0}, {10.0, nan}, {10.0},
                               {0.0}, {Cell(), Cell()});
  const auto duplicate = Build({20'000.0}, {10.0, 10.0}, {10.0},
                               {0.0}, {Cell(), Cell()});
  const auto decreasing = Build({30'000.0, 20'000.0}, {10.0}, {10.0},
                                {0.0}, {Cell(), Cell()});
  const auto nonpositive_frequency =
      Build({0.0}, {10.0}, {10.0}, {0.0}, {Cell()});
  const auto negative_source =
      Build({20'000.0}, {-1.0}, {10.0}, {0.0}, {Cell()});
  const auto negative_receiver =
      Build({20'000.0}, {10.0}, {-1.0}, {0.0}, {Cell()});
  const auto negative_range =
      Build({20'000.0}, {10.0}, {10.0}, {-1.0}, {Cell()});
  return !empty && !nonfinite && !duplicate && !decreasing &&
         !nonpositive_frequency && !negative_source &&
         !negative_receiver && !negative_range;
}

auto TestCellValidation() -> bool {
  const auto bad_count =
      Build({20'000.0}, {10.0}, {10.0}, {0.0, 1.0}, {Cell()});
  const auto bad_loss = Build(
      {20'000.0},
      {10.0},
      {10.0},
      {0.0},
      {Cell(std::numeric_limits<double>::infinity())});
  const auto bad_delay = Build(
      {20'000.0},
      {10.0},
      {10.0},
      {0.0},
      {AcousticFieldCell{70.0,
                         SimDuration::FromNanoseconds(-1),
                         {}}});
  const auto delayed_path = PropagationPath::Create(
      SimDuration::FromNanoseconds(1), 0.5, 0.0);
  if(!delayed_path) return false;
  const auto no_zero_path = Build(
      {20'000.0},
      {10.0},
      {10.0},
      {0.0},
      {AcousticFieldCell{70.0,
                         SimDuration::FromNanoseconds(1),
                         {*delayed_path}}});
  return !bad_count && !bad_loss && !bad_delay && !no_zero_path;
}

auto TestDimensionProductOverflow() -> bool {
  const auto overflow = CheckedGridCellCount(
      std::numeric_limits<std::size_t>::max(), 2U, 1U, 1U);
  const auto valid = CheckedGridCellCount(2U, 3U, 5U, 7U);
  return !overflow &&
         overflow.error().code == ErrorCode::kOverflow && valid &&
         *valid == 210U;
}

}  // namespace

auto main() -> int {
  return TestValidAssetAndLayout() && TestAxisValidation() &&
                 TestCellValidation() && TestDimensionProductOverflow()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
