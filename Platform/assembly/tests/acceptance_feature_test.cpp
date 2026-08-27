#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

#include "internal/acceptance_feature.hpp"

namespace {

using namespace ns3_factory::assembly::internal;
using namespace ns3_factory::contracts;

static_assert(kDetectionFeatureV1PayloadBytes == 15);

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

template <typename Accumulator>
concept AcceptsTargetTruth = requires(Accumulator& accumulator) {
  accumulator.TryFuse(At(1), 0.0, 0.0, 200.0, 150.0);
};

static_assert(!AcceptsTargetTruth<FeatureFusionAccumulator>);

auto Report(std::uint16_t sequence,
            std::uint32_t sample_ms,
            std::int16_t x,
            std::int16_t y,
            std::int16_t bearing) -> DetectionFeatureReportV1 {
  return DetectionFeatureReportV1{
      sequence, sample_ms, x, y, bearing, 100, 0};
}

auto TestCodecRoundTripAndCanonicalBytes() -> bool {
  const auto report = DetectionFeatureReportV1{
      0x1234, 0x01020304, -2, 300, -1234, 80, 0};
  const auto encoded = EncodeDetectionFeatureReportV1(report);
  if(!encoded) return false;
  const std::array<std::byte, 15> expected{
      std::byte{0x11}, std::byte{0x34}, std::byte{0x12},
      std::byte{0x04}, std::byte{0x03}, std::byte{0x02},
      std::byte{0x01}, std::byte{0xfe}, std::byte{0xff},
      std::byte{0x2c}, std::byte{0x01}, std::byte{0x2e},
      std::byte{0xfb}, std::byte{0x50}, std::byte{0x00}};
  const auto decoded = DecodeDetectionFeatureReportV1(*encoded);
  return *encoded == expected && decoded && *decoded == report;
}

auto TestCodecValidation() -> bool {
  auto invalid_version =
      *EncodeDetectionFeatureReportV1(Report(1, 0, 0, 0, 0));
  invalid_version[0] = std::byte{0x12};
  auto invalid_flags = Report(1, 0, 0, 0, 0);
  invalid_flags.flags = 1;
  auto invalid_bearing = Report(1, 0, 0, 0, 18'000);
  auto invalid_confidence = Report(1, 0, 0, 0, 0);
  invalid_confidence.confidence_percent = 0;
  const std::array<std::byte, 14> short_payload{};
  return !DecodeDetectionFeatureReportV1(invalid_version) &&
         !DecodeDetectionFeatureReportV1(short_payload) &&
         !EncodeDetectionFeatureReportV1(invalid_flags) &&
         !EncodeDetectionFeatureReportV1(invalid_bearing) &&
         !EncodeDetectionFeatureReportV1(invalid_confidence);
}

auto TestBearingAndReportGeneration() -> bool {
  const auto east = GenerateGlobalBearingCentidegrees(0, 0, 10, 0);
  const auto north = GenerateGlobalBearingCentidegrees(0, 0, 0, 10);
  const auto west = GenerateGlobalBearingCentidegrees(0, 0, -10, 0);
  const auto generated = MakeDetectionFeatureReportV1(
      7, At(1'000'000'000), At(3'500'000'000), 12.4, -4.6, 20, 20);
  const auto position_overflow = MakeDetectionFeatureReportV1(
      1, At(0), At(0), 40'000, 0, 0, 1);
  const auto time_overflow = MakeDetectionFeatureReportV1(
      1,
      At(0),
      At(4'294'967'296'000'000),
      0,
      0,
      1,
      1);
  return east && *east == 0 && north && *north == 9'000 && west &&
         *west == -18'000 && generated &&
         generated->sample_time_ms_from_run_start == 2'500 &&
         generated->sensor_x_meters_quantized == 12 &&
         generated->sensor_y_meters_quantized == -5 &&
         !position_overflow && !time_overflow &&
         !GenerateGlobalBearingCentidegrees(1, 1, 1, 1);
}

auto BearingFrom(std::int16_t x,
                 std::int16_t y,
                 double target_x,
                 double target_y) -> std::int16_t {
  return *GenerateGlobalBearingCentidegrees(
      static_cast<double>(x),
      static_cast<double>(y),
      target_x,
      target_y);
}

auto TestUniqueCountingAndLeastSquaresFusion() -> bool {
  auto accumulator = FeatureFusionAccumulator::Create(
      5, At(0), SimDuration::FromNanoseconds(180'000'000'000));
  if(!accumulator) return false;
  constexpr auto kTargetX = 200.0;
  constexpr auto kTargetY = 150.0;
  const std::array<std::pair<std::int16_t, std::int16_t>, 5> positions{
      std::pair<std::int16_t, std::int16_t>{1'000, 0},
      {-950, 250},
      {0, -1'050},
      {750, 700},
      {-700, -700}};
  for(std::size_t index = 0; index < positions.size(); ++index) {
    const auto [x, y] = positions[index];
    const auto outcome = accumulator->Ingest(
        NodeId{10 + index * 10},
        Report(static_cast<std::uint16_t>(index + 1),
               static_cast<std::uint32_t>(index * 4'000),
               x,
               y,
               BearingFrom(x, y, kTargetX, kTargetY)));
    if(!outcome || *outcome != FeatureIngestOutcome::kAccepted) return false;
  }
  const auto duplicate = accumulator->Ingest(
      NodeId{10},
      Report(1,
             99'000,
             positions[0].first,
             positions[0].second,
             BearingFrom(positions[0].first,
                         positions[0].second,
                         kTargetX,
                         kTargetY)));
  const auto fused = accumulator->TryFuse(At(20'000'000'000), 0, 0);
  return duplicate && *duplicate == FeatureIngestOutcome::kDuplicate &&
         accumulator->unique_observation_count() == 5 && fused && *fused &&
         (**fused).observation_count == 5 &&
         (**fused).observation_identities.size() == 5 &&
         std::abs((**fused).estimated_target_x_meters - kTargetX) < 1.0 &&
         std::abs((**fused).estimated_target_y_meters - kTargetY) < 1.0 &&
         (**fused).fusion_period.nanoseconds() == 20'000'000'000 &&
         (**fused).meets_period_requirement &&
         accumulator->active_observation_count() == 0;
}

auto TestInsufficientAndDegenerateGeometry() -> bool {
  auto insufficient = FeatureFusionAccumulator::Create(
      5, At(0), SimDuration::FromNanoseconds(180'000'000'000));
  auto degenerate = FeatureFusionAccumulator::Create(
      5, At(0), SimDuration::FromNanoseconds(180'000'000'000));
  if(!insufficient || !degenerate) return false;
  if(!insufficient->Ingest(NodeId{1}, Report(1, 0, 0, 0, 0))) return false;
  const auto no_result = insufficient->TryFuse(At(1), 0, 0);
  for(std::uint16_t sequence = 1; sequence <= 5; ++sequence) {
    if(!degenerate->Ingest(
           NodeId{sequence}, Report(sequence, sequence, 0, sequence, 0))) {
      return false;
    }
  }
  const auto failure = degenerate->TryFuse(At(10'000'000), 0, 0);
  return no_result && !*no_result && !failure &&
         failure.error().code == ErrorCode::kFailedPrecondition;
}

}  // namespace

auto main() -> int {
  return TestCodecRoundTripAndCanonicalBytes() && TestCodecValidation() &&
                 TestBearingAndReportGeneration() &&
                 TestUniqueCountingAndLeastSquaresFusion() &&
                 TestInsufficientAndDegenerateGeometry()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
