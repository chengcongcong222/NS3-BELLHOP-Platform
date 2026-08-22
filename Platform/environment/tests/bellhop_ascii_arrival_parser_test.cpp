#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>

#include <ns3_factory/contracts/errors.hpp>

#include "internal/import/bellhop_ascii_arrival_parser.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::environment::internal::import;

static_assert(!std::is_default_constructible_v<
              BellhopArrivalImportOptions>);
static_assert(std::is_constructible_v<BellhopArrivalImportOptions,
                                      BellhopReceiverRangeUnit>);

namespace {

auto Options(BellhopReceiverRangeUnit unit)
    -> BellhopArrivalImportOptions {
  return BellhopArrivalImportOptions{unit};
}

auto ParseText(const std::string& text,
               BellhopArrivalImportOptions options =
                   Options(BellhopReceiverRangeUnit::kMeters))
    -> Result<BellhopRawArrivalDataset> {
  std::istringstream input{text};
  return BellhopAsciiArrivalParser::Parse(input, options);
}

auto SyntheticTwoByTwoByTwo() -> std::string {
  return
      "'2D'\n"
      "20000\n"
      "2 10 20\n"
      "2 30 40\n"
      "2 1.5 2.5\n"
      "3\n"
      "0\n"
      "1\n"
      "1 -180 0.1 -0.01 5 -5 0 1\n"
      "2\n"
      "2 0 0.2 0 6 -6 2 3\n"
      "3 270 0.3 0.02 7 -7 4 5\n"
      "0\n"
      "2\n"
      "1\n"
      "4 45 0.4 -0.03 8 -8 6 7\n"
      "0\n"
      "0\n"
      "1\n"
      "5 90 0.5 0.04 9 -9 8 9\n";
}

auto MinimalFile(std::string frequency = "20000",
                 std::string arrival = "1 0 0.1 0 0 0 0 0",
                 std::string trailing = "") -> std::string {
  return "'2D'\n" + frequency + "\n1 10\n1 20\n1 1\n1\n1\n" +
         arrival + "\n" + trailing;
}

auto EmptyGridFile(std::string source_axis,
                   std::string receiver_axis,
                   std::string range_axis,
                   std::string source_blocks) -> std::string {
  return "'2D'\n20000\n" + source_axis + "\n" + receiver_axis +
         "\n" + range_axis + "\n" + source_blocks;
}

auto TestSyntheticGrammarAndRawPreservation() -> bool {
  const auto dataset = ParseText(SyntheticTwoByTwoByTwo());
  if(!dataset || dataset->frequency_hz() != 20'000.0 ||
     dataset->source_depths_m().size() != 2U ||
     dataset->receiver_depths_m().size() != 2U ||
     dataset->receiver_ranges_m().size() != 2U ||
     dataset->source_depths_m()[0] != 10.0 ||
     dataset->source_depths_m()[1] != 20.0 ||
     dataset->receiver_depths_m()[0] != 30.0 ||
     dataset->receiver_depths_m()[1] != 40.0 ||
     dataset->receiver_ranges_m()[0] != 1.5 ||
     dataset->receiver_ranges_m()[1] != 2.5 ||
     dataset->cells().size() != 8U ||
     dataset->total_arrival_count() != 5U) {
    return false;
  }
  if(!dataset->cell(0U, 0U, 0U).arrivals.empty() ||
     dataset->cell(0U, 0U, 1U).arrivals.size() != 1U ||
     dataset->cell(0U, 1U, 0U).arrivals.size() != 2U ||
     !dataset->cell(0U, 1U, 1U).arrivals.empty() ||
     dataset->cell(1U, 0U, 0U).arrivals.size() != 1U ||
     !dataset->cell(1U, 0U, 1U).arrivals.empty() ||
     !dataset->cell(1U, 1U, 0U).arrivals.empty() ||
     dataset->cell(1U, 1U, 1U).arrivals.size() != 1U) {
    return false;
  }

  const auto& negative_imag =
      dataset->cell(0U, 0U, 1U).arrivals.front();
  const auto& zero_imag = dataset->cell(0U, 1U, 0U).arrivals[0];
  const auto& positive_imag = dataset->cell(0U, 1U, 0U).arrivals[1];
  return negative_imag.raw_magnitude() == 1.0 &&
         negative_imag.raw_phase_degrees() == -180.0 &&
         negative_imag.raw_delay_real_seconds() == 0.1 &&
         negative_imag.raw_delay_imag_seconds() == -0.01 &&
         negative_imag.raw_source_angle_degrees() == 5.0 &&
         negative_imag.raw_receiver_angle_degrees() == -5.0 &&
         negative_imag.raw_top_bounce_count() == 0U &&
         negative_imag.raw_bottom_bounce_count() == 1U &&
         zero_imag.raw_phase_degrees() == 0.0 &&
         zero_imag.raw_delay_imag_seconds() == 0.0 &&
         positive_imag.raw_phase_degrees() == 270.0 &&
         positive_imag.raw_delay_imag_seconds() == 0.02;
}

auto TestExplicitRangeUnitsAndDeterminism() -> bool {
  const auto text = SyntheticTwoByTwoByTwo();
  const auto meters = ParseText(
      text, Options(BellhopReceiverRangeUnit::kMeters));
  const auto kilometers = ParseText(
      text, Options(BellhopReceiverRangeUnit::kKilometers));
  const auto repeated = ParseText(
      text, Options(BellhopReceiverRangeUnit::kMeters));
  return meters && kilometers && repeated && *meters == *repeated &&
         meters->receiver_ranges_m()[0] == 1.5 &&
         meters->receiver_ranges_m()[1] == 2.5 &&
         kilometers->receiver_ranges_m()[0] == 1'500.0 &&
         kilometers->receiver_ranges_m()[1] == 2'500.0;
}

auto TestAxisValidation() -> bool {
  const auto duplicate_source = ParseText(EmptyGridFile(
      "2 10 10", "1 20", "1 1", "0\n0\n0\n0\n"));
  const auto decreasing_receiver = ParseText(EmptyGridFile(
      "1 10", "2 30 20", "1 1", "0\n0\n0\n"));
  const auto duplicate_range = ParseText(EmptyGridFile(
      "1 10", "1 20", "2 1 1", "0\n0\n0\n"));
  const auto negative_depth = ParseText(EmptyGridFile(
      "1 -1", "1 20", "1 1", "0\n0\n"));
  const auto negative_range = ParseText(EmptyGridFile(
      "1 10", "1 20", "1 -1", "0\n0\n"));
  const auto empty_axis = ParseText(EmptyGridFile(
      "0", "1 20", "1 1", ""));
  const auto nonfinite_axis = ParseText(EmptyGridFile(
      "1 nan", "1 20", "1 1", "0\n0\n"));
  return !duplicate_source && !decreasing_receiver && !duplicate_range &&
         !negative_depth && !negative_range && !empty_axis &&
         !nonfinite_axis;
}

auto TestArrivalValidation() -> bool {
  const auto nonfinite = ParseText(MinimalFile("20000",
                                               "nan 0 0.1 0 0 0 0 0"));
  const auto negative_magnitude = ParseText(MinimalFile(
      "20000", "-1 0 0.1 0 0 0 0 0"));
  const auto negative_real_delay = ParseText(MinimalFile(
      "20000", "1 0 -0.1 0 0 0 0 0"));
  const auto negative_top_bounce = ParseText(MinimalFile(
      "20000", "1 0 0.1 0 0 0 -1 0"));
  const auto negative_bottom_bounce = ParseText(MinimalFile(
      "20000", "1 0 0.1 0 0 0 0 -1"));
  const auto nonfinite_frequency = ParseText(MinimalFile("inf"));
  const auto negative_frequency = ParseText(MinimalFile("-20000"));
  return !nonfinite && !negative_magnitude && !negative_real_delay &&
         !negative_top_bounce && !negative_bottom_bounce &&
         !nonfinite_frequency && !negative_frequency;
}

auto TestCountsAndAllocationSafety() -> bool {
  auto cell_limit_options = Options(BellhopReceiverRangeUnit::kMeters);
  cell_limit_options.limits.max_spatial_cells = 7U;
  auto arrival_limit_options = Options(BellhopReceiverRangeUnit::kMeters);
  arrival_limit_options.limits.max_arrivals_per_cell = 1U;
  auto total_limit_options = Options(BellhopReceiverRangeUnit::kMeters);
  total_limit_options.limits.max_total_arrivals = 4U;
  const auto cell_limit =
      ParseText(SyntheticTwoByTwoByTwo(), cell_limit_options);
  const auto arrival_limit =
      ParseText(SyntheticTwoByTwoByTwo(), arrival_limit_options);
  const auto total_limit =
      ParseText(SyntheticTwoByTwoByTwo(), total_limit_options);
  const auto bad_count = ParseText(
      "'2D'\n20000\n-1\n");
  const auto count_overflow = ParseText(
      "'2D'\n20000\n184467440737095516160\n");
  const auto narr_exceeds_narrmx = ParseText(
      "'2D'\n20000\n1 10\n1 20\n1 1\n1\n2\n");
  const auto dimension_overflow = CheckedRawArrivalCellCount(
      std::numeric_limits<std::size_t>::max(), 2U, 1U);
  return !cell_limit && !arrival_limit && !total_limit && !bad_count &&
         !count_overflow && !narr_exceeds_narrmx &&
         !dimension_overflow &&
         dimension_overflow.error().code == ErrorCode::kOverflow;
}

auto TestCompletenessAndUnsupportedInput() -> bool {
  const auto early_eof = ParseText(MinimalFile(
      "20000", "1 0 0.1"));
  const auto wrong_numeric = ParseText(MinimalFile(
      "20000", "oops 0 0.1 0 0 0 0 0"));
  const auto trailing = ParseText(MinimalFile(
      "20000", "1 0 0.1 0 0 0 0 0", "unexpected\n"));
  const auto unsupported = ParseText(
      "'3D'\n20000\n");
  const auto unquoted = ParseText(
      "2D\n20000\n");
  return !early_eof && !wrong_numeric && !trailing && !unsupported &&
         !unquoted && unsupported.error().code == ErrorCode::kUnsupported;
}

auto TestZeroNarrIsAValidEmptyCell() -> bool {
  const auto dataset = ParseText(
      "'2D'\n20000\n1 10\n1 20\n1 1\n0\n0\n");
  return dataset && dataset->cells().size() == 1U &&
         dataset->cell(0U, 0U, 0U).arrivals.empty() &&
         dataset->total_arrival_count() == 0U;
}

}  // namespace

auto main() -> int {
  return TestSyntheticGrammarAndRawPreservation() &&
                 TestExplicitRangeUnitsAndDeterminism() &&
                 TestAxisValidation() && TestArrivalValidation() &&
                 TestCountsAndAllocationSafety() &&
                 TestCompletenessAndUnsupportedInput() &&
                 TestZeroNarrIsAValidEmptyCell()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
