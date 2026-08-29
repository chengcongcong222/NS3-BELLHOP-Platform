#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

#include "internal/acoustic_field_asset.hpp"
#include "internal/environment_asset_package.hpp"
#include "internal/environment_asset_repository.hpp"

namespace {

using namespace ns3_factory;
using namespace environment::internal;

auto MakeAsset() -> contracts::Result<AcousticFieldAsset> {
  auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!frame) return std::unexpected(frame.error());
  std::vector<AcousticFieldCell> cells;
  for(std::size_t index = 0; index < 8U; ++index) {
    cells.push_back(AcousticFieldSignalCell{
        70.0, contracts::SimDuration::FromNanoseconds(500'000'000), {}});
  }
  return AcousticFieldAsset::Create(1,
                                    "backend acceptance fixture",
                                    *frame,
                                    {25'000.0},
                                    {0.0, 100.0},
                                    {0.0, 100.0},
                                    {0.0, 5'000.0},
                                    std::move(cells));
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if(argc != 2) return EXIT_FAILURE;
  const std::filesystem::path root{argv[1]};
  std::error_code error;
  if(!std::filesystem::create_directory(root, error) || error) {
    std::cerr << "cannot create test asset repository\n";
    return EXIT_FAILURE;
  }
  auto repository = EnvironmentAssetRepository::Open(root);
  auto id = EnvironmentAssetId::Create("backend-field-v1");
  auto asset = MakeAsset();
  auto provenance = EnvironmentAssetPackageProvenance::Create(
      EnvironmentAssetProducerType::kManual,
      "s4-05-test",
      "deterministic backend integration fixture",
      "",
      "");
  if(!repository || !id || !asset || !provenance) return EXIT_FAILURE;
  const auto registered = repository->Register(*id, *asset, *provenance);
  return registered ? EXIT_SUCCESS : EXIT_FAILURE;
}
