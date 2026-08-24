#pragma once

#include <filesystem>
#include <map>
#include <string>

#include "bathymetry_profile.hpp"
#include "environment_profile.hpp"

namespace ns3_factory::environment {

struct WossCacheManifest final {
  std::string source_id;
  std::string source_name;
  std::string provider;
  std::string mode;
  std::filesystem::path sound_speed_profile_path;
  std::filesystem::path bathymetry_profile_path;
  double latitude_degrees;
  double longitude_degrees;
  std::string time_descriptor;
  std::map<std::string, std::string> datasets;
  std::string source_kind;
};

struct EnvironmentSourceProvenance final {
  std::string source_format;
  std::string source_uri;
  std::string content_digest;
  std::string generated_by;

  [[nodiscard]] auto Describe() const -> std::string {
    return "source_format=" + source_format + "|source_uri=" + source_uri +
           "|content_digest=" + content_digest +
           "|generated_by=" + generated_by;
  }

  auto operator==(const EnvironmentSourceProvenance&) const -> bool = default;
};

struct WossCachedEnvironment final {
  WossCacheManifest manifest;
  GeographicRegion geographic_region;
  EnvironmentSourceQuery source_query;
  SoundSpeedProfile sound_speed_profile;
  BathymetryProfile bathymetry_profile;
  EnvironmentSourceProvenance provenance;
};

// Reads the frozen WOSS cache format used by the legacy system. It performs
// no downloads and resolves artifact paths under an explicitly supplied root.
class WossCacheLoader final {
 public:
  [[nodiscard]] static auto Load(
      const std::filesystem::path& manifest_path,
      const std::filesystem::path& asset_root)
      -> contracts::Result<WossCachedEnvironment>;
};

}  // namespace ns3_factory::environment

#include "woss_cache_loader_impl.hpp"
