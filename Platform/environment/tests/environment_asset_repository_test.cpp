#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>

#include "internal/acoustic_field_asset.hpp"
#include "internal/environment_asset_repository.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::environment::internal;

namespace {

class TemporaryRepositoryRoot final {
 public:
  TemporaryRepositoryRoot()
      : path_(std::filesystem::temp_directory_path() /
              "ns3_factory_environment_asset_repository_test") {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
    std::filesystem::create_directory(path_);
  }

  ~TemporaryRepositoryRoot() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] auto path() const -> const std::filesystem::path& {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

auto Asset() -> Result<AcousticFieldAsset> {
  auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveDown);
  if(!frame) return std::unexpected(frame.error());
  return AcousticFieldAsset::Create(
      1U,
      "repository fixture",
      *frame,
      {10'000.0},
      {5.0},
      {10.0},
      {50.0},
      {AcousticFieldNoArrivalCell{}});
}

auto Provenance() -> Result<EnvironmentAssetPackageProvenance> {
  return EnvironmentAssetPackageProvenance::Create(
      EnvironmentAssetProducerType::kManual,
      "test-build",
      "manual repository fixture",
      "",
      "");
}

auto TestIdValidation() -> bool {
  return EnvironmentAssetId::Create("ocean-field.v1") &&
         !EnvironmentAssetId::Create("") &&
         !EnvironmentAssetId::Create(".") &&
         !EnvironmentAssetId::Create("..") &&
         !EnvironmentAssetId::Create("../asset") &&
         !EnvironmentAssetId::Create("asset/child") &&
         !EnvironmentAssetId::Create("asset\\child") &&
         !EnvironmentAssetId::Create("/absolute") &&
         !EnvironmentAssetId::Create("-leading");
}

auto TestRegisterLoadFindListAndDuplicate() -> bool {
  TemporaryRepositoryRoot temporary;
  auto repository = EnvironmentAssetRepository::Open(temporary.path());
  auto asset = Asset();
  auto provenance = Provenance();
  auto first_id = EnvironmentAssetId::Create("field-b");
  auto second_id = EnvironmentAssetId::Create("field-a");
  auto missing_id = EnvironmentAssetId::Create("missing");
  if(!repository || !asset || !provenance || !first_id || !second_id ||
     !missing_id) {
    return false;
  }

  const auto first = repository->Register(*first_id, *asset, *provenance);
  const auto duplicate =
      repository->Register(*first_id, *asset, *provenance);
  const auto second = repository->Register(*second_id, *asset, *provenance);
  auto loaded = repository->Load(*first_id);
  auto found = repository->Find(*first_id);
  auto listed = repository->List();
  const auto missing = repository->Load(*missing_id);
  if(!first || !second || duplicate || !loaded || !found || !listed ||
     missing) {
    return false;
  }
  return duplicate.error().code == ErrorCode::kAlreadyExists &&
         found->asset_id == *first_id &&
         found->metadata == *first &&
         (*loaded)->provenance() == "repository fixture" &&
         found->asset->cells().size() == 1U && listed->size() == 2U &&
         (*listed)[0] == *second_id && (*listed)[1] == *first_id &&
         missing.error().code == ErrorCode::kNotFound;
}

auto TestExplicitExistingRoot() -> bool {
  const auto missing = std::filesystem::temp_directory_path() /
                       "ns3_factory_missing_repository_root";
  std::error_code ignored;
  std::filesystem::remove_all(missing, ignored);
  return !EnvironmentAssetRepository::Open({}) &&
         !EnvironmentAssetRepository::Open(missing);
}

auto TestListRejectsCorruptRegisteredPackage() -> bool {
  TemporaryRepositoryRoot temporary;
  auto repository = EnvironmentAssetRepository::Open(temporary.path());
  auto asset = Asset();
  auto provenance = Provenance();
  auto asset_id = EnvironmentAssetId::Create("corrupt-field");
  if(!repository || !asset || !provenance || !asset_id ||
     !repository->Register(*asset_id, *asset, *provenance)) {
    return false;
  }
  std::ofstream payload{temporary.path() / asset_id->value() / "field.bin",
                        std::ios::binary | std::ios::app};
  payload.put('\0');
  payload.close();
  return !repository->List();
}

auto TestFailedPublishDoesNotExposeFinalAssetId() -> bool {
  TemporaryRepositoryRoot temporary;
  auto repository = EnvironmentAssetRepository::Open(temporary.path());
  auto asset = Asset();
  auto provenance = Provenance();
  auto asset_id = EnvironmentAssetId::Create("blocked-publish");
  if(!repository || !asset || !provenance || !asset_id) return false;

  const auto temporary_path =
      temporary.path() / ".register-blocked-publish.tmp";
  std::ofstream blocker{temporary_path};
  blocker << "reserved temporary path collision";
  blocker.close();
  const auto registration =
      repository->Register(*asset_id, *asset, *provenance);
  return !registration &&
         !std::filesystem::exists(temporary.path() / asset_id->value());
}

}  // namespace

auto main() -> int {
  return TestIdValidation() && TestRegisterLoadFindListAndDuplicate() &&
                 TestExplicitExistingRoot() &&
                 TestListRejectsCorruptRegisteredPackage() &&
                 TestFailedPublishDoesNotExposeFinalAssetId()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
