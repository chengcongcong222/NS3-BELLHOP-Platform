#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include "acoustic_field_asset.hpp"
#include "bellhop_environment_builder.hpp"
#include "environment_coordinate_frame.hpp"
#include "environment_profile.hpp"
#include "import/bellhop_arrival_import_options.hpp"
#include "import/bellhop_ascii_arrival_parser.hpp"
#include "import/bellhop_raw_arrival_bundle.hpp"
#include "import/bellhop_raw_arrival_normalizer.hpp"

namespace ns3_factory::environment {

struct BellhopFrequencyGenerationPlan final {
  std::string case_name;
  double frequency_hz;
  BellhopRunConfiguration configuration;
};

struct EnvironmentAssetGenerationRequest final {
  std::uint32_t asset_format_version;
  std::string asset_provenance;
  internal::EnvironmentCoordinateFrame coordinate_frame;
  internal::import::BellhopArrivalImportOptions arrival_import_options;
  GeographicRegion geographic_region;
  EnvironmentSourceQuery source_query;
  double source_depth_meters;
  std::vector<double> receiver_depths_meters;
  double maximum_range_meters;
  double maximum_water_depth_meters;
  SoundSpeedCoveragePolicy sound_speed_coverage_policy;
  std::vector<double> surface_ranges_meters;
  std::vector<double> surface_elevations_meters;
  std::vector<BellhopFrequencyGenerationPlan> frequency_plans;
};

// The only Bellhop build path. Runtime providers consume the normalized
// AcousticFieldAsset and cannot reach this builder, process runner, or parser.
class OfflineBellhopAssetPipeline final {
 public:
  OfflineBellhopAssetPipeline(
      const IBellhopEnvironmentBuilder& environment_builder,
      const IBellhopRunner& bellhop_runner)
      : environment_builder_(environment_builder),
        bellhop_runner_(bellhop_runner) {}

  [[nodiscard]] auto Generate(
      const EnvironmentAssetGenerationRequest& request,
      SoundSpeedProfile sound_speed_profile,
      BathymetryProfile bathymetry_profile) const
      -> contracts::Result<internal::AcousticFieldAsset> {
    const auto request_status = ValidateRequest(request);
    if(!request_status) {
      return std::unexpected(request_status.error());
    }
    auto prepared_sound_speed = PrepareSoundSpeedProfileCoverage(
        sound_speed_profile,
        request.maximum_water_depth_meters,
        request.sound_speed_coverage_policy);
    if(!prepared_sound_speed) {
      return std::unexpected(prepared_sound_speed.error());
    }

    std::vector<internal::import::BellhopRawArrivalDataset> datasets;
    datasets.reserve(request.frequency_plans.size());
    std::string provenance = request.asset_provenance;
    for(const auto& plan : request.frequency_plans) {
      BellhopEnvironmentBuildRequest build_request{
          plan.frequency_hz,
          request.source_depth_meters,
          request.receiver_depths_meters,
          request.maximum_range_meters,
          request.maximum_water_depth_meters,
          *prepared_sound_speed,
          bathymetry_profile,
          request.surface_ranges_meters,
          request.surface_elevations_meters,
          plan.configuration};
      auto input_files = environment_builder_.get().Build(build_request);
      if(!input_files) {
        return std::unexpected(input_files.error());
      }
      auto run_result = bellhop_runner_.get().Run(plan.case_name, *input_files);
      if(!run_result) {
        return std::unexpected(run_result.error());
      }
      if(run_result->runner_identity.empty()) {
        return std::unexpected(
            contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                             "Bellhop runner must identify the offline "
                             "generator"});
      }

      std::istringstream arrivals{run_result->arrivals_ascii};
      auto parsed = internal::import::BellhopAsciiArrivalParser::Parse(
          arrivals, request.arrival_import_options);
      if(!parsed) {
        return std::unexpected(parsed.error());
      }
      const auto field_status = ValidateGeneratedDataset(request,
                                                         plan,
                                                         *parsed);
      if(!field_status) {
        return std::unexpected(field_status.error());
      }
      provenance += "|runner=" + run_result->runner_identity;
      datasets.push_back(std::move(*parsed));
    }

    auto bundle = internal::import::BellhopRawArrivalBundle::Create(
        std::move(datasets));
    if(!bundle) {
      return std::unexpected(bundle.error());
    }
    return internal::import::BellhopRawArrivalNormalizer::Normalize(
        *bundle,
        request.asset_format_version,
        std::move(provenance),
        request.coordinate_frame);
  }

