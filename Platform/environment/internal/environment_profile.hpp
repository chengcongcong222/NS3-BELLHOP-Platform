#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

namespace ns3_factory::environment {

// Explicit source coverage used by offline environment import/build steps.
class GeographicRegion final {
 public:
  [[nodiscard]] static auto Create(double south_latitude_degrees,
                                   double west_longitude_degrees,
                                   double north_latitude_degrees,
                                   double east_longitude_degrees)
      -> contracts::Result<GeographicRegion> {
    if(!std::isfinite(south_latitude_degrees) ||
       !std::isfinite(west_longitude_degrees) ||
       !std::isfinite(north_latitude_degrees) ||
       !std::isfinite(east_longitude_degrees)) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Geographic region coordinates must be finite"});
    }
    if(south_latitude_degrees < -90.0 || north_latitude_degrees > 90.0 ||
       south_latitude_degrees > north_latitude_degrees ||
       west_longitude_degrees < -180.0 ||
       west_longitude_degrees > 180.0 ||
       east_longitude_degrees < -180.0 ||
       east_longitude_degrees > 180.0) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kOutOfRange,
          "Geographic region coordinates are outside valid latitude/longitude bounds"});
    }
    return GeographicRegion{south_latitude_degrees,
                            west_longitude_degrees,
                            north_latitude_degrees,
                            east_longitude_degrees};
  }

  [[nodiscard]] constexpr auto south_latitude_degrees() const noexcept
      -> double {
    return south_latitude_degrees_;
  }

  [[nodiscard]] constexpr auto west_longitude_degrees() const noexcept
      -> double {
    return west_longitude_degrees_;
  }

  [[nodiscard]] constexpr auto north_latitude_degrees() const noexcept
      -> double {
    return north_latitude_degrees_;
  }

  [[nodiscard]] constexpr auto east_longitude_degrees() const noexcept
      -> double {
    return east_longitude_degrees_;
  }

  [[nodiscard]] auto Contains(double latitude_degrees,
                              double longitude_degrees) const noexcept
      -> bool {
    if(!std::isfinite(latitude_degrees) ||
       !std::isfinite(longitude_degrees) || latitude_degrees < -90.0 ||
       latitude_degrees > 90.0 || longitude_degrees < -180.0 ||
       longitude_degrees > 180.0) {
      return false;
    }
    const auto longitude_is_covered =
        west_longitude_degrees_ <= east_longitude_degrees_
            ? longitude_degrees >= west_longitude_degrees_ &&
                  longitude_degrees <= east_longitude_degrees_
            : longitude_degrees >= west_longitude_degrees_ ||
                  longitude_degrees <= east_longitude_degrees_;
    return latitude_degrees >= south_latitude_degrees_ &&
           latitude_degrees <= north_latitude_degrees_ &&
           longitude_is_covered;
  }

  auto operator==(const GeographicRegion&) const -> bool = default;

 private:
  constexpr GeographicRegion(double south_latitude_degrees,
                             double west_longitude_degrees,
                             double north_latitude_degrees,
                             double east_longitude_degrees) noexcept
      : south_latitude_degrees_(south_latitude_degrees),
        west_longitude_degrees_(west_longitude_degrees),
        north_latitude_degrees_(north_latitude_degrees),
        east_longitude_degrees_(east_longitude_degrees) {}

  double south_latitude_degrees_;
  double west_longitude_degrees_;
  double north_latitude_degrees_;
  double east_longitude_degrees_;
};

class EnvironmentSourceQuery final {
 public:
  [[nodiscard]] static auto Create(double latitude_degrees,
                                   double longitude_degrees,
                                   std::string time_descriptor)
      -> contracts::Result<EnvironmentSourceQuery> {
    if(!std::isfinite(latitude_degrees) ||
       !std::isfinite(longitude_degrees)) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Environment source query coordinates must be finite"});
    }
    if(latitude_degrees < -90.0 || latitude_degrees > 90.0 ||
       longitude_degrees < -180.0 || longitude_degrees > 180.0) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kOutOfRange,
          "Environment source query coordinates are outside valid bounds"});
    }
    if(time_descriptor.empty()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Environment source query requires an explicit time descriptor"});
    }
    return EnvironmentSourceQuery{latitude_degrees,
                                  longitude_degrees,
                                  std::move(time_descriptor)};
  }

  [[nodiscard]] constexpr auto latitude_degrees() const noexcept -> double {
    return latitude_degrees_;
  }

  [[nodiscard]] constexpr auto longitude_degrees() const noexcept -> double {
    return longitude_degrees_;
  }

  [[nodiscard]] auto time_descriptor() const noexcept -> const std::string& {
    return time_descriptor_;
  }

 private:
  EnvironmentSourceQuery(double latitude_degrees,
                         double longitude_degrees,
                         std::string time_descriptor)
      : latitude_degrees_(latitude_degrees),
        longitude_degrees_(longitude_degrees),
        time_descriptor_(std::move(time_descriptor)) {}

  double latitude_degrees_;
  double longitude_degrees_;
  std::string time_descriptor_;
};

