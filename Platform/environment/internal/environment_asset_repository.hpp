#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "acoustic_field_asset.hpp"
#include "environment_asset_package.hpp"

namespace ns3_factory::environment::internal {

class EnvironmentAssetId final {
 public:
  [[nodiscard]] static auto Create(std::string value)
      -> contracts::Result<EnvironmentAssetId>;

  [[nodiscard]] constexpr auto value() const noexcept -> const std::string& {
    return value_;
  }

  auto operator<=>(const EnvironmentAssetId&) const = default;

 private:
  explicit EnvironmentAssetId(std::string value)
      : value_(std::move(value)) {}

  std::string value_;
};

struct EnvironmentAssetRecord final {
  EnvironmentAssetId asset_id;
  AcousticFieldPackageMetadata metadata;
  std::shared_ptr<const AcousticFieldAsset> asset;
};

class EnvironmentAssetRepository final {
 public:
  [[nodiscard]] static auto Open(std::filesystem::path root)
      -> contracts::Result<EnvironmentAssetRepository>;

  [[nodiscard]] auto Register(
      const EnvironmentAssetId& asset_id,
      const AcousticFieldAsset& asset,
      const EnvironmentAssetPackageProvenance& provenance) const
      -> contracts::Result<AcousticFieldPackageMetadata>;

  [[nodiscard]] auto Load(const EnvironmentAssetId& asset_id) const
      -> contracts::Result<std::shared_ptr<const AcousticFieldAsset>>;

  [[nodiscard]] auto Find(const EnvironmentAssetId& asset_id) const
      -> contracts::Result<EnvironmentAssetRecord>;

  [[nodiscard]] auto List() const
      -> contracts::Result<std::vector<EnvironmentAssetId>>;

 private:
  explicit EnvironmentAssetRepository(std::filesystem::path root)
      : root_(std::move(root)) {}

  [[nodiscard]] auto PackageDirectory(
      const EnvironmentAssetId& asset_id) const -> std::filesystem::path {
    return root_ / asset_id.value();
  }

  [[nodiscard]] auto TemporaryPackageDirectory(
      const EnvironmentAssetId& asset_id) const -> std::filesystem::path {
    return root_ / (".register-" + asset_id.value() + ".tmp");
  }

