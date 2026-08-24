#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <locale>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include "bathymetry_profile.hpp"
#include "environment_profile.hpp"

namespace ns3_factory::environment {

struct BellhopRunConfiguration final {
  std::string title;
  std::string top_boundary_option;
  std::string bottom_boundary_option;
  double bottom_roughness_meters;
  double bottom_sound_speed_meters_per_second;
  double bottom_density_grams_per_cubic_centimeter;
  double bottom_attenuation_db_per_wavelength;
  std::size_t receiver_range_count;
  std::size_t beam_count;
  double minimum_beam_angle_degrees;
  double maximum_beam_angle_degrees;
  double ray_step_meters;

  auto operator==(const BellhopRunConfiguration&) const -> bool = default;
};

[[nodiscard]] inline auto ValidateBellhopRunConfiguration(
    const BellhopRunConfiguration& configuration) -> contracts::Status {
  const auto safe_ascii = [](std::string_view value) {
    if(value.empty()) {
      return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
      const auto byte = static_cast<unsigned char>(character);
      return byte >= 0x20 && byte <= 0x7e && character != '\'';
    });
  };
  if(!safe_ascii(configuration.title) ||
     !safe_ascii(configuration.top_boundary_option) ||
     !safe_ascii(configuration.bottom_boundary_option)) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "Bellhop title and boundary options must be non-empty safe ASCII fields"});
  }
  if(!std::isfinite(configuration.bottom_roughness_meters) ||
     !std::isfinite(
         configuration.bottom_sound_speed_meters_per_second) ||
     !std::isfinite(
         configuration.bottom_density_grams_per_cubic_centimeter) ||
     !std::isfinite(configuration.bottom_attenuation_db_per_wavelength) ||
     !std::isfinite(configuration.minimum_beam_angle_degrees) ||
     !std::isfinite(configuration.maximum_beam_angle_degrees) ||
     !std::isfinite(configuration.ray_step_meters)) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "Bellhop configuration physical values must be finite"});
  }
  if(configuration.bottom_roughness_meters < 0.0 ||
     configuration.bottom_sound_speed_meters_per_second <= 0.0 ||
     configuration.bottom_density_grams_per_cubic_centimeter <= 0.0 ||
     configuration.bottom_attenuation_db_per_wavelength < 0.0 ||
     configuration.receiver_range_count < 2 ||
     configuration.minimum_beam_angle_degrees >=
         configuration.maximum_beam_angle_degrees ||
     configuration.ray_step_meters < 0.0) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kOutOfRange,
        "Bellhop configuration is outside supported physical bounds"});
  }
  return {};
}

struct BellhopEnvironmentBuildRequest final {
  double frequency_hz;
  double source_depth_meters;
  std::vector<double> receiver_depths_meters;
  double maximum_range_meters;
  double maximum_water_depth_meters;
  SoundSpeedProfile sound_speed_profile;
  BathymetryProfile bathymetry_profile;
  std::vector<double> surface_ranges_meters;
  std::vector<double> surface_elevations_meters;
  BellhopRunConfiguration configuration;
};

struct BellhopInputFiles final {
  std::string environment_ascii;
  std::string bathymetry_ascii;
  std::string surface_ascii;

  auto operator==(const BellhopInputFiles&) const -> bool = default;
};

class IBellhopEnvironmentBuilder {
 public:
  virtual ~IBellhopEnvironmentBuilder() = default;

  [[nodiscard]] virtual auto Build(
      const BellhopEnvironmentBuildRequest& request) const
      -> contracts::Result<BellhopInputFiles> = 0;
};

struct BellhopRunResult final {
  std::string arrivals_ascii;
  std::string runner_identity;
};

// Process execution belongs to the offline asset-generation application. The
// Environment core defines the seam but never launches Bellhop from a runtime
// channel query.
class IBellhopRunner {
 public:
  virtual ~IBellhopRunner() = default;

