#include <cstdlib>
#include <filesystem>
#include <string>

#include "internal/woss_cache_loader.hpp"

using namespace ns3_factory;

namespace {

auto Check(bool condition) -> void {
  if(!condition) {
    std::abort();
  }
}

}  // namespace

int main(int argc, char** argv) {
  Check(argc == 2);
  const std::filesystem::path root{argv[1]};
  const auto loaded = environment::WossCacheLoader::Load(
      root / "data/woss_sources/fixture.json", root);
  Check(loaded.has_value());
  Check(loaded->manifest.source_id == "fixture-source");
  Check(loaded->manifest.provider == "woss");
  Check(loaded->manifest.datasets.at("woa") == "2023-month-04-1deg");
  Check(loaded->manifest.datasets.at("gebco") == "2020-public-api");
  Check(loaded->source_query.latitude_degrees() == 18.0);
  Check(loaded->source_query.longitude_degrees() == 130.0);
  Check(loaded->source_query.time_descriptor() ==
        "WOA23 month-04 climatology");
  Check(loaded->sound_speed_profile.samples().size() == 3);
  Check(loaded->bathymetry_profile.ranges_meters().size() == 3);
  Check(loaded->provenance.source_format == "woss-cache-v1");
  Check(loaded->provenance.content_digest.starts_with("fnv1a64:"));

  const auto argo = environment::WossCacheLoader::Load(
      root / "data/woss_sources/argo_fixture.json", root);
  Check(argo.has_value());
  Check(argo->manifest.datasets.at("argo") == "argo-profile-fixture");
  Check(argo->manifest.datasets.at("gebco") == "2020-public-api");

  const auto missing_bathymetry_source = environment::WossCacheLoader::Load(
      root / "data/woss_sources/missing_gebco.json", root);
  Check(!missing_bathymetry_source.has_value());
  Check(missing_bathymetry_source.error().code ==
        contracts::ErrorCode::kFailedPrecondition);

  const auto outside = environment::WossCacheLoader::Load(
      root / "data/woss_sources/escape.json", root);
  Check(!outside.has_value());
  Check(outside.error().code == contracts::ErrorCode::kInvalidArgument);
  return EXIT_SUCCESS;
}
