#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::assembly::internal {

struct DetectionFeatureReportV1 final {
  std::uint16_t observation_sequence;
  std::uint32_t sample_time_ms_from_run_start;
  std::int16_t sensor_x_meters_quantized;
  std::int16_t sensor_y_meters_quantized;
  std::int16_t bearing_centidegrees;
  std::uint8_t confidence_percent;
  std::uint8_t flags;

  constexpr auto operator==(const DetectionFeatureReportV1&) const noexcept
      -> bool = default;
};

inline constexpr auto kDetectionFeatureV1TypeAndVersion =
    std::uint8_t{0x11};
inline constexpr auto kDetectionFeatureV1PayloadBytes = std::size_t{15};
inline constexpr auto kDetectionFeatureV1AllowedFlags = std::uint8_t{0};

[[nodiscard]] auto EncodeDetectionFeatureReportV1(
    const DetectionFeatureReportV1& report)
    -> contracts::Result<
        std::array<std::byte, kDetectionFeatureV1PayloadBytes>>;

[[nodiscard]] auto DecodeDetectionFeatureReportV1(
    std::span<const std::byte> payload)
    -> contracts::Result<DetectionFeatureReportV1>;

[[nodiscard]] auto GenerateGlobalBearingCentidegrees(
    double sensor_x_meters,
    double sensor_y_meters,
    double target_x_meters,
    double target_y_meters,
    double deterministic_offset_degrees = 0.0)
    -> contracts::Result<std::int16_t>;

[[nodiscard]] auto MakeDetectionFeatureReportV1(
    std::uint16_t observation_sequence,
    contracts::SimTime run_started_at,
    contracts::SimTime sample_time,
    double sensor_x_meters,
    double sensor_y_meters,
    double target_x_meters,
    double target_y_meters,
    std::uint8_t confidence_percent = 100,
    std::uint8_t flags = 0,
    double deterministic_bearing_offset_degrees = 0.0)
    -> contracts::Result<DetectionFeatureReportV1>;

struct ObservationIdentity final {
  contracts::NodeId sender_node_id;
  std::uint16_t observation_sequence;

  constexpr auto operator<=>(const ObservationIdentity&) const noexcept =
      default;
};

struct BearingFeatureObservation final {
  ObservationIdentity identity;
  contracts::SimTime sample_time;
  double sensor_x_meters;
  double sensor_y_meters;
  std::int16_t bearing_centidegrees;
  std::uint8_t confidence_percent;

  auto operator==(const BearingFeatureObservation&) const -> bool = default;
};

struct FusionResult final {
  std::uint64_t fusion_sequence;
  contracts::SimTime started_at;
  contracts::SimTime completed_at;
  contracts::SimDuration fusion_period;
  std::size_t observation_count;
  std::vector<ObservationIdentity> observation_identities;
  double estimated_target_x_meters;
  double estimated_target_y_meters;
  std::int16_t fusion_center_bearing_centidegrees;
  double residual_rms_meters;
  bool meets_period_requirement;

  auto operator==(const FusionResult&) const -> bool = default;
};

class FusionResultStore final {
 public:
  [[nodiscard]] auto Append(FusionResult result) -> contracts::Status {
    if(!results_.empty() &&
       result.fusion_sequence <= results_.back().fusion_sequence) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kAlreadyExists,
              "Fusion result sequence must be strictly increasing"});
    }
    results_.push_back(std::move(result));
    return {};
  }

  [[nodiscard]] auto results() const noexcept
      -> std::span<const FusionResult> {
    return std::span<const FusionResult>{results_};
  }

 private:
  std::vector<FusionResult> results_;
};

enum class FeatureIngestOutcome {
  kAccepted,
  kDuplicate,
};

class FeatureFusionAccumulator final {
 public:
  [[nodiscard]] static auto Create(
      std::size_t minimum_bearing_points,
      contracts::SimTime run_started_at,
      contracts::SimDuration maximum_fusion_period)
      -> contracts::Result<FeatureFusionAccumulator>;

  [[nodiscard]] auto Ingest(
      contracts::NodeId sender_node_id,
      const DetectionFeatureReportV1& report)
      -> contracts::Result<FeatureIngestOutcome>;

