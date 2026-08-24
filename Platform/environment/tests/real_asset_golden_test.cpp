#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

#include "internal/bellhop_process_runner.hpp"
#include "internal/offline_asset_pipeline.hpp"
#include "internal/woss_cache_loader.hpp"

using namespace ns3_factory;

namespace {

auto Check(
    bool condition,
    const std::source_location location = std::source_location::current())
    -> void {
  if(!condition) {
    std::cerr << "Golden check failed at line " << location.line() << '\n';
    std::abort();
  }
}

class TemporaryDirectory final {
 public:
  TemporaryDirectory() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    path_ = std::filesystem::temp_directory_path() /
            ("platform-real-bellhop-golden-" + std::to_string(suffix));
    Check(std::filesystem::create_directory(path_));
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  auto operator=(const TemporaryDirectory&) -> TemporaryDirectory& = delete;

  [[nodiscard]] auto path() const noexcept
      -> const std::filesystem::path& {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace

int main(int argc, char** argv) {
  Check(argc == 5);
  const std::filesystem::path executable_path{argv[1]};
  const std::filesystem::path asset_root{argv[2]};
  const std::filesystem::path manifest_path{argv[3]};
  const std::filesystem::path reference_arrivals_path{argv[4]};

  auto cached = environment::WossCacheLoader::Load(manifest_path,
                                                    asset_root);
  Check(cached.has_value());
  const auto maximum_depth = *std::max_element(
      cached->bathymetry_profile.depths_meters().begin(),
      cached->bathymetry_profile.depths_meters().end());
  const auto maximum_range =
      cached->bathymetry_profile.ranges_meters().back();
  auto coordinate_frame =
      environment::internal::EnvironmentCoordinateFrame::Create(
          0.0,
          environment::internal::VerticalAxisDirection::kPositiveDown);
  Check(coordinate_frame.has_value());

  TemporaryDirectory workspace;
  auto runner = environment::BellhopProcessRunner::Create(
      {executable_path,
       workspace.path(),
       std::chrono::seconds{300},
       128U * 1024U * 1024U});
  Check(runner.has_value());
  environment::BellhopAsciiEnvironmentBuilder builder;
  environment::OfflineBellhopAssetPipeline pipeline{builder, *runner};

  const environment::internal::import::BellhopArrivalImportOptions
      arrival_options{
          environment::internal::import::BellhopReceiverRangeUnit::
              kMeters};
  environment::EnvironmentAssetGenerationRequest request{
      1U,
      cached->provenance.Describe(),
      *coordinate_frame,
      arrival_options,
      cached->geographic_region,
      cached->source_query,
      30.0,
      {10.0, 30.0, 50.0, 70.0, 90.0},
      maximum_range,
      maximum_depth,
      environment::SoundSpeedCoveragePolicy::
          kHoldLastValueToMaximumDepth,
      {0.0, maximum_range},
      {0.0, 0.0},
      {{"real-golden-case",
        12'000.0,
        {"NS3_Factory: 20260403-1",
         "SVW",
         "A",
         0.0,
         1600.0,
         1.8,
         0.8,
         501,
         0,
         -80.0,
         80.0,
         0.0}}}};
  auto generated = pipeline.Generate(request,
                                     std::move(cached->sound_speed_profile),
                                     std::move(cached->bathymetry_profile));
  if(!generated) {
    std::cerr << "Generation failed: " << generated.error().message << '\n';
  }
  Check(generated.has_value());

  auto reference_dataset =
      environment::internal::import::BellhopAsciiArrivalParser::ParseFile(
          reference_arrivals_path, arrival_options);
  Check(reference_dataset.has_value());
  std::vector<environment::internal::import::BellhopRawArrivalDataset>
      reference_datasets;
  reference_datasets.push_back(std::move(*reference_dataset));
  auto reference_bundle =
      environment::internal::import::BellhopRawArrivalBundle::Create(
          std::move(reference_datasets));
  Check(reference_bundle.has_value());
  auto reference =
      environment::internal::import::BellhopRawArrivalNormalizer::Normalize(
          *reference_bundle,
          request.asset_format_version,
          "legacy-reference-arrivals",
          request.coordinate_frame);
  Check(reference.has_value());

  Check(generated->format_version() == reference->format_version());
  Check(generated->coordinate_frame() == reference->coordinate_frame());
  Check(std::ranges::equal(generated->frequency_hz(),
                           reference->frequency_hz()));
  Check(std::ranges::equal(generated->source_depth_m(),
                           reference->source_depth_m()));
  Check(std::ranges::equal(generated->receiver_depth_m(),
                           reference->receiver_depth_m()));
  Check(std::ranges::equal(generated->horizontal_range_m(),
                           reference->horizontal_range_m()));
  if(!std::ranges::equal(generated->cells(), reference->cells())) {
    const auto generated_cells = generated->cells();
    const auto reference_cells = reference->cells();
    for(std::size_t index = 0U; index < generated_cells.size(); ++index) {
      if(generated_cells[index] == reference_cells[index]) {
        continue;
      }
      std::cerr << std::setprecision(17)
                << "First cell mismatch at " << index << '\n';
      const auto* generated_signal = std::get_if<
          environment::internal::AcousticFieldSignalCell>(
              &generated_cells[index]);
      const auto* reference_signal = std::get_if<
          environment::internal::AcousticFieldSignalCell>(
              &reference_cells[index]);
      if(generated_signal != nullptr && reference_signal != nullptr) {
        std::cerr << "TL generated="
                  << generated_signal->aggregate_transmission_loss_db
                  << " reference="
                  << reference_signal->aggregate_transmission_loss_db
                  << " delay generated="
                  << generated_signal->first_arrival_delay.nanoseconds()
                  << " reference="
                  << reference_signal->first_arrival_delay.nanoseconds()
                  << " paths generated=" << generated_signal->paths.size()
                  << " reference=" << reference_signal->paths.size() << '\n';
      }
      break;
    }
  }
  Check(std::ranges::equal(generated->cells(), reference->cells()));
  return EXIT_SUCCESS;
}
