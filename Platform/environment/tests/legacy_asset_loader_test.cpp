#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "internal/legacy_asset_loader.hpp"

using namespace ns3_factory;

namespace {

auto Check(bool condition) -> void {
  if(!condition) {
    std::abort();
  }
}

class TemporaryArrivalFile final {
 public:
  TemporaryArrivalFile() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    path_ = std::filesystem::temp_directory_path() /
            ("legacy-environment-asset-" + std::to_string(suffix) +
             ".arr");
    std::ofstream output{path_};
    Check(output.is_open());
    output << "'2D'\n"
              "12000\n"
              "1 30\n"
              "2 10 20\n"
              "2 0 1000\n"
              "1\n"
              "1\n"
              "0.5 0 0.1 0 -1 2 0 0\n"
              "0\n"
              "1\n"
              "0.25 0 0.2 0 -1 2 0 0\n"
              "0\n";
    Check(output.good());
  }

  ~TemporaryArrivalFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  [[nodiscard]] auto path() const noexcept
      -> const std::filesystem::path& {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace

int main() {
  auto region = environment::GeographicRegion::Create(
      10.0, 100.0, 11.0, 101.0);
  Check(region.has_value());
  auto sound_speed = environment::SoundSpeedProfile::Create(
      {{0.0, 1500.0}, {100.0, 1490.0}});
  Check(sound_speed.has_value());
  auto bathymetry = environment::BathymetryProfile::Create(
      {0.0, 1000.0}, {100.0, 100.0});
  Check(bathymetry.has_value());
  auto frame = environment::internal::EnvironmentCoordinateFrame::Create(
      0.0,
      environment::internal::VerticalAxisDirection::kPositiveDown);
  Check(frame.has_value());

  TemporaryArrivalFile arrival;
  environment::LegacyBellhopNativeAssetRequest request{
      "legacy-april",
      3U,
      1U,
      *region,
      "WOA23 month-04 climatology",
      std::move(*sound_speed),
      std::move(*bathymetry),
      {{12'000.0,
        {"legacy fixture", "SVW", "A", 0.0, 1600.0, 1.8, 0.8,
         2U, 0U, -80.0, 80.0, 0.0}}},
      {"legacy-bellhop-arr", arrival.path().string(), "fnv1a64:fixture",
       "LegacyEnvironmentAssetLoader"},
      *frame,
      environment::internal::import::BellhopArrivalImportOptions{
          environment::internal::import::BellhopReceiverRangeUnit::kMeters},
      {arrival.path()},
      1024U * 1024U};

  auto asset = environment::LegacyEnvironmentAssetLoader::
      ImportBellhopNative(std::move(request));
  Check(asset.has_value());
  Check(asset->asset_id() == "legacy-april");
  Check(asset->version() == 3U);
  Check(asset->field_atlas().frequency_hz().front() == 12'000.0);
  Check(asset->field_atlas().cells().size() == 4U);
  Check(asset->provenance().source_format == "legacy-bellhop-arr");
  return EXIT_SUCCESS;
}