  [[nodiscard]] auto TryFuse(
      contracts::SimTime completed_at,
      double fusion_center_x_meters,
      double fusion_center_y_meters)
      -> contracts::Result<std::optional<FusionResult>>;

  [[nodiscard]] constexpr auto active_observation_count() const noexcept
      -> std::size_t {
    return active_observations_.size();
  }

  [[nodiscard]] constexpr auto unique_observation_count() const noexcept
      -> std::size_t {
    return seen_identities_.size();
  }

 private:
  FeatureFusionAccumulator(
      std::size_t minimum_bearing_points,
      contracts::SimTime run_started_at,
      contracts::SimDuration maximum_fusion_period) noexcept
      : minimum_bearing_points_(minimum_bearing_points),
        run_started_at_(run_started_at),
        maximum_fusion_period_(maximum_fusion_period) {}

  std::size_t minimum_bearing_points_;
  contracts::SimTime run_started_at_;
  contracts::SimDuration maximum_fusion_period_;
  std::vector<ObservationIdentity> seen_identities_;
  std::vector<BearingFeatureObservation> active_observations_;
  std::uint64_t next_fusion_sequence_{1};
};

namespace acceptance_feature_detail {

[[nodiscard]] inline auto ValidationError(const char* message)
    -> contracts::Error {
  return contracts::Error{contracts::ErrorCode::kInvalidArgument, message};
}

[[nodiscard]] inline auto Validate(
    const DetectionFeatureReportV1& report) -> contracts::Status {
  if(report.bearing_centidegrees < -18'000 ||
     report.bearing_centidegrees >= 18'000) {
    return std::unexpected(
        ValidationError("Feature bearing must be within [-180, 180) degrees"));
  }
  if(report.confidence_percent == 0 || report.confidence_percent > 100) {
    return std::unexpected(
        ValidationError("Feature confidence must be within [1, 100]"));
  }
  if((report.flags & ~kDetectionFeatureV1AllowedFlags) != 0) {
    return std::unexpected(
        ValidationError("Feature report contains unsupported flags"));
  }
  return {};
}

inline auto WriteU16(std::array<std::byte, 15>& bytes,
                     std::size_t offset,
                     std::uint16_t value) noexcept -> void {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

inline auto WriteU32(std::array<std::byte, 15>& bytes,
                     std::size_t offset,
                     std::uint32_t value) noexcept -> void {
  for(std::size_t index = 0; index < 4; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (8U * index)) & 0xffU);
  }
}

[[nodiscard]] inline auto ReadU16(std::span<const std::byte> bytes,
                                  std::size_t offset) noexcept
    -> std::uint16_t {
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(
             bytes[offset])) |
         static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(
             bytes[offset + 1]))
             << 8U;
}

[[nodiscard]] inline auto ReadU32(std::span<const std::byte> bytes,
                                  std::size_t offset) noexcept
    -> std::uint32_t {
  auto value = std::uint32_t{0};
  for(std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<std::uint8_t>(bytes[offset + index]))
             << (8U * index);
  }
  return value;
}

[[nodiscard]] inline auto ReadI16(std::span<const std::byte> bytes,
                                  std::size_t offset) noexcept
    -> std::int16_t {
  const auto encoded = ReadU16(bytes, offset);
  if(encoded <= static_cast<std::uint16_t>(
                    std::numeric_limits<std::int16_t>::max())) {
    return static_cast<std::int16_t>(encoded);
  }
  const auto magnitude_minus_one =
      static_cast<std::uint16_t>(std::numeric_limits<std::uint16_t>::max() -
                                 encoded);
  return static_cast<std::int16_t>(
      -1 - static_cast<std::int32_t>(magnitude_minus_one));
}

}  // namespace acceptance_feature_detail

