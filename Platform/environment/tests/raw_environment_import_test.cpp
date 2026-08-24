#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "internal/bellhop_process_runner.hpp"
#include "internal/offline_raw_environment_asset_builder.hpp"

using namespace ns3_factory;

namespace {

auto Check(bool condition) -> void {
  if(!condition) {
    std::abort();
  }
}

class TemporaryWorkspace final {
 public:
  TemporaryWorkspace() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    root_ = std::filesystem::temp_directory_path() /
            ("platform-raw-environment-e2e-" + std::to_string(suffix));
    Check(std::filesystem::create_directories(root_ / "assets"));
    Check(std::filesystem::create_directories(root_ / "bellhop"));
  }

  ~TemporaryWorkspace() {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  [[nodiscard]] auto asset_root() const -> std::filesystem::path {
    return root_ / "assets";
  }

  [[nodiscard]] auto bellhop_root() const -> std::filesystem::path {
    return root_ / "bellhop";
  }

 private:
  std::filesystem::path root_;
};

}  // namespace

int main(int argc, char** argv) {
  Check(argc == 7);
  const std::filesystem::path python_executable{argv[1]};
  const std::filesystem::path importer_script{argv[2]};
  const std::filesystem::path temperature_path{argv[3]};
  const std::filesystem::path salinity_path{argv[4]};
  const std::filesystem::path gebco_response_path{argv[5]};
  const std::filesystem::path bellhop_executable{argv[6]};

  TemporaryWorkspace workspace;
  auto importer = environment::PythonRawEnvironmentImporter::Create(
      {python_executable, importer_script, std::chrono::minutes{2}});
  Check(importer.has_value());
  auto runner = environment::BellhopProcessRunner::Create(
      {bellhop_executable,
       workspace.bellhop_root(),
       std::chrono::minutes{5},
       128U * 1024U * 1024U});
  Check(runner.has_value());
  auto frame = environment::internal::EnvironmentCoordinateFrame::Create(
      0.0,
      environment::internal::VerticalAxisDirection::kPositiveDown);
  Check(frame.has_value());
  auto region = environment::GeographicRegion::Create(
      18.0, 130.0, 18.0, 130.0);
  Check(region.has_value());
  auto query = environment::EnvironmentSourceQuery::Create(
      18.0, 130.0, "WOA23 month-04 climatology");
  Check(query.has_value());

  environment::BellhopAsciiEnvironmentBuilder environment_builder;
  environment::InMemoryEnvironmentAssetRepository repository;
  environment::OfflineRawEnvironmentAssetBuilder builder{
      *importer, environment_builder, *runner, repository};
  environment::RawEnvironmentAssetBuildRequest request{
      {temperature_path,
       salinity_path,
       18.0,
       130.0,
       500.0,
       90.0,
       1000.0,
       3U,
       "raw-e2e-april",
       "Raw WOA23 April end-to-end fixture",
       "WOA23 month-04 climatology",
       "2023-month-04-1deg",
       "2020-recorded-response",
       workspace.asset_root(),
       gebco_response_path,
       false,
       false},
      1U,
      {1U,
       "replaced-by-raw-e2e-builder",
       *frame,
       environment::internal::import::BellhopArrivalImportOptions{
           environment::internal::import::BellhopReceiverRangeUnit::kMeters},
       *region,
       *query,
       30.0,
       {10.0, 30.0, 50.0, 70.0, 90.0},
       1000.0,
       500.0,
       environment::SoundSpeedCoveragePolicy::kRequireExact,
       {0.0, 1000.0},
       {0.0, 0.0},
       {{"raw-e2e-case",
         12'000.0,
         {"raw WOA23 April end-to-end", "SVW", "A", 0.0, 1600.0,
          1.8, 0.8, 6U, 0U, -80.0, 80.0, 0.0}}}}};

  const auto built = builder.BuildAndStore(std::move(request));
  Check(built.has_value());
  Check((*built)->asset_id() == "raw-e2e-april");
  Check((*built)->version() == 1U);
  Check((*built)->sound_speed_profile().samples().size() == 37U);
  Check((*built)->sound_speed_profile().samples().front()
            .speed_meters_per_second == 1540.058);
  Check((*built)->sound_speed_profile().samples().back()
            .speed_meters_per_second == 1493.239);
  Check((*built)->bathymetry_profile().depths_meters().size() == 3U);
  Check((*built)->field_atlas().frequency_hz().front() == 12'000.0);
  Check((*built)->field_atlas().cells().size() == 30U);
  Check(repository.Find({"raw-e2e-april", 1U}) == *built);
  Check(std::filesystem::is_regular_file(
      workspace.asset_root() / "data" / "woss_sources" /
      "raw-e2e-april.json"));
  return EXIT_SUCCESS;
}
