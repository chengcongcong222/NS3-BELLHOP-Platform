#include <cstdlib>
#include <memory>
#include <utility>

#include "internal/environment_asset_repository.hpp"

using namespace ns3_factory;

namespace {

auto Check(bool condition) -> void {
  if(!condition) {
    std::abort();
  }
}

auto MakeAsset(std::string asset_id,
               std::uint32_t version)
    -> environment::internal::AcousticEnvironmentAsset {
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
  auto atlas = environment::internal::AcousticFieldAtlas::Create(
      1U,
      "repository-fixture",
      *frame,
      {12'000.0},
      {30.0},
      {10.0},
      {0.0},
      {environment::internal::AcousticFieldNoArrivalCell{}});
  Check(atlas.has_value());
  auto asset = environment::internal::AcousticEnvironmentAsset::Create(
      std::move(asset_id),
      version,
      *region,
      "fixture-annual",
      std::move(*sound_speed),
      std::move(*bathymetry),
      {{12'000.0,
        {"repository fixture", "SVW", "A", 0.0, 1600.0, 1.8, 0.8,
         2U, 0U, -80.0, 80.0, 0.0}}},
      {"test-fixture", "fixture://repository", "fnv1a64:test",
       "environment-test"},
      std::make_shared<const environment::internal::AcousticFieldAtlas>(
          std::move(*atlas)));
  Check(asset.has_value());
  return std::move(*asset);
}

}  // namespace

int main() {
  auto complete_asset = MakeAsset("april-18n-130e", 1U);
  Check(complete_asset.asset_id() == "april-18n-130e");
  Check(complete_asset.version() == 1U);
  Check(complete_asset.valid_time() == "fixture-annual");
  Check(complete_asset.sound_speed_profile().samples().size() == 2U);
  Check(complete_asset.bathymetry_profile().ranges_meters().size() == 2U);
  Check(complete_asset.bellhop_configurations().size() == 1U);
  Check(complete_asset.field_atlas().frequency_hz().front() == 12'000.0);

  environment::InMemoryEnvironmentAssetRepository repository;
  const environment::EnvironmentAssetKey key{"april-18n-130e", 1U};
  auto asset =
      std::make_shared<const environment::internal::AcousticEnvironmentAsset>(
          std::move(complete_asset));

  Check(!repository.Store(nullptr).has_value());
  Check(repository.Store(asset).has_value());
  Check(repository.Find(key) == asset);
  Check(repository.Find({"april-18n-130e", 2U}) == nullptr);
  Check(!repository.Store(asset).has_value());
  return EXIT_SUCCESS;
}