  std::filesystem::path root_;
};

inline auto EnvironmentAssetId::Create(std::string value)
    -> contracts::Result<EnvironmentAssetId> {
  if(value.empty() || value == "." || value == "..") {
    return package_detail::PackageError(
        contracts::ErrorCode::kInvalidArgument,
        "Environment asset ID must not be empty or a dot component");
  }
  for(const auto character : value) {
    const auto is_ascii_alphanumeric =
        (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9');
    if(!(is_ascii_alphanumeric || character == '-' || character == '_' ||
         character == '.')) {
      return package_detail::PackageError(
          contracts::ErrorCode::kInvalidArgument,
          "Environment asset ID contains a path separator or invalid byte");
    }
  }
  const auto first = value.front();
  const auto first_is_ascii_alphanumeric =
      (first >= 'a' && first <= 'z') ||
      (first >= 'A' && first <= 'Z') ||
      (first >= '0' && first <= '9');
  if(!first_is_ascii_alphanumeric) {
    return package_detail::PackageError(
        contracts::ErrorCode::kInvalidArgument,
        "Environment asset ID must begin with an alphanumeric byte");
  }
  return EnvironmentAssetId{std::move(value)};
}

inline auto EnvironmentAssetRepository::Open(std::filesystem::path root)
    -> contracts::Result<EnvironmentAssetRepository> {
  if(root.empty()) {
    return package_detail::PackageError(
        contracts::ErrorCode::kInvalidArgument,
        "Environment asset repository root must be explicit");
  }
  std::error_code error;
  if(!std::filesystem::is_directory(root, error) || error) {
    return package_detail::PackageError(
        contracts::ErrorCode::kNotFound,
        "Environment asset repository root is not an existing directory");
  }
  return EnvironmentAssetRepository{std::move(root)};
}

inline auto EnvironmentAssetRepository::Register(
    const EnvironmentAssetId& asset_id,
    const AcousticFieldAsset& asset,
    const EnvironmentAssetPackageProvenance& provenance) const
    -> contracts::Result<AcousticFieldPackageMetadata> {
  const auto package_directory = PackageDirectory(asset_id);
  const auto temporary_directory = TemporaryPackageDirectory(asset_id);
  std::error_code error;
  if(std::filesystem::exists(package_directory, error)) {
    return package_detail::PackageError(
        contracts::ErrorCode::kAlreadyExists,
        "Environment asset ID is already registered");
  }
  if(error) {
    return package_detail::PackageError(
        contracts::ErrorCode::kUnavailable,
        "Cannot inspect environment asset repository");
  }
  if(std::filesystem::exists(temporary_directory, error)) {
    return package_detail::PackageError(
        contracts::ErrorCode::kFailedPrecondition,
        "Environment asset registration temporary path already exists");
  }
  if(error) {
    return package_detail::PackageError(
        contracts::ErrorCode::kUnavailable,
        "Cannot inspect environment asset registration temporary path");
  }

  const auto cleanup_temporary = [&temporary_directory] {
    std::error_code ignored;
    std::filesystem::remove_all(temporary_directory, ignored);
  };
  auto written = AcousticFieldAssetWriter::Write(
      temporary_directory, asset, provenance);
  if(!written) {
    cleanup_temporary();
    return std::unexpected(written.error());
  }
  auto validated = AcousticFieldAssetLoader::Load(temporary_directory);
  if(!validated) {
    cleanup_temporary();
    return std::unexpected(validated.error());
  }
  if(validated->metadata != *written) {
    cleanup_temporary();
    return package_detail::PackageError(
        contracts::ErrorCode::kFailedPrecondition,
        "Environment asset registration validation changed package metadata");
  }

  std::filesystem::rename(temporary_directory, package_directory, error);
  if(error) {
    cleanup_temporary();
    std::error_code exists_error;
    if(std::filesystem::exists(package_directory, exists_error) &&
       !exists_error) {
      return package_detail::PackageError(
          contracts::ErrorCode::kAlreadyExists,
          "Environment asset ID became registered during publish");
    }
    return package_detail::PackageError(
        contracts::ErrorCode::kUnavailable,
        "Cannot publish validated environment asset package");
  }
  return std::move(validated->metadata);
}

inline auto EnvironmentAssetRepository::Load(
    const EnvironmentAssetId& asset_id) const
    -> contracts::Result<std::shared_ptr<const AcousticFieldAsset>> {
  auto package = AcousticFieldAssetLoader::Load(PackageDirectory(asset_id));
  if(!package) return std::unexpected(package.error());
  return std::move(package->asset);
}

inline auto EnvironmentAssetRepository::Find(
    const EnvironmentAssetId& asset_id) const
    -> contracts::Result<EnvironmentAssetRecord> {
  auto package = AcousticFieldAssetLoader::Load(PackageDirectory(asset_id));
  if(!package) return std::unexpected(package.error());
  return EnvironmentAssetRecord{
      asset_id, std::move(package->metadata), std::move(package->asset)};
}

inline auto EnvironmentAssetRepository::List() const
    -> contracts::Result<std::vector<EnvironmentAssetId>> {
  std::vector<EnvironmentAssetId> asset_ids;
  std::error_code error;
  for(std::filesystem::directory_iterator iterator{root_, error};
      !error && iterator != std::filesystem::directory_iterator{};
      iterator.increment(error)) {
    if(!iterator->is_directory(error) || error) {
      return package_detail::PackageError(
          contracts::ErrorCode::kFailedPrecondition,
          "Environment asset repository contains a non-package entry");
    }
    auto asset_id =
        EnvironmentAssetId::Create(iterator->path().filename().string());
    if(!asset_id) {
      return package_detail::PackageError(
          contracts::ErrorCode::kFailedPrecondition,
          "Environment asset repository contains an invalid asset ID");
    }
    auto package = AcousticFieldAssetLoader::Load(iterator->path());
    if(!package) return std::unexpected(package.error());
    asset_ids.push_back(std::move(*asset_id));
  }
  if(error) {
    return package_detail::PackageError(
        contracts::ErrorCode::kUnavailable,
        "Cannot enumerate environment asset repository");
  }
  std::sort(asset_ids.begin(), asset_ids.end());
  return asset_ids;
}

}  // namespace ns3_factory::environment::internal
