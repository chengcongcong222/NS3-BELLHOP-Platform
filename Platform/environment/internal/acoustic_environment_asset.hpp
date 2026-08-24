#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "acoustic_field_asset.hpp"
#include "bathymetry_profile.hpp"
#include "bellhop_environment_builder.hpp"
#include "environment_profile.hpp"

namespace ns3_factory::environment::internal {

// P0.4 names this normalized four-axis field an AcousticFieldAtlas. Keep the
// existing, already integrated AcousticFieldAsset implementation as the
// storage type until the public contracts/environment.hpp is supplied.
using AcousticFieldAtlas = AcousticFieldAsset;

struct AssetProvenance final {
  std::string source_format;
  std::string source_uri;
  std::string content_digest;
  std::string generated_by;

  [[nodiscard]] auto Validate() const -> contracts::Status {
    if(source_format.empty() || source_uri.empty() ||
       content_digest.empty() || generated_by.empty()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Environment asset provenance fields must not be empty"});
    }
    return {};
  }

  [[nodiscard]] auto Describe() const -> std::string {
    return "source_format=" + source_format + "|source_uri=" + source_uri +
           "|content_digest=" + content_digest +
           "|generated_by=" + generated_by;
  }

  auto operator==(const AssetProvenance&) const -> bool = default;
};

struct BellhopFrequencyConfiguration final {
  double frequency_hz;
  BellhopRunConfiguration run;

  auto operator==(const BellhopFrequencyConfiguration&) const
      -> bool = default;
};

// Complete immutable offline asset. Runtime-facing code must eventually use
// the frozen public contract; until then this type stays private to Environment
// so no other module becomes coupled to an internal header.
class AcousticEnvironmentAsset final {
 public:
  [[nodiscard]] static auto Create(
      std::string asset_id,
      std::uint32_t version,
      GeographicRegion region,
      std::string valid_time,
      SoundSpeedProfile sound_speed_profile,
      BathymetryProfile bathymetry_profile,
      std::vector<BellhopFrequencyConfiguration> bellhop_configurations,
      AssetProvenance provenance,
      std::shared_ptr<const AcousticFieldAtlas> field_atlas)
      -> contracts::Result<AcousticEnvironmentAsset> {
    if(asset_id.empty() || version == 0U || valid_time.empty() ||
       !field_atlas) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Environment asset requires an id, version, valid time, and atlas"});
    }
    const auto provenance_status = provenance.Validate();
    if(!provenance_status) {
      return std::unexpected(provenance_status.error());
    }
    if(bellhop_configurations.size() != field_atlas->frequency_hz().size()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Bellhop configuration count must match the atlas frequency axis"});
    }
    for(std::size_t index = 0U;
        index < bellhop_configurations.size();
        ++index) {
      const auto& configuration = bellhop_configurations[index];
      if(!std::isfinite(configuration.frequency_hz) ||
         configuration.frequency_hz <= 0.0 ||
         configuration.frequency_hz != field_atlas->frequency_hz()[index]) {
        return std::unexpected(contracts::Error{
            contracts::ErrorCode::kInvalidArgument,
            "Bellhop configuration frequencies must exactly match the atlas"});
      }
      const auto configuration_status =
          ValidateBellhopRunConfiguration(configuration.run);
      if(!configuration_status) {
        return std::unexpected(configuration_status.error());
      }
    }

    return AcousticEnvironmentAsset{
        std::move(asset_id),
        version,
        region,
        std::move(valid_time),
        std::move(sound_speed_profile),
        std::move(bathymetry_profile),
        std::move(bellhop_configurations),
        std::move(provenance),
        std::move(field_atlas)};
  }

  [[nodiscard]] auto asset_id() const noexcept -> const std::string& {
    return asset_id_;
  }

  [[nodiscard]] constexpr auto version() const noexcept -> std::uint32_t {
    return version_;
  }

  [[nodiscard]] constexpr auto region() const noexcept
      -> const GeographicRegion& {
    return region_;
  }

  [[nodiscard]] auto valid_time() const noexcept -> const std::string& {
    return valid_time_;
  }

  [[nodiscard]] auto sound_speed_profile() const noexcept
      -> const SoundSpeedProfile& {
    return sound_speed_profile_;
  }

  [[nodiscard]] auto bathymetry_profile() const noexcept
      -> const BathymetryProfile& {
    return bathymetry_profile_;
  }

  [[nodiscard]] auto bellhop_configurations() const noexcept
      -> std::span<const BellhopFrequencyConfiguration> {
    return bellhop_configurations_;
  }

  [[nodiscard]] auto provenance() const noexcept
      -> const AssetProvenance& {
    return provenance_;
  }

  [[nodiscard]] auto field_atlas() const noexcept
      -> const AcousticFieldAtlas& {
    return *field_atlas_;
  }

  [[nodiscard]] auto field_atlas_pointer() const noexcept
      -> const std::shared_ptr<const AcousticFieldAtlas>& {
    return field_atlas_;
  }

 private:
  AcousticEnvironmentAsset(
      std::string asset_id,
      std::uint32_t version,
      GeographicRegion region,
      std::string valid_time,
      SoundSpeedProfile sound_speed_profile,
      BathymetryProfile bathymetry_profile,
      std::vector<BellhopFrequencyConfiguration> bellhop_configurations,
      AssetProvenance provenance,
      std::shared_ptr<const AcousticFieldAtlas> field_atlas)
      : asset_id_(std::move(asset_id)),
        version_(version),
        region_(region),
        valid_time_(std::move(valid_time)),
        sound_speed_profile_(std::move(sound_speed_profile)),
        bathymetry_profile_(std::move(bathymetry_profile)),
        bellhop_configurations_(std::move(bellhop_configurations)),
        provenance_(std::move(provenance)),
        field_atlas_(std::move(field_atlas)) {}

  std::string asset_id_;
  std::uint32_t version_;
  GeographicRegion region_;
  std::string valid_time_;
  SoundSpeedProfile sound_speed_profile_;
  BathymetryProfile bathymetry_profile_;
  std::vector<BellhopFrequencyConfiguration> bellhop_configurations_;
  AssetProvenance provenance_;
  std::shared_ptr<const AcousticFieldAtlas> field_atlas_;
};

}  // namespace ns3_factory::environment::internal