struct HydrographicSample final {
  double depth_meters;
  double temperature_celsius;
  double salinity_psu;

  auto operator==(const HydrographicSample&) const -> bool = default;
};

class HydrographicProfile final {
 public:
  [[nodiscard]] static auto Create(std::vector<HydrographicSample> samples)
      -> contracts::Result<HydrographicProfile> {
    if(samples.size() < 2) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Hydrographic profile requires at least two samples"});
    }
    for(std::size_t index = 0; index < samples.size(); ++index) {
      const auto& sample = samples[index];
      if(!std::isfinite(sample.depth_meters) ||
         !std::isfinite(sample.temperature_celsius) ||
         !std::isfinite(sample.salinity_psu) || sample.depth_meters < 0.0 ||
         sample.salinity_psu < 0.0 ||
         (index > 0 &&
          samples[index - 1].depth_meters >= sample.depth_meters)) {
        return std::unexpected(contracts::Error{
            contracts::ErrorCode::kInvalidArgument,
            "Hydrographic samples require increasing non-negative depths and finite physical values"});
      }
    }
    return HydrographicProfile{std::move(samples)};
  }

  [[nodiscard]] auto samples() const noexcept
      -> std::span<const HydrographicSample> {
    return samples_;
  }

 private:
  explicit HydrographicProfile(std::vector<HydrographicSample> samples)
      : samples_(std::move(samples)) {}

  std::vector<HydrographicSample> samples_;
};

struct SoundSpeedSample final {
  double depth_meters;
  double speed_meters_per_second;

  auto operator==(const SoundSpeedSample&) const -> bool = default;
};

class SoundSpeedProfile final {
 public:
  [[nodiscard]] static auto Create(std::vector<SoundSpeedSample> samples)
      -> contracts::Result<SoundSpeedProfile> {
    if(samples.size() < 2) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Sound-speed profile requires at least two samples"});
    }
    for(std::size_t index = 0; index < samples.size(); ++index) {
      const auto& sample = samples[index];
      if(!std::isfinite(sample.depth_meters) ||
         !std::isfinite(sample.speed_meters_per_second) ||
         sample.depth_meters < 0.0 || sample.speed_meters_per_second <= 0.0 ||
         (index > 0 &&
          samples[index - 1].depth_meters >= sample.depth_meters)) {
        return std::unexpected(contracts::Error{
            contracts::ErrorCode::kInvalidArgument,
            "Sound-speed samples require increasing non-negative depths and finite positive speeds"});
      }
    }
    return SoundSpeedProfile{std::move(samples)};
  }

  [[nodiscard]] auto samples() const noexcept
      -> std::span<const SoundSpeedSample> {
    return samples_;
  }

  [[nodiscard]] auto SpeedAt(double depth_meters) const
      -> contracts::Result<double> {
    if(!std::isfinite(depth_meters)) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Sound-speed query depth must be finite"});
    }
    if(depth_meters < samples_.front().depth_meters ||
       depth_meters > samples_.back().depth_meters) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kOutOfRange,
          "Sound-speed query is outside profile coverage"});
    }
    const auto upper = std::lower_bound(
        samples_.begin(),
        samples_.end(),
        depth_meters,
        [](const auto& sample, double value) {
          return sample.depth_meters < value;
        });
    if(upper == samples_.begin()) {
      return upper->speed_meters_per_second;
    }
    if(upper == samples_.end()) {
      return samples_.back().speed_meters_per_second;
    }
    if(upper->depth_meters == depth_meters) {
      return upper->speed_meters_per_second;
    }
    const auto lower = upper - 1;
    const auto weight = (depth_meters - lower->depth_meters) /
                        (upper->depth_meters - lower->depth_meters);
    return lower->speed_meters_per_second * (1.0 - weight) +
           upper->speed_meters_per_second * weight;
  }

 private:
  explicit SoundSpeedProfile(std::vector<SoundSpeedSample> samples)
      : samples_(std::move(samples)) {}

  std::vector<SoundSpeedSample> samples_;
};

enum class SoundSpeedCoveragePolicy {
  kRequireExact,
  kHoldLastValueToMaximumDepth,
};