  [[nodiscard]] virtual auto Run(std::string_view case_name,
                                 const BellhopInputFiles& input_files) const
      -> contracts::Result<BellhopRunResult> = 0;
};

class BellhopAsciiEnvironmentBuilder final
    : public IBellhopEnvironmentBuilder {
 public:
  [[nodiscard]] auto Build(
      const BellhopEnvironmentBuildRequest& request) const
      -> contracts::Result<BellhopInputFiles> override {
    const auto request_status = Validate(request);
    if(!request_status) {
      return std::unexpected(request_status.error());
    }

    std::ostringstream environment;
    environment.imbue(std::locale::classic());
    environment << std::fixed;
    environment << '\'' << request.configuration.title << "'\n";
    environment << std::setprecision(1) << request.frequency_hz << '\n';
    environment << "1\n";
    environment << '\'' << request.configuration.top_boundary_option
                << "'\n";
    environment << "  " << request.sound_speed_profile.samples().size()
                << "  0.0  " << std::setprecision(1)
                << request.maximum_water_depth_meters << '\n';
    for(const auto& sample : request.sound_speed_profile.samples()) {
      environment << "  " << std::setprecision(1) << sample.depth_meters
                  << "  " << std::setprecision(2)
                  << sample.speed_meters_per_second << "  /\n";
    }
    environment << '\'' << request.configuration.bottom_boundary_option
                << "'  " << std::setprecision(1)
                << request.configuration.bottom_roughness_meters << '\n';
    environment
        << "  " << std::setprecision(1)
        << request.maximum_water_depth_meters << "  "
        << request.configuration.bottom_sound_speed_meters_per_second << "  "
        << std::setprecision(2)
        << request.configuration.bottom_density_grams_per_cubic_centimeter
        << "  "
        << request.configuration.bottom_attenuation_db_per_wavelength
        << "  /\n";
    environment << "  1\n";
    environment << "  " << std::setprecision(1)
                << request.source_depth_meters << "  /\n";
    environment << "  " << request.receiver_depths_meters.size() << '\n';
    environment << "  ";
    for(std::size_t index = 0;
        index < request.receiver_depths_meters.size();
        ++index) {
      if(index > 0) {
        environment << "  ";
      }
      environment << std::setprecision(1)
                  << request.receiver_depths_meters[index];
    }
    environment << "  /\n";
    environment << "  " << request.configuration.receiver_range_count
                << '\n';
    environment << "  0.0  " << std::setprecision(4)
                << request.maximum_range_meters / 1000.0 << "  /\n";
    environment << "'A'\n";
    environment << "  " << request.configuration.beam_count << '\n';
    environment << "  " << std::setprecision(1)
                << request.configuration.minimum_beam_angle_degrees << "  "
                << request.configuration.maximum_beam_angle_degrees
                << "  /\n";
    environment << "  " << std::setprecision(1)
                << request.configuration.ray_step_meters << "  "
                << request.maximum_water_depth_meters << "  "
                << std::setprecision(4)
                << request.maximum_range_meters / 1000.0 << '\n';

    std::ostringstream bathymetry;
    bathymetry.imbue(std::locale::classic());
    bathymetry << std::fixed << "'L'\n";
    bathymetry << "  " << request.bathymetry_profile.ranges_meters().size()
               << '\n';
    for(std::size_t index = 0;
        index < request.bathymetry_profile.ranges_meters().size();
        ++index) {
      bathymetry << "  " << std::setprecision(6)
                 << request.bathymetry_profile.ranges_meters()[index] /
                        1000.0
                 << "  " << std::setprecision(2)
                 << request.bathymetry_profile.depths_meters()[index]
                 << '\n';
    }

    std::ostringstream surface;
    surface.imbue(std::locale::classic());
    surface << std::fixed << "'L'\n";
    surface << "  " << request.surface_ranges_meters.size() << '\n';
    for(std::size_t index = 0; index < request.surface_ranges_meters.size();
        ++index) {
      surface << "  " << std::setprecision(6)
              << request.surface_ranges_meters[index] / 1000.0 << "  "
              << std::setprecision(2)
              << request.surface_elevations_meters[index] << '\n';
    }

    return BellhopInputFiles{environment.str(),
                             bathymetry.str(),
                             surface.str()};
  }

 private:
  [[nodiscard]] static auto Validate(
      const BellhopEnvironmentBuildRequest& request) -> contracts::Status {
    const auto configuration_status =
        ValidateBellhopRunConfiguration(request.configuration);
    if(!configuration_status) {
      return configuration_status;
    }
    if(!std::isfinite(request.frequency_hz) ||
       !std::isfinite(request.source_depth_meters) ||
       !std::isfinite(request.maximum_range_meters) ||
       !std::isfinite(request.maximum_water_depth_meters)) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Bellhop build request physical values must be finite"});
    }
    if(request.frequency_hz <= 0.0 || request.source_depth_meters < 0.0 ||
       request.maximum_range_meters <= 0.0 ||
       request.maximum_water_depth_meters <= 0.0 ||
       request.source_depth_meters > request.maximum_water_depth_meters) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kOutOfRange,
          "Bellhop build request is outside supported physical bounds"});
    }
    if(request.receiver_depths_meters.empty()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Bellhop build request requires receiver depths"});
    }
    for(std::size_t index = 0;
        index < request.receiver_depths_meters.size();
        ++index) {
      const auto depth = request.receiver_depths_meters[index];
      if(!std::isfinite(depth) || depth < 0.0 ||
         depth > request.maximum_water_depth_meters ||
         (index > 0 && request.receiver_depths_meters[index - 1] >= depth)) {
        return std::unexpected(contracts::Error{
            contracts::ErrorCode::kInvalidArgument,
            "Bellhop receiver depths must be finite, covered, and strictly increasing"});
      }
    }

    const auto sound_speed_samples = request.sound_speed_profile.samples();
    if(sound_speed_samples.front().depth_meters != 0.0 ||
       sound_speed_samples.back().depth_meters !=
           request.maximum_water_depth_meters) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kFailedPrecondition,
          "Bellhop sound-speed profile must explicitly span zero through maximum water depth"});
    }
    if(request.bathymetry_profile.ranges_meters().front() != 0.0 ||
       request.bathymetry_profile.ranges_meters().back() <
           request.maximum_range_meters) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kFailedPrecondition,
          "Bellhop bathymetry must explicitly cover the requested range"});
    }
    for(const auto depth : request.bathymetry_profile.depths_meters()) {
      if(depth > request.maximum_water_depth_meters) {
        return std::unexpected(contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Bellhop maximum water depth must cover all bathymetry samples"});
      }
    }

    if(request.surface_ranges_meters.size() < 2 ||
       request.surface_ranges_meters.size() !=
           request.surface_elevations_meters.size()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Bellhop surface requires at least two matching range/elevation values"});
    }
    for(std::size_t index = 0; index < request.surface_ranges_meters.size();
        ++index) {
      if(!std::isfinite(request.surface_ranges_meters[index]) ||
         !std::isfinite(request.surface_elevations_meters[index]) ||
         request.surface_ranges_meters[index] < 0.0 ||
         (index > 0 && request.surface_ranges_meters[index - 1] >=
                           request.surface_ranges_meters[index])) {
        return std::unexpected(contracts::Error{
            contracts::ErrorCode::kInvalidArgument,
            "Bellhop surface ranges must increase and all surface values must be finite"});
      }
    }
    if(request.surface_ranges_meters.front() != 0.0 ||
       request.surface_ranges_meters.back() < request.maximum_range_meters) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kFailedPrecondition,
          "Bellhop surface must explicitly cover the requested range"});
    }
    return {};
  }
};

}  // namespace ns3_factory::environment