inline auto EncodeDetectionFeatureReportV1(
    const DetectionFeatureReportV1& report)
    -> contracts::Result<
        std::array<std::byte, kDetectionFeatureV1PayloadBytes>> {
  const auto valid = acceptance_feature_detail::Validate(report);
  if(!valid) return std::unexpected(valid.error());
  std::array<std::byte, kDetectionFeatureV1PayloadBytes> bytes{};
  bytes[0] = static_cast<std::byte>(kDetectionFeatureV1TypeAndVersion);
  acceptance_feature_detail::WriteU16(
      bytes, 1, report.observation_sequence);
  acceptance_feature_detail::WriteU32(
      bytes, 3, report.sample_time_ms_from_run_start);
  acceptance_feature_detail::WriteU16(
      bytes,
      7,
      static_cast<std::uint16_t>(report.sensor_x_meters_quantized));
  acceptance_feature_detail::WriteU16(
      bytes,
      9,
      static_cast<std::uint16_t>(report.sensor_y_meters_quantized));
  acceptance_feature_detail::WriteU16(
      bytes, 11, static_cast<std::uint16_t>(report.bearing_centidegrees));
  bytes[13] = static_cast<std::byte>(report.confidence_percent);
  bytes[14] = static_cast<std::byte>(report.flags);
  return bytes;
}

inline auto DecodeDetectionFeatureReportV1(
    std::span<const std::byte> payload)
    -> contracts::Result<DetectionFeatureReportV1> {
  if(payload.size() != kDetectionFeatureV1PayloadBytes ||
     std::to_integer<std::uint8_t>(payload[0]) !=
         kDetectionFeatureV1TypeAndVersion) {
    return std::unexpected(
        acceptance_feature_detail::ValidationError(
            "Feature payload length or type/version is invalid"));
  }
  const auto report = DetectionFeatureReportV1{
      acceptance_feature_detail::ReadU16(payload, 1),
      acceptance_feature_detail::ReadU32(payload, 3),
      acceptance_feature_detail::ReadI16(payload, 7),
      acceptance_feature_detail::ReadI16(payload, 9),
      acceptance_feature_detail::ReadI16(payload, 11),
      std::to_integer<std::uint8_t>(payload[13]),
      std::to_integer<std::uint8_t>(payload[14])};
  const auto valid = acceptance_feature_detail::Validate(report);
  if(!valid) return std::unexpected(valid.error());
  return report;
}

inline auto GenerateGlobalBearingCentidegrees(
    double sensor_x_meters,
    double sensor_y_meters,
    double target_x_meters,
    double target_y_meters,
    double deterministic_offset_degrees)
    -> contracts::Result<std::int16_t> {
  if(!std::isfinite(sensor_x_meters) ||
     !std::isfinite(sensor_y_meters) ||
     !std::isfinite(target_x_meters) ||
     !std::isfinite(target_y_meters) ||
     !std::isfinite(deterministic_offset_degrees)) {
    return std::unexpected(
        acceptance_feature_detail::ValidationError(
            "Bearing geometry and deterministic offset must be finite"));
  }
  const auto dx = target_x_meters - sensor_x_meters;
  const auto dy = target_y_meters - sensor_y_meters;
  if(dx == 0.0 && dy == 0.0) {
    return std::unexpected(
        acceptance_feature_detail::ValidationError(
            "Bearing is undefined at the target position"));
  }
  auto degrees = std::atan2(dy, dx) *
                     180.0 / std::numbers::pi +
                 deterministic_offset_degrees;
  degrees = std::fmod(degrees + 180.0, 360.0);
  if(degrees < 0.0) degrees += 360.0;
  degrees -= 180.0;
  auto quantized = std::llround(degrees * 100.0);
  if(quantized == 18'000) quantized = -18'000;
  if(quantized < -18'000 || quantized >= 18'000) {
    return std::unexpected(
        acceptance_feature_detail::ValidationError(
            "Quantized bearing is outside the V1 representation"));
  }
  return static_cast<std::int16_t>(quantized);
}

inline auto MakeDetectionFeatureReportV1(
    std::uint16_t observation_sequence,
    contracts::SimTime run_started_at,
    contracts::SimTime sample_time,
    double sensor_x_meters,
    double sensor_y_meters,
    double target_x_meters,
    double target_y_meters,
    std::uint8_t confidence_percent,
    std::uint8_t flags,
    double deterministic_bearing_offset_degrees)
    -> contracts::Result<DetectionFeatureReportV1> {
  const auto elapsed = contracts::CheckedSubtract(sample_time, run_started_at);
  if(!elapsed || *elapsed < contracts::SimDuration::Zero()) {
    return std::unexpected(
        acceptance_feature_detail::ValidationError(
            "Feature sample time precedes the run start"));
  }
  constexpr auto kNanosecondsPerMillisecond = std::int64_t{1'000'000};
  const auto elapsed_ms = elapsed->nanoseconds() /
                          kNanosecondsPerMillisecond;
  if(elapsed_ms > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Feature sample time exceeds V1 range"});
  }
  const auto quantize_position = [](double meters)
      -> contracts::Result<std::int16_t> {
    if(!std::isfinite(meters)) {
      return std::unexpected(
          acceptance_feature_detail::ValidationError(
              "Feature sample position must be finite"));
    }
    if(meters <
           static_cast<double>(std::numeric_limits<std::int16_t>::min()) -
               0.5 ||
       meters >
           static_cast<double>(std::numeric_limits<std::int16_t>::max()) +
               0.5) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Feature sample position exceeds V1 range"});
    }
    const auto rounded = std::llround(meters);
    if(rounded < std::numeric_limits<std::int16_t>::min() ||
       rounded > std::numeric_limits<std::int16_t>::max()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Feature sample position exceeds V1 range"});
    }
    return static_cast<std::int16_t>(rounded);
  };
  const auto x = quantize_position(sensor_x_meters);
  const auto y = quantize_position(sensor_y_meters);
  const auto bearing = GenerateGlobalBearingCentidegrees(
      sensor_x_meters,
      sensor_y_meters,
      target_x_meters,
      target_y_meters,
      deterministic_bearing_offset_degrees);
  if(!x) return std::unexpected(x.error());
  if(!y) return std::unexpected(y.error());
  if(!bearing) return std::unexpected(bearing.error());
  DetectionFeatureReportV1 report{
      observation_sequence,
      static_cast<std::uint32_t>(elapsed_ms),
      *x,
      *y,
      *bearing,
      confidence_percent,
      flags};
  const auto valid = acceptance_feature_detail::Validate(report);
  if(!valid) return std::unexpected(valid.error());
  return report;
}

