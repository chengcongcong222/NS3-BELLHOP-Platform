#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>

#include "internal/reference_environment_asset.hpp"

auto main(int argc, char** argv) -> int {
  using namespace ns3_factory::environment::internal;
  if(argc != 3) {
    std::cerr << "usage: platform_reference_environment_builder "
                 "ASSET_ROOT REPOSITORY_ROOT\n";
    return EXIT_FAILURE;
  }
  auto repository = EnvironmentAssetRepository::Open(
      std::filesystem::path{argv[2]});
  auto asset_id = EnvironmentAssetId::Create(
      kReferenceShallowWaterV1AssetId);
  if(!repository || !asset_id) return EXIT_FAILURE;

  const auto existing = repository->Find(*asset_id);
  if(existing) {
    if(!MatchesReferenceShallowWaterV1Metadata(existing->metadata)) {
      std::cerr << "immutable reference asset ID already contains different "
                   "content\n";
      return EXIT_FAILURE;
    }
    std::cout << kReferenceShallowWaterV1AssetId << ' '
              << std::hex << std::setfill('0') << std::setw(16)
              << existing->metadata.payload_checksum << '\n';
    return EXIT_SUCCESS;
  }

  auto asset = BuildReferenceShallowWaterV1(argv[1]);
  auto provenance = ReferenceShallowWaterV1Provenance();
  if(!asset || !provenance) return EXIT_FAILURE;
  const auto metadata = repository->Register(
      *asset_id, *asset, *provenance);
  if(!metadata) {
    std::cerr << metadata.error().message << '\n';
    return EXIT_FAILURE;
  }
  if(kReferenceShallowWaterV1PayloadChecksum != 0U &&
     !MatchesReferenceShallowWaterV1Metadata(*metadata)) {
    std::cerr << "canonical reference asset metadata mismatch\n";
    return EXIT_FAILURE;
  }
  std::cout << kReferenceShallowWaterV1AssetId << ' '
            << std::hex << std::setfill('0') << std::setw(16)
            << metadata->payload_checksum << '\n';
  return EXIT_SUCCESS;
}