 private:
  [[nodiscard]] static auto ValidateRequest(
      const EnvironmentAssetGenerationRequest& request)
      -> contracts::Status {
    if(request.asset_format_version == 0U ||
       request.asset_provenance.empty() ||
       request.frequency_plans.empty()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Environment generation requires an asset format, "
                           "provenance, and frequency plans"});
    }
    if(!request.geographic_region.Contains(
           request.source_query.latitude_degrees(),
           request.source_query.longitude_degrees())) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Environment asset region does not contain the "
                           "source query"});
    }
    for(std::size_t index = 0U;
        index < request.frequency_plans.size();
        ++index) {
      const auto& plan = request.frequency_plans[index];
      if(plan.case_name.empty() ||
         (index != 0U &&
          request.frequency_plans[index - 1U].frequency_hz >=
              plan.frequency_hz)) {
        return std::unexpected(
            contracts::Error{contracts::ErrorCode::kInvalidArgument,
                             "Bellhop plans require non-empty case names and "
                             "strictly increasing frequencies"});
      }
      const auto duplicate_case = std::find_if(
          request.frequency_plans.begin(),
          request.frequency_plans.begin() +
              static_cast<std::ptrdiff_t>(index),
          [&](const auto& previous) {
            return previous.case_name == plan.case_name;
          });
      if(duplicate_case != request.frequency_plans.begin() +
                              static_cast<std::ptrdiff_t>(index)) {
        return std::unexpected(
            contracts::Error{contracts::ErrorCode::kAlreadyExists,
                             "Bellhop generation case names must be unique"});
      }
    }
    return {};
  }

  [[nodiscard]] static auto ValidateGeneratedDataset(
      const EnvironmentAssetGenerationRequest& request,
      const BellhopFrequencyGenerationPlan& plan,
      const internal::import::BellhopRawArrivalDataset& parsed)
      -> contracts::Status {
    if(parsed.frequency_hz() != plan.frequency_hz) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Bellhop result frequency does not match its "
                           "generation plan"});
    }
    const auto tx_depths = parsed.source_depths_m();
    const auto rx_depths = parsed.receiver_depths_m();
    const auto ranges = parsed.receiver_ranges_m();
    if(tx_depths.size() != 1U ||
       tx_depths.front() != request.source_depth_meters ||
       !std::ranges::equal(rx_depths, request.receiver_depths_meters) ||
       ranges.size() != plan.configuration.receiver_range_count ||
       ranges.front() != 0.0 ||
       ranges.back() != request.maximum_range_meters) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Bellhop result axes do not match the explicit "
                           "generation plan"});
    }
    return {};
  }

  std::reference_wrapper<const IBellhopEnvironmentBuilder>
      environment_builder_;
  std::reference_wrapper<const IBellhopRunner> bellhop_runner_;
};

class OfflineEnvironmentAssetPipeline final {
 public:
  OfflineEnvironmentAssetPipeline(
      const IEnvironmentSource& environment_source,
      const IBathymetrySource& bathymetry_source,
      const ISoundSpeedModel& sound_speed_model,
      const IBellhopEnvironmentBuilder& environment_builder,
      const IBellhopRunner& bellhop_runner)
      : environment_source_(environment_source),
        bathymetry_source_(bathymetry_source),
        sound_speed_model_(sound_speed_model),
        bellhop_pipeline_(environment_builder, bellhop_runner) {}

  [[nodiscard]] auto Generate(
      const EnvironmentAssetGenerationRequest& request) const
      -> contracts::Result<internal::AcousticFieldAsset> {
    auto hydrographic_profile =
        environment_source_.get().LoadHydrographicProfile(
            request.source_query);
    if(!hydrographic_profile) {
      return std::unexpected(hydrographic_profile.error());
    }
    auto sound_speed_profile =
        sound_speed_model_.get().Compute(*hydrographic_profile);
    if(!sound_speed_profile) {
      return std::unexpected(sound_speed_profile.error());
    }
    auto bathymetry_profile =
        bathymetry_source_.get().LoadBathymetryProfile(request.source_query);
    if(!bathymetry_profile) {
      return std::unexpected(bathymetry_profile.error());
    }
    return bellhop_pipeline_.Generate(request,
                                      std::move(*sound_speed_profile),
                                      std::move(*bathymetry_profile));
  }

 private:
  std::reference_wrapper<const IEnvironmentSource> environment_source_;
  std::reference_wrapper<const IBathymetrySource> bathymetry_source_;
  std::reference_wrapper<const ISoundSpeedModel> sound_speed_model_;
  OfflineBellhopAssetPipeline bellhop_pipeline_;
};

}  // namespace ns3_factory::environment
