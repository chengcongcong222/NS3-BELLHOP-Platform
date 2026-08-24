#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "acoustic_environment_asset.hpp"
#include "environment_coordinate_frame.hpp"
#include "import/bellhop_arrival_import_options.hpp"
#include "import/bellhop_ascii_arrival_parser.hpp"
#include "import/bellhop_raw_arrival_bundle.hpp"
#include "import/bellhop_raw_arrival_normalizer.hpp"

namespace ns3_factory::environment {

struct LegacyBellhopNativeAssetRequest final {
  std::string asset_id;
  std::uint32_t asset_version;
  std::uint32_t atlas_format_version;
  GeographicRegion region;
  std::string valid_time;
  SoundSpeedProfile sound_speed_profile;
  BathymetryProfile bathymetry_profile;
  std::vector<internal::BellhopFrequencyConfiguration>
      bellhop_configurations;
  internal::AssetProvenance provenance;
  internal::EnvironmentCoordinateFrame coordinate_frame;
  internal::import::BellhopArrivalImportOptions arrival_import_options;
  std::vector<std::filesystem::path> arrival_files;
  std::uintmax_t maximum_arrival_file_bytes;
};

// Imports already-computed legacy Bellhop native ARR files. It never starts
// Bellhop, downloads data, invents missing cells, or silently changes units.
class LegacyEnvironmentAssetLoader final {
 public:
  [[nodiscard]] static auto ImportBellhopNative(
      LegacyBellhopNativeAssetRequest request)
      -> contracts::Result<internal::AcousticEnvironmentAsset> {
    if(request.arrival_files.empty() ||
       request.maximum_arrival_file_bytes == 0U ||
       request.arrival_files.size() !=
           request.bellhop_configurations.size()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Legacy Bellhop import requires size-limited ARR files matching the configurations"});
    }

    std::vector<internal::import::BellhopRawArrivalDataset> datasets;
    datasets.reserve(request.arrival_files.size());
    for(std::size_t index = 0U; index < request.arrival_files.size();
        ++index) {
      std::error_code error;
      const auto canonical_path =
          std::filesystem::weakly_canonical(request.arrival_files[index],
                                            error);
      if(error ||
         !std::filesystem::is_regular_file(canonical_path, error) || error) {
        return std::unexpected(contracts::Error{
            contracts::ErrorCode::kNotFound,
            "Legacy Bellhop ARR source must be an existing regular file"});
      }
      const auto file_size = std::filesystem::file_size(canonical_path,
                                                        error);
      if(error) {
        return std::unexpected(contracts::Error{
            contracts::ErrorCode::kUnavailable,
            "Legacy Bellhop ARR source size could not be read"});
      }
      if(file_size > request.maximum_arrival_file_bytes) {
        return std::unexpected(contracts::Error{
            contracts::ErrorCode::kOutOfRange,
            "Legacy Bellhop ARR source exceeds the configured size limit"});
      }

      auto dataset =
          internal::import::BellhopAsciiArrivalParser::ParseFile(
              canonical_path, request.arrival_import_options);
      if(!dataset) {
        return std::unexpected(dataset.error());
      }
      if(dataset->frequency_hz() !=
         request.bellhop_configurations[index].frequency_hz) {
        return std::unexpected(contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Legacy Bellhop ARR frequency does not match its configuration"});
      }
      datasets.push_back(std::move(*dataset));
    }

    auto bundle = internal::import::BellhopRawArrivalBundle::Create(
        std::move(datasets));
    if(!bundle) {
      return std::unexpected(bundle.error());
    }
    auto atlas = internal::import::BellhopRawArrivalNormalizer::Normalize(
        *bundle,
        request.atlas_format_version,
        request.provenance.Describe(),
        request.coordinate_frame);
    if(!atlas) {
      return std::unexpected(atlas.error());
    }

    return internal::AcousticEnvironmentAsset::Create(
        std::move(request.asset_id),
        request.asset_version,
        request.region,
        std::move(request.valid_time),
        std::move(request.sound_speed_profile),
        std::move(request.bathymetry_profile),
        std::move(request.bellhop_configurations),
        std::move(request.provenance),
        std::make_shared<const internal::AcousticFieldAtlas>(
            std::move(*atlas)));
  }
};

}  // namespace ns3_factory::environment
