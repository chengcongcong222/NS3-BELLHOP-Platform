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

auto MakeAsset() -> environment::internal::AcousticFieldAsset {
  auto frame = environment::internal::EnvironmentCoordinateFrame::Create(
      0.0,
      environment::internal::VerticalAxisDirection::kPositiveDown);
  Check(frame.has_value());
  auto asset = environment::internal::AcousticFieldAsset::Create(
      1U,
      "repository-fixture",
      *frame,
      {12'000.0},
      {30.0},
      {10.0},
      {0.0},
      {environment::internal::AcousticFieldNoArrivalCell{}});
  Check(asset.has_value());
  return std::move(*asset);
}

}  // namespace

int main() {
  environment::InMemoryAcousticFieldAssetRepository repository;
  const environment::AcousticFieldAssetKey key{"april-18n-130e", 1U};
  auto asset =
      std::make_shared<const environment::internal::AcousticFieldAsset>(
          MakeAsset());

  Check(!repository.Add({"", 1U}, asset).has_value());
  Check(!repository.Add({"april-18n-130e", 0U}, asset).has_value());
  Check(!repository.Add(key, nullptr).has_value());
  Check(repository.Add(key, asset).has_value());
  Check(repository.Find(key) == asset);
  Check(repository.Find({"april-18n-130e", 2U}) == nullptr);
  Check(!repository.Add(key, asset).has_value());
  return EXIT_SUCCESS;
}