inline auto FeatureFusionAccumulator::Create(
    std::size_t minimum_bearing_points,
    contracts::SimTime run_started_at,
    contracts::SimDuration maximum_fusion_period)
    -> contracts::Result<FeatureFusionAccumulator> {
  if(minimum_bearing_points == 0 ||
     maximum_fusion_period <= contracts::SimDuration::Zero()) {
    return std::unexpected(
        acceptance_feature_detail::ValidationError(
            "Fusion thresholds must be positive"));
  }
  return FeatureFusionAccumulator{
      minimum_bearing_points, run_started_at, maximum_fusion_period};
}

inline auto FeatureFusionAccumulator::Ingest(
    contracts::NodeId sender_node_id,
    const DetectionFeatureReportV1& report)
    -> contracts::Result<FeatureIngestOutcome> {
  const auto valid = acceptance_feature_detail::Validate(report);
  if(!valid) return std::unexpected(valid.error());
  const auto identity =
      ObservationIdentity{sender_node_id, report.observation_sequence};
  const auto position = std::lower_bound(
      seen_identities_.begin(), seen_identities_.end(), identity);
  if(position != seen_identities_.end() && *position == identity) {
    return FeatureIngestOutcome::kDuplicate;
  }
  constexpr auto kNanosecondsPerMillisecond = std::int64_t{1'000'000};
  const auto elapsed = contracts::SimDuration::FromNanoseconds(
      static_cast<std::int64_t>(report.sample_time_ms_from_run_start) *
      kNanosecondsPerMillisecond);
  const auto sample_time = contracts::CheckedAdd(run_started_at_, elapsed);
  if(!sample_time) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Feature sample SimTime overflowed"});
  }
  seen_identities_.insert(position, identity);
  active_observations_.push_back(
      BearingFeatureObservation{identity,
                                *sample_time,
                                static_cast<double>(
                                    report.sensor_x_meters_quantized),
                                static_cast<double>(
                                    report.sensor_y_meters_quantized),
                                report.bearing_centidegrees,
                                report.confidence_percent});
  return FeatureIngestOutcome::kAccepted;
}

