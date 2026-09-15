#include <filesystem>
#include <iostream>
#include <vector>

#include "internal/environment_asset_repository.hpp"
#include "internal/environment_coordinate_frame.hpp"
#include "internal/import/bellhop_ascii_arrival_parser.hpp"
#include "internal/import/bellhop_raw_arrival_bundle.hpp"
#include "internal/import/bellhop_raw_arrival_normalizer.hpp"

int main(int argc, char** argv) {
  using namespace ns3_factory;
  using namespace environment::internal;
  using namespace environment::internal::import;
  if(argc != 4) return 2;
  auto dataset = BellhopAsciiArrivalParser::ParseFile(
      std::filesystem::path{argv[1]},
      BellhopArrivalImportOptions{BellhopReceiverRangeUnit::kMeters});
  if(!dataset) { std::cerr << dataset.error().message; return 3; }
  std::vector<BellhopRawArrivalDataset> values;
  values.push_back(std::move(*dataset));
  auto bundle = BellhopRawArrivalBundle::Create(std::move(values));
  auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!bundle || !frame) return 3;
  auto asset = BellhopRawArrivalNormalizer::Normalize(
      *bundle, 1, "Authored Bellhop offline environment", *frame);
  auto repository = EnvironmentAssetRepository::Open(argv[2]);
  auto id = EnvironmentAssetId::Create(argv[3]);
  auto provenance = EnvironmentAssetPackageProvenance::Create(
      EnvironmentAssetProducerType::kBellhopRawImport,
      "p0-e2e-01", "User-authored Bellhop offline environment",
      std::filesystem::path{argv[1]}.filename().string(),
      "bellhop-raw-arrival-normalizer-v1");
  if(!asset || !repository || !id || !provenance) return 3;
  const auto result = repository->Register(*id, *asset, *provenance);
  if(!result) { std::cerr << result.error().message; return 3; }
  std::cout << result->payload_checksum << '\n';
  return 0;
}
