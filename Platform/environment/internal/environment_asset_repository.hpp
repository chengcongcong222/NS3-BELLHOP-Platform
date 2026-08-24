#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "acoustic_field_asset.hpp"

namespace ns3_factory::environment {

struct AcousticFieldAssetKey final {
  std::string asset_id;
  std::uint32_t revision;

  auto operator==(const AcousticFieldAssetKey&) const -> bool = default;
};

class IAcousticFieldAssetRepository {
 public:
  virtual ~IAcousticFieldAssetRepository() = default;

  [[nodiscard]] virtual auto Find(
      const AcousticFieldAssetKey& key) const noexcept
      -> std::shared_ptr<const internal::AcousticFieldAsset> = 0;
};

// Deterministic process-local repository for immutable normalized assets.
// Persistence remains an explicit later design decision; this repository does
// not read files, run Bellhop, or perform fallback asset selection.
class InMemoryAcousticFieldAssetRepository final
    : public IAcousticFieldAssetRepository {
 public:
  [[nodiscard]] auto Add(
      AcousticFieldAssetKey key,
      std::shared_ptr<const internal::AcousticFieldAsset> asset)
      -> contracts::Status {
    if(key.asset_id.empty() || key.revision == 0U || !asset) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Asset repository requires a non-empty id, "
                           "non-zero revision, and non-null asset"});
    }
    if(Find(key)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kAlreadyExists,
                           "Acoustic field asset id/revision already exists"});
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
      const AcousticFieldAssetKey& key) const noexcept
      -> std::shared_ptr<const internal::AcousticFieldAsset> override {
    const auto found = std::find_if(
        entries_.begin(), entries_.end(), [&](const auto& entry) {
          return entry.key == key;
        });
    return found == entries_.end() ? nullptr : found->asset;
  }

 private:
  struct Entry final {
    AcousticFieldAssetKey key;
    std::shared_ptr<const internal::AcousticFieldAsset> asset;
  };

  std::vector<Entry> entries_;
};

}  // namespace ns3_factory::environment