inline auto FeatureFusionAccumulator::TryFuse(
    contracts::SimTime completed_at,
    double fusion_center_x_meters,
    double fusion_center_y_meters)
    -> contracts::Result<std::optional<FusionResult>> {
  if(active_observations_.size() < minimum_bearing_points_) {
    return std::optional<FusionResult>{};
  }
  if(!std::isfinite(fusion_center_x_meters) ||
     !std::isfinite(fusion_center_y_meters)) {
    return std::unexpected(
        acceptance_feature_detail::ValidationError(
            "Fusion center position must be finite"));
  }
  auto observations = active_observations_;
  std::sort(observations.begin(), observations.end(),
            [](const BearingFeatureObservation& lhs,
               const BearingFeatureObservation& rhs) {
              return lhs.identity < rhs.identity;
            });
  auto started_at = observations.front().sample_time;
  long double a00 = 0.0L;
  long double a01 = 0.0L;
  long double a11 = 0.0L;
  long double b0 = 0.0L;
  long double b1 = 0.0L;
  for(const auto& observation : observations) {
    started_at = std::min(started_at, observation.sample_time);
    const auto radians =
        static_cast<long double>(observation.bearing_centidegrees) *
        std::numbers::pi_v<long double> / 18'000.0L;
    const auto nx = -std::sin(radians);
    const auto ny = std::cos(radians);
    const auto line_value =
        nx * static_cast<long double>(observation.sensor_x_meters) +
        ny * static_cast<long double>(observation.sensor_y_meters);
    a00 += nx * nx;
    a01 += nx * ny;
    a11 += ny * ny;
    b0 += nx * line_value;
    b1 += ny * line_value;
  }
  const auto trace = a00 + a11;
  const auto discriminant =
      std::sqrt((a00 - a11) * (a00 - a11) + 4.0L * a01 * a01);
  const auto maximum_eigenvalue = (trace + discriminant) / 2.0L;
  const auto minimum_eigenvalue = (trace - discriminant) / 2.0L;
  if(!std::isfinite(maximum_eigenvalue) ||
     !std::isfinite(minimum_eigenvalue) ||
     maximum_eigenvalue <= 0.0L ||
     minimum_eigenvalue <= maximum_eigenvalue * 1.0e-10L) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "Bearing-line fusion geometry is rank deficient"});
  }
  const auto determinant = a00 * a11 - a01 * a01;
  const auto estimated_x = (b0 * a11 - b1 * a01) / determinant;
  const auto estimated_y = (a00 * b1 - a01 * b0) / determinant;
  long double squared_residual_sum = 0.0L;
  for(const auto& observation : observations) {
    const auto radians =
        static_cast<long double>(observation.bearing_centidegrees) *
        std::numbers::pi_v<long double> / 18'000.0L;
    const auto nx = -std::sin(radians);
    const auto ny = std::cos(radians);
    const auto residual =
        nx * (estimated_x - observation.sensor_x_meters) +
        ny * (estimated_y - observation.sensor_y_meters);
    squared_residual_sum += residual * residual;
  }
  if(!std::isfinite(estimated_x) || !std::isfinite(estimated_y) ||
     !std::isfinite(squared_residual_sum)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOutOfRange,
                         "Bearing-line fusion produced a non-finite result"});
  }
  const auto period = contracts::CheckedSubtract(completed_at, started_at);
  if(!period || *period < contracts::SimDuration::Zero()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "Fusion completed before its first observation"});
  }
  const auto center_bearing = GenerateGlobalBearingCentidegrees(
      fusion_center_x_meters,
      fusion_center_y_meters,
      static_cast<double>(estimated_x),
      static_cast<double>(estimated_y));
  if(!center_bearing) return std::unexpected(center_bearing.error());
  if(next_fusion_sequence_ ==
     std::numeric_limits<std::uint64_t>::max()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Fusion sequence exhausted"});
  }
  const auto result = FusionResult{
      next_fusion_sequence_++,
      started_at,
      completed_at,
      *period,
      observations.size(),
      [&observations] {
        std::vector<ObservationIdentity> identities;
        identities.reserve(observations.size());
        for(const auto& observation : observations) {
          identities.push_back(observation.identity);
        }
        return identities;
      }(),
      static_cast<double>(estimated_x),
      static_cast<double>(estimated_y),
      *center_bearing,
      std::sqrt(static_cast<double>(
          squared_residual_sum / observations.size())),
      *period <= maximum_fusion_period_};
  active_observations_.clear();
  return std::optional<FusionResult>{result};
}

}  // namespace ns3_factory::assembly::internal
