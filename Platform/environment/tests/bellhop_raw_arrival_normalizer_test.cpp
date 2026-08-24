#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "internal/import/bellhop_arrival_import_options.hpp"
#include "internal/import/bellhop_ascii_arrival_parser.hpp"
#include "internal/import/bellhop_raw_arrival_normalizer.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::environment::internal;
using namespace ns3_factory::environment::internal::import;

namespace {

static_assert(std::is_empty_v<AcousticFieldNoArrivalCell>);

auto Raw(double magnitude,
         double phase_degrees,
         double delay_real_seconds,
         double delay_imag_seconds = 0.0) -> BellhopRawArrival {
  return *BellhopRawArrival::Create(magnitude,
                                    phase_degrees,
                                    delay_real_seconds,
                                    delay_imag_seconds,
                                    -10.0,
                                    20.0,
                                    0U,
                                    0U);
}

auto Bundle(double frequency_hz,
            std::vector<double> ranges,
            std::vector<BellhopRawArrivalCell> cells)
    -> Result<BellhopRawArrivalBundle> {
  auto dataset = BellhopRawArrivalDataset::Create(
      frequency_hz,
      {10.0},
      {20.0},
      std::move(ranges),
      std::move(cells));
  if(!dataset) return std::unexpected(dataset.error());
  std::vector<BellhopRawArrivalDataset> datasets;
  datasets.push_back(std::move(*dataset));
  return BellhopRawArrivalBundle::Create(std::move(datasets));
}

auto Normalize(const BellhopRawArrivalBundle& bundle)
    -> Result<AcousticFieldAsset> {
  auto frame = EnvironmentCoordinateFrame::Create(
      3.0, VerticalAxisDirection::kPositiveDown);
  if(!frame) return std::unexpected(frame.error());
  return BellhopRawArrivalNormalizer::Normalize(
      bundle, 2U, "synthetic Bellhop normalization fixture", *frame);
}

auto NearlyEqual(long double lhs,
                 long double rhs,
                 long double tolerance = 1.0e-12L) -> bool {
  const auto scale = std::max({1.0L, std::abs(lhs), std::abs(rhs)});
  return std::abs(lhs - rhs) <= tolerance * scale;
}

auto NearlyEqual(std::complex<long double> lhs,
                 std::complex<long double> rhs,
                 long double tolerance = 1.0e-11L) -> bool {
  const auto scale = std::max({1.0L, std::abs(lhs), std::abs(rhs)});
  return std::abs(lhs - rhs) <= tolerance * scale;
}

auto RawContribution(const BellhopRawArrival& raw,
                     long double omega)
    -> std::complex<long double> {
  const std::complex<long double> imaginary_unit{0.0L, 1.0L};
  const auto phase =
      static_cast<long double>(raw.raw_phase_degrees()) *
      std::numbers::pi_v<long double> / 180.0L;
  const std::complex<long double> delay{
      static_cast<long double>(raw.raw_delay_real_seconds()),
      static_cast<long double>(raw.raw_delay_imag_seconds())};
  return static_cast<long double>(raw.raw_magnitude()) *
         std::exp(imaginary_unit * phase) *
         std::exp(-imaginary_unit * omega * delay);
}

auto NormalizedContribution(const AcousticFieldSignalCell& cell,
                            long double omega)
    -> std::complex<long double> {
  const std::complex<long double> imaginary_unit{0.0L, 1.0L};
  const auto first_seconds =
      static_cast<long double>(cell.first_arrival_delay.nanoseconds()) /
      1'000'000'000.0L;
  std::complex<long double> relative_sum{};
  for(const auto& path : cell.paths) {
    const auto excess_seconds =
        static_cast<long double>(path.excess_delay().nanoseconds()) /
        1'000'000'000.0L;
    relative_sum +=
        static_cast<long double>(path.pressure_gain_linear()) *
        std::exp(imaginary_unit *
                 static_cast<long double>(path.phase_radians())) *
        std::exp(-imaginary_unit * omega * excess_seconds);
  }
  return std::exp(-imaginary_unit * omega * first_seconds) *
         relative_sum;
}

auto TestGoldenComplexSemanticsAndAggregateLoss() -> bool {
  constexpr auto frequency_hz = 1'000.0;
  const auto omega = 2.0L * std::numbers::pi_v<long double> *
                     static_cast<long double>(frequency_hz);
  const auto logarithmic_delay =
      static_cast<double>(std::log(2.0L) / omega);
  std::vector<BellhopRawArrival> arrivals{
      Raw(0.125, 0.0, 1.003e-6),
      Raw(0.5, -180.0, 1.000e-6, logarithmic_delay),
      Raw(0.25, 270.0, 1.002e-6, -logarithmic_delay)};
  auto bundle = Bundle(frequency_hz, {100.0}, {{arrivals}});
  if(!bundle) return false;
  auto asset = Normalize(*bundle);
  if(!asset ||
     asset->coordinate_frame().surface_z_meters() != 3.0 ||
     asset->coordinate_frame().vertical_direction() !=
         VerticalAxisDirection::kPositiveDown ||
     asset->format_version() != 2U ||
     asset->frequency_hz().size() != 1U ||
     asset->frequency_hz().front() != frequency_hz ||
     asset->horizontal_range_m().front() != 100.0) {
    return false;
  }
  const auto* signal = std::get_if<AcousticFieldSignalCell>(
      &asset->cell(0U, 0U, 0U, 0U));
  if(signal == nullptr || signal->paths.size() != 3U ||
     signal->first_arrival_delay !=
         SimDuration::FromNanoseconds(1'000)) {
    return false;
  }

  std::complex<long double> raw_sum{};
  for(const auto& raw : arrivals) raw_sum += RawContribution(raw, omega);
  const auto normalized_sum = NormalizedContribution(*signal, omega);
  const auto expected_aggregate =
      std::hypot(std::hypot(1.0L, 0.125L), 0.125L);
  const auto expected_loss = -20.0L * std::log10(expected_aggregate);
  return NearlyEqual(raw_sum, normalized_sum) &&
         NearlyEqual(signal->aggregate_transmission_loss_db,
                     expected_loss) &&
         signal->paths[0].excess_delay() == SimDuration::Zero() &&
         NearlyEqual(signal->paths[0].pressure_gain_linear(), 1.0L) &&
         NearlyEqual(signal->paths[0].phase_radians(),
                     -std::numbers::pi_v<long double>) &&
         signal->paths[1].excess_delay() ==
             SimDuration::FromNanoseconds(2) &&
         NearlyEqual(signal->paths[1].pressure_gain_linear(), 0.125L) &&
         NearlyEqual(signal->paths[1].phase_radians(),
                     1.5L * std::numbers::pi_v<long double>) &&
         signal->paths[2].excess_delay() ==
             SimDuration::FromNanoseconds(3) &&
         NearlyEqual(signal->paths[2].pressure_gain_linear(), 0.125L) &&
         NearlyEqual(signal->paths[2].phase_radians(), 0.0L);
}

auto TestAggregateLossIgnoresPhase() -> bool {
  auto first = Bundle(
      2'000.0,
      {10.0},
      {{{Raw(0.3, 0.0, 1.0e-6), Raw(0.4, 90.0, 2.0e-6)}}});
  auto second = Bundle(
      2'000.0,
      {10.0},
      {{{Raw(0.3, -720.0, 1.0e-6),
         Raw(0.4, 1'234.0, 2.0e-6)}}});
  if(!first || !second) return false;
  const auto first_asset = Normalize(*first);
  const auto second_asset = Normalize(*second);
  if(!first_asset || !second_asset) return false;
  const auto& first_signal = std::get<AcousticFieldSignalCell>(
      first_asset->cell(0U, 0U, 0U, 0U));
  const auto& second_signal = std::get<AcousticFieldSignalCell>(
      second_asset->cell(0U, 0U, 0U, 0U));
  return first_signal.aggregate_transmission_loss_db ==
         second_signal.aggregate_transmission_loss_db;
}

auto TestDelayRoundingAndNoArrivalClassification() -> bool {
  auto bundle = Bundle(
      1'000.0,
      {0.0, 1.0},
      {{}, {{Raw(1.0, 0.0, 0.5e-9)}}});
  if(!bundle) return false;
  auto asset = Normalize(*bundle);
  if(!asset) return false;
  const auto* signal = std::get_if<AcousticFieldSignalCell>(
      &asset->cell(0U, 0U, 0U, 1U));
  return std::holds_alternative<AcousticFieldNoArrivalCell>(
             asset->cell(0U, 0U, 0U, 0U)) &&
         signal != nullptr &&
         signal->first_arrival_delay ==
             SimDuration::FromNanoseconds(1) &&
         signal->paths.size() == 1U &&
         signal->paths.front().excess_delay() == SimDuration::Zero();
}

auto TestBundleAxesAndFrequencyMajorCellOrderArePreserved() -> bool {
  auto low = BellhopRawArrivalDataset::Create(
      1'000.0,
      {10.0},
      {20.0},
      {5.0},
      {{{Raw(0.25, 0.0, 1.0e-6)}}});
  auto high = BellhopRawArrivalDataset::Create(
      2'000.0,
      {10.0},
      {20.0},
      {5.0},
      {{{Raw(0.5, 0.0, 2.0e-6)}}});
  if(!low || !high) return false;
  std::vector<BellhopRawArrivalDataset> datasets;
  datasets.push_back(std::move(*low));
  datasets.push_back(std::move(*high));
  auto bundle = BellhopRawArrivalBundle::Create(std::move(datasets));
  if(!bundle) return false;
  auto asset = Normalize(*bundle);
  if(!asset ||
     !std::ranges::equal(asset->frequency_hz(),
                        std::vector<double>{1'000.0, 2'000.0}) ||
     !std::ranges::equal(asset->source_depth_m(),
                        std::vector<double>{10.0}) ||
     !std::ranges::equal(asset->receiver_depth_m(),
                        std::vector<double>{20.0}) ||
     !std::ranges::equal(asset->horizontal_range_m(),
                        std::vector<double>{5.0})) {
    return false;
  }
  const auto& low_signal = std::get<AcousticFieldSignalCell>(
      asset->cell(0U, 0U, 0U, 0U));
  const auto& high_signal = std::get<AcousticFieldSignalCell>(
      asset->cell(1U, 0U, 0U, 0U));
  return low_signal.paths.front().pressure_gain_linear() == 0.25 &&
         high_signal.paths.front().pressure_gain_linear() == 0.5;
}

auto TestExplicitNormalizationFailures() -> bool {
  const auto normalize_one = [](BellhopRawArrival arrival) {
    auto bundle = Bundle(20'000.0, {1.0}, {{{arrival}}});
    return bundle ? Normalize(*bundle)
                  : Result<AcousticFieldAsset>{
                        std::unexpected(bundle.error())};
  };
  const auto zero = normalize_one(Raw(0.0, 0.0, 1.0e-6));
  const auto gain_overflow =
      normalize_one(Raw(1.0, 0.0, 1.0e-6, 1.0e6));
  const auto gain_underflow =
      normalize_one(Raw(1.0, 0.0, 1.0e-6, -1.0e6));
  const auto delay_overflow =
      normalize_one(Raw(1.0, 0.0, 1.0e20));
  return !zero &&
         zero.error().code == ErrorCode::kFailedPrecondition &&
         !gain_overflow &&
         gain_overflow.error().code == ErrorCode::kOverflow &&
         !gain_underflow &&
         gain_underflow.error().code == ErrorCode::kOverflow &&
         !delay_overflow &&
         delay_overflow.error().code == ErrorCode::kOverflow;
}

auto CharacterizeLegacyFile(const std::filesystem::path& path) -> bool {
  BellhopArrivalImportOptions options{
      BellhopReceiverRangeUnit::kKilometers};
  auto dataset = BellhopAsciiArrivalParser::ParseFile(path, options);
  if(!dataset) {
    std::cerr << "LEGACY_CHARACTERIZATION_PARSE_ERROR="
              << dataset.error().message << '\n';
    return false;
  }
  std::vector<BellhopRawArrivalDataset> datasets;
  datasets.push_back(std::move(*dataset));
  auto bundle = BellhopRawArrivalBundle::Create(std::move(datasets));
  if(!bundle) return false;
  const auto& raw_dataset = bundle->datasets().front();
  auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!frame) return false;
  auto asset = BellhopRawArrivalNormalizer::Normalize(
      *bundle, 1U, path.string(), *frame);
  if(!asset) {
    std::cerr << "LEGACY_CHARACTERIZATION_NORMALIZATION_ERROR="
              << asset.error().message << '\n';
    return false;
  }

  std::size_t no_arrival_count = 0U;
  std::size_t signal_count = 0U;
  auto minimum_delay = std::numeric_limits<NanosecondCount>::max();
  NanosecondCount maximum_delay = 0;
  auto minimum_loss = std::numeric_limits<double>::infinity();
  auto maximum_loss = -std::numeric_limits<double>::infinity();
  auto minimum_gain = std::numeric_limits<double>::infinity();
  double maximum_gain = 0.0;
  for(const auto& cell : asset->cells()) {
    const auto* signal = std::get_if<AcousticFieldSignalCell>(&cell);
    if(signal == nullptr) {
      ++no_arrival_count;
      continue;
    }
    ++signal_count;
    minimum_delay = std::min(
        minimum_delay, signal->first_arrival_delay.nanoseconds());
    maximum_delay = std::max(
        maximum_delay, signal->first_arrival_delay.nanoseconds());
    minimum_loss =
        std::min(minimum_loss, signal->aggregate_transmission_loss_db);
    maximum_loss =
        std::max(maximum_loss, signal->aggregate_transmission_loss_db);
    for(const auto& path_value : signal->paths) {
      minimum_gain =
          std::min(minimum_gain, path_value.pressure_gain_linear());
      maximum_gain =
          std::max(maximum_gain, path_value.pressure_gain_linear());
    }
  }

  std::cout << "LEGACY_CHARACTERIZATION frequency_hz="
            << raw_dataset.frequency_hz()
            << " source_depths=" << raw_dataset.source_depths_m().size()
            << " receiver_depths="
            << raw_dataset.receiver_depths_m().size()
            << " ranges=" << raw_dataset.receiver_ranges_m().size()
            << " raw_cells=" << raw_dataset.cells().size()
            << " raw_arrivals=" << raw_dataset.total_arrival_count()
            << " no_arrival_cells=" << no_arrival_count
            << " signal_cells=" << signal_count
            << " min_first_delay_ns=" << minimum_delay
            << " max_first_delay_ns=" << maximum_delay
            << " min_tl_db=" << minimum_loss
            << " max_tl_db=" << maximum_loss
            << " min_gain=" << minimum_gain
            << " max_gain=" << maximum_gain << '\n';

  for(std::size_t index = 0U; index < raw_dataset.cells().size();
      ++index) {
    const auto& raw_cell = raw_dataset.cells()[index];
    if(raw_cell.arrivals.empty()) continue;
    const auto& raw = raw_cell.arrivals.front();
    const auto omega = 2.0L * std::numbers::pi_v<long double> *
                       raw_dataset.frequency_hz();
    const auto normalized =
        ns3_factory::environment::internal::import::detail::
            NormalizeBellhopPath(raw, omega);
    const auto* signal =
        std::get_if<AcousticFieldSignalCell>(&asset->cells()[index]);
    if(!normalized || signal == nullptr) return false;
    const auto excess = CheckedSubtract(
        normalized->absolute_delay, signal->first_arrival_delay);
    if(!excess) return false;
    std::cout << "LEGACY_FIRST_NONEMPTY cell=" << index
              << " raw_magnitude=" << raw.raw_magnitude()
              << " raw_phase_deg=" << raw.raw_phase_degrees()
              << " raw_delay_real_s="
              << raw.raw_delay_real_seconds()
              << " raw_delay_imag_s="
              << raw.raw_delay_imag_seconds()
              << " normalized_gain="
              << normalized->pressure_gain_linear
              << " normalized_phase_rad="
              << normalized->phase_radians
              << " absolute_delay_ns="
              << normalized->absolute_delay.nanoseconds()
              << " excess_delay_ns=" << excess->nanoseconds() << '\n';
    if(raw.raw_delay_imag_seconds() == 0.0 &&
       normalized->pressure_gain_linear != raw.raw_magnitude()) {
      return false;
    }
    break;
  }
  return true;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  const auto tests_pass =
      TestGoldenComplexSemanticsAndAggregateLoss() &&
      TestAggregateLossIgnoresPhase() &&
      TestDelayRoundingAndNoArrivalClassification() &&
      TestBundleAxesAndFrequencyMajorCellOrderArePreserved() &&
      TestExplicitNormalizationFailures();
  if(!tests_pass) return EXIT_FAILURE;
  if(argc == 2 && !CharacterizeLegacyFile(argv[1])) return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
