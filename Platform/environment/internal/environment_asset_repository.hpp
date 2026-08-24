#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "acoustic_environment_asset.hpp"

namespace ns3_factory::environment {

struct EnvironmentAssetKey final {
  std::string asset_id;
  std::uint32_t revision;

  auto operator==(const EnvironmentAssetKey&) const -> bool = default;
};

class IEnvironmentAssetRepository {
 public:
  virtual ~IEnvironmentAssetRepository() = default;

  [[nodiscard]] virtual auto Store(
      std::shared_ptr<const internal::AcousticEnvironmentAsset> asset)
      -> contracts::Status = 0;

  [[nodiscard]] virtual auto Find(
      const EnvironmentAssetKey& key) const noexcept
      -> std::shared_ptr<const internal::AcousticEnvironmentAsset> = 0;
};

// Deterministic process-local repository for immutable normalized assets.
// Persistence remains an explicit later design decision; this repository does
// not read files, run Bellhop, or perform fallback asset selection.
class InMemoryEnvironmentAssetRepository final
    : public IEnvironmentAssetRepository {
 public:
  [[nodiscard]] auto Store(
      std::shared_ptr<const internal::AcousticEnvironmentAsset> asset)
      -> contracts::Status override {
    if(!asset) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Asset repository requires a non-null asset"});
    }
    const EnvironmentAssetKey key{asset->asset_id(), asset->version()};
    if(Find(key)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kAlreadyExists,
                           "Environment asset id/revision already exists"});
    }
    entries_.push_back(Entry{std::move(key), std::move(asset)});
    std::sort(entries_.begin(), entries_.end(), [](const auto& lhs,
                                                   const auto& rhs) {
      if(lhs.key.asset_id != rhs.key.asset_id) {
        return lhs.key.asset_id < rhs.key.asset_id;
      }
      return lhs.key.revision < rhs.key.revision;
    });
    return {};
  }

  [[nodiscard]] auto Find(
      const EnvironmentAssetKey& key) const noexcept
      -> std::shared_ptr<const internal::AcousticEnvironmentAsset> override {
    const auto found = std::find_if(
        entries_.begin(), entries_.end(), [&](const auto& entry) {
          return entry.key == key;
        });
    return found == entries_.end() ? nullptr : found->asset;
  }

 private:
  struct Entry final {
    EnvironmentAssetKey key;
    std::shared_ptr<const internal::AcousticEnvironmentAsset> asset;
  };

  std::vector<Entry> entries_;
};

}  // namespace ns3_factory::environment