[[nodiscard]] inline auto PrepareSoundSpeedProfileCoverage(
    const SoundSpeedProfile& profile,
    double maximum_depth_meters,
    SoundSpeedCoveragePolicy policy)
    -> contracts::Result<SoundSpeedProfile> {
  if(!std::isfinite(maximum_depth_meters) || maximum_depth_meters <= 0.0) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "Sound-speed coverage depth must be finite and positive"});
  }
  const auto last_depth = profile.samples().back().depth_meters;
  if(last_depth == maximum_depth_meters) {
    return profile;
  }
  if(last_depth > maximum_depth_meters) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "Sound-speed profile exceeds the requested maximum depth; clipping requires a separate explicit policy"});
  }
  if(policy != SoundSpeedCoveragePolicy::kHoldLastValueToMaximumDepth) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "Sound-speed profile does not cover maximum depth and extension was not authorized"});
  }
  std::vector<SoundSpeedSample> samples{profile.samples().begin(),
                                        profile.samples().end()};
  samples.push_back(
      {maximum_depth_meters, samples.back().speed_meters_per_second});
  return SoundSpeedProfile::Create(std::move(samples));
}

class IEnvironmentSource {
 public:
  virtual ~IEnvironmentSource() = default;

  [[nodiscard]] virtual auto LoadHydrographicProfile(
      const EnvironmentSourceQuery& query) const
      -> contracts::Result<HydrographicProfile> = 0;
};

// Manual input is a complete offline source. WOA and Argo adapters implement
// IEnvironmentSource outside the runtime query path and return the same value
// type, so the pure sound-speed model does not depend on network clients.
class ManualEnvironmentSource final : public IEnvironmentSource {
 public:
  [[nodiscard]] static auto Create(GeographicRegion coverage,
                                   std::string time_descriptor,
                                   HydrographicProfile profile)
      -> contracts::Result<ManualEnvironmentSource> {
    if(time_descriptor.empty()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Manual environment source requires a time descriptor"});
    }
    return ManualEnvironmentSource{coverage,
                                   std::move(time_descriptor),
                                   std::move(profile)};
  }

  [[nodiscard]] auto LoadHydrographicProfile(
      const EnvironmentSourceQuery& query) const
      -> contracts::Result<HydrographicProfile> override {
    if(!coverage_.Contains(query.latitude_degrees(),
                           query.longitude_degrees())) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kOutOfRange,
          "Environment source query is outside manual profile coverage"});
    }
    if(query.time_descriptor() != time_descriptor_) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kNotFound,
          "Environment source time descriptor was not found"});
    }
    return profile_;
  }

 private:
  ManualEnvironmentSource(GeographicRegion coverage,
                          std::string time_descriptor,
                          HydrographicProfile profile)
      : coverage_(coverage),
        time_descriptor_(std::move(time_descriptor)),
        profile_(std::move(profile)) {}

  GeographicRegion coverage_;
  std::string time_descriptor_;
  HydrographicProfile profile_;
};

class ISoundSpeedModel {
 public:
  virtual ~ISoundSpeedModel() = default;

  [[nodiscard]] virtual auto Compute(
      const HydrographicProfile& hydrographic_profile) const
      -> contracts::Result<SoundSpeedProfile> = 0;
};

// Pure migration of the legacy Mackenzie calculation. It contains no file,
// HTTP, clock, or process access and therefore has deterministic test inputs.
class MackenzieSoundSpeedModel final : public ISoundSpeedModel {
 public:
  [[nodiscard]] auto Compute(
      const HydrographicProfile& hydrographic_profile) const
      -> contracts::Result<SoundSpeedProfile> override {
    std::vector<SoundSpeedSample> sound_speed_samples;
    sound_speed_samples.reserve(hydrographic_profile.samples().size());
    for(const auto& sample : hydrographic_profile.samples()) {
      const auto temperature = sample.temperature_celsius;
      const auto salinity_delta = sample.salinity_psu - 35.0;
      const auto depth = sample.depth_meters;
      const auto speed =
          1448.96 + 4.591 * temperature -
          5.304e-2 * temperature * temperature +
          2.374e-4 * temperature * temperature * temperature +
          1.340 * salinity_delta + 1.630e-2 * depth +
          1.675e-7 * depth * depth -
          1.025e-2 * temperature * salinity_delta -
          7.139e-13 * temperature * depth * depth * depth;
      sound_speed_samples.push_back({depth, speed});
    }
    return SoundSpeedProfile::Create(std::move(sound_speed_samples));
  }
};

}  // namespace ns3_factory::environment
