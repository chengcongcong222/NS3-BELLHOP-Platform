#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <utility>

#include "internal/offline_raw_environment_asset_builder.hpp"

using namespace ns3_factory;

namespace {

auto Check(bool condition) -> void {
  if(!condition) {
    std::abort();
  }
}

class FixtureRawImporter final : public environment::IRawEnvironmentImporter {
 public:
  explicit FixtureRawImporter(std::filesystem::path asset_root)
      : asset_root_(std::move(asset_root)) {}

  [[nodiscard]] auto Import(
      const environment::RawEnvironmentImportRequest&) const
      -> contracts::Result<environment::RawEnvironmentImportResult> override {
    ++call_count_;
    return environment::RawEnvironmentImportResult{
        asset_root_,
        asset_root_ / "data" / "woss_sources" / "fixture.json"};
  }

  [[nodiscard]] auto call_count() const noexcept -> std::size_t {
    return call_count_;
  }

 private:
  std::filesystem::path asset_root_;
  mutable std::size_t call_count_{0U};
};

class FixtureBellhopRunner final : public environment::IBellhopRunner {
 public:
  [[nodiscard]] auto Run(
      std::string_view case_name,
      const environment::BellhopInputFiles& input_files) const
      -> contracts::Result<environment::BellhopRunResult> override {
    ++call_count_;
    if(case_name != "unified-fixture" ||
       input_files.environment_ascii.find("12000.0") ==
           std::string::npos) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kFailedPrecondition,
          "Unified fixture runner received unexpected input"});
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
        "unified-fixture-runner"};
  }

  [[nodiscard]] auto call_count() const noexcept -> std::size_t {
    return call_count_;
  }

 private:
  mutable std::size_t call_count_{0U};
};

class FailingBellhopRunner final : public environment::IBellhopRunner {
 public:
  [[nodiscard]] auto Run(
      std::string_view,
      const environment::BellhopInputFiles&) const
      -> contracts::Result<environment::BellhopRunResult> override {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kUnavailable,
                         "Intentional Bellhop fixture failure"});
  }
};

[[nodiscard]] auto MakeRequest(
    const std::filesystem::path& root)
    -> environment::RawEnvironmentAssetBuildRequest {
  auto region = environment::GeographicRegion::Create(
      18.0, 130.0, 18.0, 130.0);
  Check(region.has_value());
  auto query = environment::EnvironmentSourceQuery::Create(
      18.0, 130.0, "WOA23 month-04 climatology");
  Check(query.has_value());
  auto frame = environment::internal::EnvironmentCoordinateFrame::Create(
      0.0,
      environment::internal::VerticalAxisDirection::kPositiveDown);
  Check(frame.has_value());
  return {
      {root / "data" / "ssp" / "fixture.csv",
       root / "data" / "ssp" / "fixture.csv",
       18.0,
       130.0,
       200.0,
       90.0,
       1000.0,
       3U,
       "fixture-source",
       "fixture-source",
       "WOA23 month-04 climatology",
       "2023-month-04-1deg",
       "2020-public-api",
       root,
       root / "data" / "bathymetry" / "fixture.json",
       false,
       false},
      1U,
      {1U,
       "replaced-by-unified-builder",
       *frame,
       environment::internal::import::BellhopArrivalImportOptions{
           environment::internal::import::BellhopReceiverRangeUnit::kMeters},
       *region,
       *query,
       30.0,
       {10.0, 20.0},
       1000.0,
       200.0,
       environment::SoundSpeedCoveragePolicy::kRequireExact,
       {0.0, 1000.0},
       {0.0, 0.0},
       {{"unified-fixture",
         12'000.0,
         {"unified fixture", "SVW", "A", 0.0, 1600.0, 1.8, 0.8,
          2U, 0U, -80.0, 80.0, 0.0}}}}};
}

}  // namespace

int main(int argc, char** argv) {
  Check(argc == 2);
  const std::filesystem::path root{argv[1]};
  FixtureRawImporter importer{root};
  FixtureBellhopRunner runner;
  environment::BellhopAsciiEnvironmentBuilder environment_builder;
  environment::InMemoryEnvironmentAssetRepository repository;
  environment::OfflineRawEnvironmentAssetBuilder builder{
      importer, environment_builder, runner, repository};

  auto implicit_network_request = MakeRequest(root);
  implicit_network_request.raw_import.gebco_response_path.reset();
  const auto implicit_network =
      builder.BuildAndStore(std::move(implicit_network_request));
  Check(!implicit_network.has_value());
  Check(implicit_network.error().code ==
        contracts::ErrorCode::kInvalidArgument);
  Check(importer.call_count() == 0U);
  Check(runner.call_count() == 0U);

  auto request = MakeRequest(root);
  const auto built = builder.BuildAndStore(request);
  Check(built.has_value());
  Check((*built)->asset_id() == "fixture-source");
  Check((*built)->version() == 1U);
  Check((*built)->sound_speed_profile().samples().size() == 3U);
  Check((*built)->field_atlas().cells().size() == 4U);
  Check(repository.Find({"fixture-source", 1U}) == *built);
  Check(importer.call_count() == 1U);
  Check(runner.call_count() == 1U);

  const auto duplicate = builder.BuildAndStore(request);
  Check(!duplicate.has_value());
  Check(duplicate.error().code == contracts::ErrorCode::kAlreadyExists);
  Check(importer.call_count() == 1U);
  Check(runner.call_count() == 1U);

  FailingBellhopRunner failing_runner;
  environment::InMemoryEnvironmentAssetRepository empty_repository;
  environment::OfflineRawEnvironmentAssetBuilder failing_builder{
      importer, environment_builder, failing_runner, empty_repository};
  const auto failed = failing_builder.BuildAndStore(MakeRequest(root));
  Check(!failed.has_value());
  Check(failed.error().code == contracts::ErrorCode::kUnavailable);
  Check(empty_repository.Find({"fixture-source", 1U}) == nullptr);
  return EXIT_SUCCESS;
}
