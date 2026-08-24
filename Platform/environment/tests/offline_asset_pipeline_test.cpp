#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include "internal/environment_asset_repository.hpp"
#include "internal/offline_asset_pipeline.hpp"

using namespace ns3_factory;

namespace {

auto Check(bool condition) -> void {
  if(!condition) {
    std::abort();
  }
}

class FixtureBellhopRunner final : public environment::IBellhopRunner {
 public:
  [[nodiscard]] auto Run(
      std::string_view case_name,
      const environment::BellhopInputFiles& input_files) const
      -> contracts::Result<environment::BellhopRunResult> override {
    if(case_name != "pipeline-fixture" ||
       input_files.environment_ascii.find("12000.0") ==
           std::string::npos ||
       input_files.bathymetry_ascii.empty() ||
       input_files.surface_ascii.empty()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Fixture runner received unexpected Bellhop "
                           "inputs"});
    }
    return environment::BellhopRunResult{
        "'2D'\n"
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
        "0\n",
        "fixture-bellhop-runner"};
  }
};

}  // namespace

int main() {
  auto region = environment::GeographicRegion::Create(
      10.0, 100.0, 11.0, 101.0);
  Check(region.has_value());
  auto hydrographic = environment::HydrographicProfile::Create(
      {{0.0, 10.0, 35.0}, {100.0, 8.0, 34.0}});
  Check(hydrographic.has_value());
  auto bathymetry = environment::BathymetryProfile::Create(
      {0.0, 1000.0}, {100.0, 100.0});
  Check(bathymetry.has_value());
  auto environment_source = environment::ManualEnvironmentSource::Create(
      *region, "fixture-annual", *hydrographic);
  Check(environment_source.has_value());
  auto bathymetry_source = environment::ManualBathymetrySource::Create(
      *region, "fixture-annual", *bathymetry);
  Check(bathymetry_source.has_value());
  auto query = environment::EnvironmentSourceQuery::Create(
      10.5, 100.5, "fixture-annual");
  Check(query.has_value());
  auto coordinate_frame =
      environment::internal::EnvironmentCoordinateFrame::Create(
          0.0,
          environment::internal::VerticalAxisDirection::kPositiveDown);
  Check(coordinate_frame.has_value());

  environment::MackenzieSoundSpeedModel sound_speed_model;
  environment::BellhopAsciiEnvironmentBuilder environment_builder;
  FixtureBellhopRunner runner;
  environment::OfflineEnvironmentAssetPipeline pipeline{
      *environment_source,
      *bathymetry_source,
      sound_speed_model,
      environment_builder,
      runner};
  environment::EnvironmentAssetGenerationRequest request{
      1U,
      "fixture://environment|sha256:pipeline-fixture",
      *coordinate_frame,
      environment::internal::import::BellhopArrivalImportOptions{
          environment::internal::import::BellhopReceiverRangeUnit::kMeters},
      *region,
      *query,
      30.0,
      {10.0, 20.0},
      1000.0,
      100.0,
      environment::SoundSpeedCoveragePolicy::kRequireExact,
      {0.0, 1000.0},
      {0.0, 0.0},
      {{"pipeline-fixture",
        12'000.0,
        {"pipeline fixture", "SVW", "A", 0.0, 1600.0, 1.8, 0.8,
         2, 0, -80.0, 80.0, 0.0}}}};
  auto asset = pipeline.GenerateEnvironmentAsset(
      request,
      {"pipeline-fixture",
       1U,
       "fixture-annual",
       {"test-fixture", "fixture://pipeline", "fnv1a64:pipeline",
        "offline-pipeline-test"}});
  Check(asset.has_value());
  Check(asset->asset_id() == "pipeline-fixture");
  Check(asset->field_atlas().frequency_hz().size() == 1U);
  Check(asset->field_atlas().frequency_hz().front() == 12'000.0);
  Check(asset->field_atlas().cells().size() == 4U);
  Check(std::holds_alternative<
        environment::internal::AcousticFieldSignalCell>(
            asset->field_atlas().cells()[0]));
  Check(std::holds_alternative<
        environment::internal::AcousticFieldNoArrivalCell>(
            asset->field_atlas().cells()[1]));

  environment::InMemoryEnvironmentAssetRepository repository;
  Check(repository.Store(
      std::make_shared<const environment::internal::AcousticEnvironmentAsset>(
          std::move(*asset))).has_value());
  Check(repository.Find({"pipeline-fixture", 1U}) != nullptr);

  return EXIT_SUCCESS;
}
