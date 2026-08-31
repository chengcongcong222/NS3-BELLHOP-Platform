#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "acoustic_field_asset.hpp"
#include "environment_asset_package.hpp"
#include "environment_asset_repository.hpp"
#include "environment_coordinate_frame.hpp"
#include "import/bellhop_arrival_import_options.hpp"
#include "import/bellhop_ascii_arrival_parser.hpp"
#include "import/bellhop_raw_arrival_bundle.hpp"
#include "import/bellhop_raw_arrival_normalizer.hpp"

namespace ns3_factory::environment::internal {

inline constexpr auto kReferenceShallowWaterV1AssetId =
    "reference-shallow-water-v1";
inline constexpr auto kReferenceShallowWaterV1FormatVersion = 1U;
inline constexpr auto kReferenceShallowWaterV1CellCount = 650U;
inline constexpr auto kReferenceShallowWaterV1PayloadChecksum =
    std::uint64_t{0xfb64e543f9042c52U};

[[nodiscard]] inline auto ReferenceShallowWaterV1Provenance()
    -> contracts::Result<EnvironmentAssetPackageProvenance> {
  return EnvironmentAssetPackageProvenance::Create(
      EnvironmentAssetProducerType::kBellhopRawImport,
      "P0-S5-02",
      "Reference/proxy northwestern South China Sea shallow-water environment; WOA23 April SSP and GEBCO 2020 bathymetry; Bellhop-derived, not field-measured",
      "reference_shallow_water_v1.arr",
      "bellhop-raw-arrival-normalizer-v1");
}

[[nodiscard]] inline auto BuildReferenceShallowWaterV1(
    const std::filesystem::path& asset_root)
    -> contracts::Result<AcousticFieldAsset> {
  using import::BellhopArrivalImportOptions;
  using import::BellhopAsciiArrivalParser;
  using import::BellhopRawArrivalBundle;
  using import::BellhopRawArrivalNormalizer;
  using import::BellhopReceiverRangeUnit;

  auto dataset = BellhopAsciiArrivalParser::ParseFile(
      asset_root / "bellhop" / "reference_shallow_water_v1.arr",
      BellhopArrivalImportOptions{BellhopReceiverRangeUnit::kMeters});
  if(!dataset) return std::unexpected(dataset.error());
  std::vector<import::BellhopRawArrivalDataset> datasets;
  datasets.push_back(std::move(*dataset));
  auto bundle = BellhopRawArrivalBundle::Create(std::move(datasets));
  if(!bundle) return std::unexpected(bundle.error());
  auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!frame) return std::unexpected(frame.error());
  return BellhopRawArrivalNormalizer::Normalize(
      *bundle,
      kReferenceShallowWaterV1FormatVersion,
      "ReferenceShallowWaterV1: WOA23/GEBCO2020 reference proxy; Bellhop-derived; not field-measured",
      *frame);
}

[[nodiscard]] inline auto MatchesReferenceShallowWaterV1Metadata(
    const AcousticFieldPackageMetadata& metadata) noexcept -> bool {
  return metadata.package_format_version == kAcousticFieldPackageVersion &&
         metadata.asset_format_version ==
             kReferenceShallowWaterV1FormatVersion &&
         metadata.cell_count == kReferenceShallowWaterV1CellCount &&
         metadata.frequency_count == 1U &&
         metadata.source_depth_count == 5U &&
         metadata.receiver_depth_count == 5U &&
         metadata.range_count == 26U &&
         metadata.signal_cell_count == 625U &&
         metadata.no_arrival_cell_count == 25U &&
         metadata.payload_bytes == 231'056U &&
         metadata.payload_checksum ==
             kReferenceShallowWaterV1PayloadChecksum &&
         metadata.provenance.producer_type() ==
             EnvironmentAssetProducerType::kBellhopRawImport &&
         metadata.provenance.created_by_build_version() == "P0-S5-02" &&
         metadata.provenance.source_description() ==
             "Reference/proxy northwestern South China Sea shallow-water environment; WOA23 April SSP and GEBCO 2020 bathymetry; Bellhop-derived, not field-measured" &&
         metadata.provenance.raw_source_logical_name() ==
             "reference_shallow_water_v1.arr" &&
         metadata.provenance.normalization_policy_version() ==
             "bellhop-raw-arrival-normalizer-v1" &&
         metadata.coordinate_frame.surface_z_meters() == 0.0 &&
         metadata.coordinate_frame.vertical_direction() ==
             VerticalAxisDirection::kPositiveUp;
}

}  // namespace ns3_factory::environment::internal
