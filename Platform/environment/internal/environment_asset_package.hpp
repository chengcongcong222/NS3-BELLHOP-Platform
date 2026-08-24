#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/time.hpp>

#include "acoustic_field_asset.hpp"

namespace ns3_factory::environment::internal {

inline constexpr std::string_view kAcousticFieldPackageFormat =
    "NS3_FACTORY_ACOUSTIC_FIELD";
inline constexpr std::uint32_t kAcousticFieldPackageVersion = 1U;
inline constexpr std::string_view kAcousticFieldPayloadChecksumAlgorithm =
    "FNV1A64";

enum class EnvironmentAssetProducerType : std::uint8_t {
  kBellhopRawImport = 1,
  kManual = 2,
  kMeasured = 3,
  kFutureModel = 4,
};

class EnvironmentAssetPackageProvenance final {
 public:
  [[nodiscard]] static auto Create(
      EnvironmentAssetProducerType producer_type,
      std::string created_by_build_version,
      std::string source_description,
      std::string raw_source_logical_name,
      std::string normalization_policy_version)
      -> contracts::Result<EnvironmentAssetPackageProvenance>;

  [[nodiscard]] constexpr auto producer_type() const noexcept
      -> EnvironmentAssetProducerType {
    return producer_type_;
  }

  [[nodiscard]] constexpr auto created_by_build_version() const noexcept
      -> const std::string& {
    return created_by_build_version_;
  }

  [[nodiscard]] constexpr auto source_description() const noexcept
      -> const std::string& {
    return source_description_;
  }

  [[nodiscard]] constexpr auto raw_source_logical_name() const noexcept
      -> const std::string& {
    return raw_source_logical_name_;
  }

  [[nodiscard]] constexpr auto normalization_policy_version() const noexcept
      -> const std::string& {
    return normalization_policy_version_;
  }

  auto operator==(const EnvironmentAssetPackageProvenance&) const
      -> bool = default;

 private:
  EnvironmentAssetPackageProvenance(
      EnvironmentAssetProducerType producer_type,
      std::string created_by_build_version,
      std::string source_description,
      std::string raw_source_logical_name,
      std::string normalization_policy_version)
      : producer_type_(producer_type),
        created_by_build_version_(std::move(created_by_build_version)),
        source_description_(std::move(source_description)),
        raw_source_logical_name_(std::move(raw_source_logical_name)),
        normalization_policy_version_(
            std::move(normalization_policy_version)) {}

  EnvironmentAssetProducerType producer_type_;
  std::string created_by_build_version_;
  std::string source_description_;
  std::string raw_source_logical_name_;
  std::string normalization_policy_version_;
};

struct AcousticFieldPackageMetadata final {
  std::uint32_t package_format_version;
  std::uint32_t asset_format_version;
  EnvironmentAssetPackageProvenance provenance;
  EnvironmentCoordinateFrame coordinate_frame;
  std::uint64_t frequency_count;
  std::uint64_t source_depth_count;
  std::uint64_t receiver_depth_count;
  std::uint64_t range_count;
  std::uint64_t cell_count;
  std::uint64_t signal_cell_count;
  std::uint64_t no_arrival_cell_count;
  std::uint64_t payload_bytes;
  std::uint64_t payload_checksum;

  auto operator==(const AcousticFieldPackageMetadata&) const -> bool = default;
};

struct LoadedAcousticFieldPackage final {
  AcousticFieldPackageMetadata metadata;
  std::shared_ptr<const AcousticFieldAsset> asset;
};

class AcousticFieldAssetWriter final {
 public:
  [[nodiscard]] static auto Write(
      const std::filesystem::path& package_directory,
      const AcousticFieldAsset& asset,
      const EnvironmentAssetPackageProvenance& provenance)
      -> contracts::Result<AcousticFieldPackageMetadata>;
};

class AcousticFieldAssetLoader final {
 public:
  [[nodiscard]] static auto Load(
      const std::filesystem::path& package_directory)
      -> contracts::Result<LoadedAcousticFieldPackage>;
};

namespace package_detail {

inline constexpr std::array<std::byte, 8U> kPayloadMagic{
    std::byte{'N'}, std::byte{'S'}, std::byte{'3'}, std::byte{'A'},
    std::byte{'F'}, std::byte{'B'}, std::byte{'0'}, std::byte{'1'}};

[[nodiscard]] inline auto PackageError(contracts::ErrorCode code,
                                       std::string message)
    -> std::unexpected<contracts::Error> {
  return std::unexpected(contracts::Error{code, std::move(message)});
}

[[nodiscard]] inline auto IsKnownProducer(
    EnvironmentAssetProducerType producer_type) noexcept -> bool {
  switch(producer_type) {
    case EnvironmentAssetProducerType::kBellhopRawImport:
    case EnvironmentAssetProducerType::kManual:
    case EnvironmentAssetProducerType::kMeasured:
    case EnvironmentAssetProducerType::kFutureModel:
      return true;
  }
  return false;
}

[[nodiscard]] inline auto IsSingleLine(std::string_view value) noexcept
    -> bool {
  for(const auto character : value) {
    if(character == '\n' || character == '\r' || character == '\0') {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline auto ProducerName(
    EnvironmentAssetProducerType producer_type) -> std::string_view {
  switch(producer_type) {
    case EnvironmentAssetProducerType::kBellhopRawImport:
      return "BellhopRawImport";
    case EnvironmentAssetProducerType::kManual:
      return "Manual";
    case EnvironmentAssetProducerType::kMeasured:
      return "Measured";
    case EnvironmentAssetProducerType::kFutureModel:
      return "FutureModel";
  }
  return {};
}

[[nodiscard]] inline auto ParseProducer(std::string_view value)
    -> std::optional<EnvironmentAssetProducerType> {
  if(value == "BellhopRawImport") {
    return EnvironmentAssetProducerType::kBellhopRawImport;
  }
  if(value == "Manual") return EnvironmentAssetProducerType::kManual;
  if(value == "Measured") return EnvironmentAssetProducerType::kMeasured;
  if(value == "FutureModel") {
    return EnvironmentAssetProducerType::kFutureModel;
  }
  return std::nullopt;
}

[[nodiscard]] inline auto DirectionName(
    VerticalAxisDirection direction) -> std::string_view {
  return direction == VerticalAxisDirection::kPositiveUp ? "PositiveUp"
                                                          : "PositiveDown";
}

[[nodiscard]] inline auto ParseDirection(std::string_view value)
    -> std::optional<VerticalAxisDirection> {
  if(value == "PositiveUp") return VerticalAxisDirection::kPositiveUp;
  if(value == "PositiveDown") {
    return VerticalAxisDirection::kPositiveDown;
  }
  return std::nullopt;
}

template <typename Integer>
[[nodiscard]] inline auto Decimal(Integer value) -> std::string {
  std::array<char, 32U> buffer{};
  const auto result = std::to_chars(buffer.data(),
                                    buffer.data() + buffer.size(),
                                    value);
  return std::string{buffer.data(), result.ptr};
}

[[nodiscard]] inline auto CanonicalDouble(double value) -> std::string {
  std::array<char, 64U> buffer{};
  const auto result = std::to_chars(buffer.data(),
                                    buffer.data() + buffer.size(),
                                    value,
                                    std::chars_format::general,
                                    std::numeric_limits<double>::max_digits10);
  return std::string{buffer.data(), result.ptr};
}

[[nodiscard]] inline auto ChecksumHex(std::uint64_t checksum)
    -> std::string {
  constexpr auto kHex = std::string_view{"0123456789abcdef"};
  std::string result(16U, '0');
  for(std::size_t index = 0U; index < result.size(); ++index) {
    const auto shift = static_cast<unsigned>((15U - index) * 4U);
    result[index] = kHex[(checksum >> shift) & 0xFU];
  }
  return result;
}

template <typename Unsigned>
inline auto AppendLittleEndian(std::vector<std::byte>& output,
                               Unsigned value) -> void {
  static_assert(std::is_unsigned_v<Unsigned>);
  for(std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
    output.push_back(
        static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
  }
}

inline auto AppendSigned64(std::vector<std::byte>& output,
                           std::int64_t value) -> void {
  AppendLittleEndian(output, std::bit_cast<std::uint64_t>(value));
}

inline auto AppendDouble(std::vector<std::byte>& output, double value)
    -> void {
  AppendLittleEndian(output, std::bit_cast<std::uint64_t>(value));
}

inline auto AppendString(std::vector<std::byte>& output,
                         std::string_view value) -> void {
  AppendLittleEndian(output, static_cast<std::uint64_t>(value.size()));
  for(const auto character : value) {
    output.push_back(static_cast<std::byte>(character));
  }
}

[[nodiscard]] inline auto Fnv1a64(std::span<const std::byte> bytes) noexcept
    -> std::uint64_t {
  std::uint64_t checksum = 14695981039346656037ULL;
  for(const auto byte : bytes) {
    checksum ^= std::to_integer<std::uint8_t>(byte);
    checksum *= 1099511628211ULL;
  }
  return checksum;
}

[[nodiscard]] inline auto ToUint64(std::size_t value)
    -> contracts::Result<std::uint64_t> {
  if constexpr(sizeof(std::size_t) > sizeof(std::uint64_t)) {
    if(value > std::numeric_limits<std::uint64_t>::max()) {
      return PackageError(contracts::ErrorCode::kOverflow,
                          "Asset count cannot be represented as uint64");
    }
  }
  return static_cast<std::uint64_t>(value);
}

[[nodiscard]] inline auto SerializePayload(const AcousticFieldAsset& asset)
    -> contracts::Result<std::vector<std::byte>> {
  if(!std::numeric_limits<double>::is_iec559 || sizeof(double) != 8U) {
    return PackageError(contracts::ErrorCode::kUnsupported,
                        "Asset packages require IEEE-754 binary64 double");
  }

  const auto frequency_count = ToUint64(asset.frequency_hz().size());
  const auto source_count = ToUint64(asset.source_depth_m().size());
  const auto receiver_count = ToUint64(asset.receiver_depth_m().size());
  const auto range_count = ToUint64(asset.horizontal_range_m().size());
  const auto cell_count = ToUint64(asset.cells().size());
  if(!frequency_count || !source_count || !receiver_count ||
     !range_count || !cell_count) {
    return PackageError(contracts::ErrorCode::kOverflow,
                        "Asset dimensions cannot be represented as uint64");
  }

  std::vector<std::byte> payload;
  payload.insert(payload.end(), kPayloadMagic.begin(), kPayloadMagic.end());
  AppendLittleEndian(payload, kAcousticFieldPackageVersion);
  AppendLittleEndian(payload, asset.format_version());
  AppendString(payload, asset.provenance());
  AppendDouble(payload, asset.coordinate_frame().surface_z_meters());
  payload.push_back(static_cast<std::byte>(
      asset.coordinate_frame().vertical_direction()));
  AppendLittleEndian(payload, *frequency_count);
  AppendLittleEndian(payload, *source_count);
  AppendLittleEndian(payload, *receiver_count);
  AppendLittleEndian(payload, *range_count);

  const auto append_axis = [&payload](std::span<const double> axis) {
    for(const auto value : axis) AppendDouble(payload, value);
  };
  append_axis(asset.frequency_hz());
  append_axis(asset.source_depth_m());
  append_axis(asset.receiver_depth_m());
  append_axis(asset.horizontal_range_m());

  AppendLittleEndian(payload, *cell_count);
  for(const auto& cell : asset.cells()) {
    if(const auto* signal = std::get_if<AcousticFieldSignalCell>(&cell)) {
      payload.push_back(std::byte{1U});
      AppendDouble(payload, signal->aggregate_transmission_loss_db);
      AppendSigned64(payload, signal->first_arrival_delay.nanoseconds());
      const auto path_count = ToUint64(signal->paths.size());
      if(!path_count) return std::unexpected(path_count.error());
      AppendLittleEndian(payload, *path_count);
      for(const auto& path : signal->paths) {
        AppendSigned64(payload, path.excess_delay().nanoseconds());
        AppendDouble(payload, path.pressure_gain_linear());
        AppendDouble(payload, path.phase_radians());
      }
    } else {
      payload.push_back(std::byte{2U});
    }
  }
  return payload;
}

[[nodiscard]] inline auto CountCells(const AcousticFieldAsset& asset)
    -> std::pair<std::uint64_t, std::uint64_t> {
  std::uint64_t signal_count = 0U;
  std::uint64_t no_arrival_count = 0U;
  for(const auto& cell : asset.cells()) {
    if(std::holds_alternative<AcousticFieldSignalCell>(cell)) {
      ++signal_count;
    } else {
      ++no_arrival_count;
    }
  }
  return {signal_count, no_arrival_count};
}

[[nodiscard]] inline auto BuildMetadata(
    const AcousticFieldAsset& asset,
    const EnvironmentAssetPackageProvenance& provenance,
    std::uint64_t payload_bytes,
    std::uint64_t payload_checksum) -> AcousticFieldPackageMetadata {
  const auto [signal_count, no_arrival_count] = CountCells(asset);
  return AcousticFieldPackageMetadata{
      kAcousticFieldPackageVersion,
      asset.format_version(),
      provenance,
      asset.coordinate_frame(),
      static_cast<std::uint64_t>(asset.frequency_hz().size()),
      static_cast<std::uint64_t>(asset.source_depth_m().size()),
      static_cast<std::uint64_t>(asset.receiver_depth_m().size()),
      static_cast<std::uint64_t>(asset.horizontal_range_m().size()),
      static_cast<std::uint64_t>(asset.cells().size()),
      signal_count,
      no_arrival_count,
      payload_bytes,
      payload_checksum};
}

[[nodiscard]] inline auto BuildManifest(
    const AcousticFieldPackageMetadata& metadata) -> std::string {
  std::string manifest;
  const auto line = [&manifest](std::string_view key,
                                std::string_view value) {
    manifest.append(key);
    manifest.push_back('=');
    manifest.append(value);
    manifest.push_back('\n');
  };
  line("format", kAcousticFieldPackageFormat);
  line("version", Decimal(metadata.package_format_version));
  line("asset_format_version", Decimal(metadata.asset_format_version));
  line("producer", ProducerName(metadata.provenance.producer_type()));
  line("created_by_build_version",
       metadata.provenance.created_by_build_version());
  line("source_description", metadata.provenance.source_description());
  line("raw_source_logical_name",
       metadata.provenance.raw_source_logical_name());
  line("normalization_policy_version",
       metadata.provenance.normalization_policy_version());
  line("coordinate_surface_z_meters",
       CanonicalDouble(metadata.coordinate_frame.surface_z_meters()));
  line("coordinate_vertical_axis",
       DirectionName(metadata.coordinate_frame.vertical_direction()));
  line("frequency_axis_unit", "Hz");
  line("frequency_axis_count", Decimal(metadata.frequency_count));
  line("source_depth_axis_unit", "m");
  line("source_depth_axis_count", Decimal(metadata.source_depth_count));
  line("receiver_depth_axis_unit", "m");
  line("receiver_depth_axis_count", Decimal(metadata.receiver_depth_count));
  line("range_axis_unit", "m");
  line("range_axis_count", Decimal(metadata.range_count));
  line("cell_count", Decimal(metadata.cell_count));
  line("signal_cell_count", Decimal(metadata.signal_cell_count));
  line("no_arrival_cell_count", Decimal(metadata.no_arrival_cell_count));
  line("payload_bytes", Decimal(metadata.payload_bytes));
  line("payload_checksum_algorithm",
       kAcousticFieldPayloadChecksumAlgorithm);
  line("payload_checksum", ChecksumHex(metadata.payload_checksum));
  return manifest;
}

[[nodiscard]] inline auto WriteBytes(const std::filesystem::path& path,
                                     std::span<const std::byte> bytes)
    -> contracts::Status {
  if(bytes.size() >
     static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
    return PackageError(contracts::ErrorCode::kOverflow,
                        "Asset payload is too large for stream I/O");
  }
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  if(!output) {
    return PackageError(contracts::ErrorCode::kUnavailable,
                        "Cannot create asset package payload");
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  if(!output) {
    return PackageError(contracts::ErrorCode::kUnavailable,
                        "Cannot write complete asset package payload");
  }
  return {};
}

[[nodiscard]] inline auto WriteText(const std::filesystem::path& path,
                                    std::string_view text)
    -> contracts::Status {
  if(text.size() >
     static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
    return PackageError(contracts::ErrorCode::kOverflow,
                        "Asset manifest is too large for stream I/O");
  }
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  if(!output) {
    return PackageError(contracts::ErrorCode::kUnavailable,
                        "Cannot create asset package manifest");
  }
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  output.close();
  if(!output) {
    return PackageError(contracts::ErrorCode::kUnavailable,
                        "Cannot write complete asset package manifest");
  }
  return {};
}

[[nodiscard]] inline auto ReadFile(const std::filesystem::path& path)
    -> contracts::Result<std::vector<std::byte>> {
  std::error_code error;
  const auto file_size = std::filesystem::file_size(path, error);
  if(error) {
    return PackageError(contracts::ErrorCode::kNotFound,
                        "Asset package file is missing or unreadable");
  }
  if(file_size > std::numeric_limits<std::size_t>::max() ||
     file_size > static_cast<std::uintmax_t>(
                     std::numeric_limits<std::streamsize>::max())) {
    return PackageError(contracts::ErrorCode::kOverflow,
                        "Asset package file size is not representable");
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
  std::ifstream input{path, std::ios::binary};
  if(!input) {
    return PackageError(contracts::ErrorCode::kUnavailable,
                        "Cannot open asset package file");
  }
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if(input.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset package file is truncated");
  }
  return bytes;
}

[[nodiscard]] inline auto BytesAsString(std::span<const std::byte> bytes)
    -> std::string {
  return std::string{reinterpret_cast<const char*>(bytes.data()),
                     bytes.size()};
}

class PayloadReader final {
 public:
  explicit PayloadReader(std::span<const std::byte> bytes) noexcept
      : bytes_(bytes) {}

  [[nodiscard]] auto ReadByte() -> contracts::Result<std::uint8_t> {
    if(remaining() < 1U) return Truncated();
    return std::to_integer<std::uint8_t>(bytes_[offset_++]);
  }

  template <typename Unsigned>
  [[nodiscard]] auto ReadLittleEndian() -> contracts::Result<Unsigned> {
    static_assert(std::is_unsigned_v<Unsigned>);
    if(remaining() < sizeof(Unsigned)) return Truncated();
    Unsigned value = 0U;
    for(std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
      value |= static_cast<Unsigned>(
                   std::to_integer<std::uint8_t>(bytes_[offset_ + index]))
               << (index * 8U);
    }
    offset_ += sizeof(Unsigned);
    return value;
  }

  [[nodiscard]] auto ReadSigned64() -> contracts::Result<std::int64_t> {
    auto value = ReadLittleEndian<std::uint64_t>();
    if(!value) return std::unexpected(value.error());
    return std::bit_cast<std::int64_t>(*value);
  }

  [[nodiscard]] auto ReadDouble() -> contracts::Result<double> {
    auto value = ReadLittleEndian<std::uint64_t>();
    if(!value) return std::unexpected(value.error());
    return std::bit_cast<double>(*value);
  }

  [[nodiscard]] auto ReadString() -> contracts::Result<std::string> {
    auto size = ReadLittleEndian<std::uint64_t>();
    if(!size) return std::unexpected(size.error());
    if(*size > remaining() || *size > std::numeric_limits<std::size_t>::max()) {
      return PackageError(contracts::ErrorCode::kFailedPrecondition,
                          "Asset payload string length is invalid");
    }
    const auto count = static_cast<std::size_t>(*size);
    const auto* data = reinterpret_cast<const char*>(bytes_.data() + offset_);
    std::string value{data, count};
    offset_ += count;
    return value;
  }

  [[nodiscard]] auto ReadMagic() -> contracts::Status {
    if(remaining() < kPayloadMagic.size()) return Truncated();
    for(std::size_t index = 0U; index < kPayloadMagic.size(); ++index) {
      if(bytes_[offset_ + index] != kPayloadMagic[index]) {
        return PackageError(contracts::ErrorCode::kFailedPrecondition,
                            "Asset payload magic is invalid");
      }
    }
    offset_ += kPayloadMagic.size();
    return {};
  }

  [[nodiscard]] constexpr auto remaining() const noexcept -> std::size_t {
    return bytes_.size() - offset_;
  }

 private:
  [[nodiscard]] static auto Truncated() -> std::unexpected<contracts::Error> {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset package payload is truncated");
  }

  std::span<const std::byte> bytes_;
  std::size_t offset_{0U};
};

[[nodiscard]] inline auto ParseUnsigned(std::string_view value)
    -> contracts::Result<std::uint64_t> {
  std::uint64_t parsed = 0U;
  const auto result = std::from_chars(value.data(),
                                      value.data() + value.size(),
                                      parsed);
  if(result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
     value.empty()) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest integer is invalid");
  }
  return parsed;
}

[[nodiscard]] inline auto ParseDouble(std::string_view value)
    -> contracts::Result<double> {
  double parsed = 0.0;
  const auto result = std::from_chars(value.data(),
                                      value.data() + value.size(),
                                      parsed,
                                      std::chars_format::general);
  if(result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
     value.empty() || !std::isfinite(parsed)) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest floating value is invalid");
  }
  return parsed;
}

[[nodiscard]] inline auto ParseChecksum(std::string_view value)
    -> contracts::Result<std::uint64_t> {
  if(value.size() != 16U) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest checksum width is invalid");
  }
  std::uint64_t parsed = 0U;
  const auto result = std::from_chars(value.data(),
                                      value.data() + value.size(),
                                      parsed,
                                      16);
  if(result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest checksum is invalid");
  }
  return parsed;
}

[[nodiscard]] inline auto ManifestValues(std::string_view manifest)
    -> contracts::Result<std::vector<std::string_view>> {
  if(manifest.empty() || manifest.back() != '\n' ||
     manifest.find('\r') != std::string_view::npos ||
     manifest.find('\0') != std::string_view::npos) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest is truncated or non-canonical");
  }
  std::vector<std::string_view> lines;
  std::size_t start = 0U;
  while(start < manifest.size()) {
    const auto end = manifest.find('\n', start);
    lines.push_back(manifest.substr(start, end - start));
    start = end + 1U;
  }
  return lines;
}

[[nodiscard]] inline auto Field(std::span<const std::string_view> lines,
                                std::size_t index,
                                std::string_view key)
    -> contracts::Result<std::string_view> {
  if(index >= lines.size()) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest field is missing");
  }
  std::string expected{key};
  expected.push_back('=');
  if(!lines[index].starts_with(expected)) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest field order or name is invalid");
  }
  return lines[index].substr(expected.size());
}

[[nodiscard]] inline auto ParseManifest(std::string_view manifest)
    -> contracts::Result<AcousticFieldPackageMetadata> {
  auto lines = ManifestValues(manifest);
  if(!lines) return std::unexpected(lines.error());
  constexpr auto kFieldCount = 24U;
  if(lines->size() != kFieldCount) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest field count is invalid");
  }

  std::size_t index = 0U;
  const auto next = [&lines, &index](std::string_view key)
      -> contracts::Result<std::string_view> {
    return Field(*lines, index++, key);
  };
  auto format = next("format");
  auto version_text = next("version");
  auto asset_version_text = next("asset_format_version");
  auto producer_text = next("producer");
  auto build_version = next("created_by_build_version");
  auto source_description = next("source_description");
  auto raw_source = next("raw_source_logical_name");
  auto normalization = next("normalization_policy_version");
  auto surface_text = next("coordinate_surface_z_meters");
  auto direction_text = next("coordinate_vertical_axis");
  auto frequency_unit = next("frequency_axis_unit");
  auto frequency_count_text = next("frequency_axis_count");
  auto source_unit = next("source_depth_axis_unit");
  auto source_count_text = next("source_depth_axis_count");
  auto receiver_unit = next("receiver_depth_axis_unit");
  auto receiver_count_text = next("receiver_depth_axis_count");
  auto range_unit = next("range_axis_unit");
  auto range_count_text = next("range_axis_count");
  auto cell_count_text = next("cell_count");
  auto signal_count_text = next("signal_cell_count");
  auto no_arrival_count_text = next("no_arrival_cell_count");
  auto payload_bytes_text = next("payload_bytes");
  auto checksum_algorithm = next("payload_checksum_algorithm");
  auto checksum_text = next("payload_checksum");
  if(!format || !version_text || !asset_version_text || !producer_text ||
     !build_version || !source_description || !raw_source ||
     !normalization || !surface_text || !direction_text || !frequency_unit ||
     !frequency_count_text || !source_unit || !source_count_text ||
     !receiver_unit || !receiver_count_text || !range_unit ||
     !range_count_text || !cell_count_text || !signal_count_text ||
     !no_arrival_count_text || !payload_bytes_text || !checksum_algorithm ||
     !checksum_text) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest is incomplete");
  }
  if(*format != kAcousticFieldPackageFormat) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest format identifier is invalid");
  }
  if(*frequency_unit != "Hz" || *source_unit != "m" ||
     *receiver_unit != "m" || *range_unit != "m" ||
     *checksum_algorithm != kAcousticFieldPayloadChecksumAlgorithm) {
    return PackageError(contracts::ErrorCode::kUnsupported,
                        "Asset manifest units or checksum are unsupported");
  }

  auto version = ParseUnsigned(*version_text);
  auto asset_version = ParseUnsigned(*asset_version_text);
  auto surface = ParseDouble(*surface_text);
  auto frequency_count = ParseUnsigned(*frequency_count_text);
  auto source_count = ParseUnsigned(*source_count_text);
  auto receiver_count = ParseUnsigned(*receiver_count_text);
  auto range_count = ParseUnsigned(*range_count_text);
  auto cell_count = ParseUnsigned(*cell_count_text);
  auto signal_count = ParseUnsigned(*signal_count_text);
  auto no_arrival_count = ParseUnsigned(*no_arrival_count_text);
  auto payload_bytes = ParseUnsigned(*payload_bytes_text);
  auto checksum = ParseChecksum(*checksum_text);
  const auto producer = ParseProducer(*producer_text);
  const auto direction = ParseDirection(*direction_text);
  if(!version || !asset_version || !surface || !frequency_count ||
     !source_count || !receiver_count || !range_count || !cell_count ||
     !signal_count || !no_arrival_count || !payload_bytes || !checksum ||
     !producer || !direction) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest contains an invalid value");
  }
  if(*version != kAcousticFieldPackageVersion) {
    return PackageError(contracts::ErrorCode::kUnsupported,
                        "Asset package version is unsupported");
  }
  if(*asset_version > std::numeric_limits<std::uint32_t>::max()) {
    return PackageError(contracts::ErrorCode::kOverflow,
                        "Asset format version is outside uint32");
  }
  auto provenance = EnvironmentAssetPackageProvenance::Create(
      *producer,
      std::string{*build_version},
      std::string{*source_description},
      std::string{*raw_source},
      std::string{*normalization});
  auto frame = EnvironmentCoordinateFrame::Create(*surface, *direction);
  if(!provenance) return std::unexpected(provenance.error());
  if(!frame) return std::unexpected(frame.error());

  AcousticFieldPackageMetadata metadata{
      static_cast<std::uint32_t>(*version),
      static_cast<std::uint32_t>(*asset_version),
      std::move(*provenance),
      *frame,
      *frequency_count,
      *source_count,
      *receiver_count,
      *range_count,
      *cell_count,
      *signal_count,
      *no_arrival_count,
      *payload_bytes,
      *checksum};
  if(BuildManifest(metadata) != manifest) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest encoding is not canonical");
  }
  return metadata;
}

struct DecodedPayload final {
  std::uint32_t package_version;
  std::uint32_t asset_version;
  std::string asset_provenance;
  EnvironmentCoordinateFrame coordinate_frame;
  std::vector<double> frequency_hz;
  std::vector<double> source_depth_m;
  std::vector<double> receiver_depth_m;
  std::vector<double> range_m;
  std::vector<AcousticFieldCell> cells;
};

[[nodiscard]] inline auto CountFitsSize(std::uint64_t count) noexcept
    -> bool {
  return count <= std::numeric_limits<std::size_t>::max();
}

[[nodiscard]] inline auto ReadAxis(PayloadReader& reader,
                                   std::uint64_t count)
    -> contracts::Result<std::vector<double>> {
  if(!CountFitsSize(count) || count > reader.remaining() / sizeof(double)) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset payload axis count is invalid");
  }
  std::vector<double> axis;
  axis.reserve(static_cast<std::size_t>(count));
  for(std::uint64_t index = 0U; index < count; ++index) {
    auto value = reader.ReadDouble();
    if(!value) return std::unexpected(value.error());
    axis.push_back(*value);
  }
  return axis;
}

[[nodiscard]] inline auto DecodePayload(std::span<const std::byte> bytes)
    -> contracts::Result<DecodedPayload> {
  if(!std::numeric_limits<double>::is_iec559 || sizeof(double) != 8U) {
    return PackageError(contracts::ErrorCode::kUnsupported,
                        "Asset packages require IEEE-754 binary64 double");
  }
  PayloadReader reader{bytes};
  auto magic = reader.ReadMagic();
  auto package_version = reader.ReadLittleEndian<std::uint32_t>();
  auto asset_version = reader.ReadLittleEndian<std::uint32_t>();
  auto asset_provenance = reader.ReadString();
  auto surface = reader.ReadDouble();
  auto direction_byte = reader.ReadByte();
  auto frequency_count = reader.ReadLittleEndian<std::uint64_t>();
  auto source_count = reader.ReadLittleEndian<std::uint64_t>();
  auto receiver_count = reader.ReadLittleEndian<std::uint64_t>();
  auto range_count = reader.ReadLittleEndian<std::uint64_t>();
  if(!magic || !package_version || !asset_version || !asset_provenance ||
     !surface || !direction_byte || !frequency_count || !source_count ||
     !receiver_count || !range_count) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset payload header is truncated");
  }
  if(*package_version != kAcousticFieldPackageVersion) {
    return PackageError(contracts::ErrorCode::kUnsupported,
                        "Asset payload version is unsupported");
  }
  const auto direction =
      static_cast<VerticalAxisDirection>(*direction_byte);
  auto frame = EnvironmentCoordinateFrame::Create(*surface, direction);
  if(!frame) return std::unexpected(frame.error());

  auto frequency = ReadAxis(reader, *frequency_count);
  auto source = ReadAxis(reader, *source_count);
  auto receiver = ReadAxis(reader, *receiver_count);
  auto range = ReadAxis(reader, *range_count);
  if(!frequency || !source || !receiver || !range) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset payload axes are invalid or truncated");
  }
  auto cell_count = reader.ReadLittleEndian<std::uint64_t>();
  if(!cell_count || !CountFitsSize(*cell_count) ||
     *cell_count > reader.remaining()) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset payload cell count is invalid");
  }
  std::vector<AcousticFieldCell> cells;
  cells.reserve(static_cast<std::size_t>(*cell_count));
  for(std::uint64_t cell_index = 0U; cell_index < *cell_count; ++cell_index) {
    auto kind = reader.ReadByte();
    if(!kind) return std::unexpected(kind.error());
    if(*kind == 2U) {
      cells.emplace_back(AcousticFieldNoArrivalCell{});
      continue;
    }
    if(*kind != 1U) {
      return PackageError(contracts::ErrorCode::kFailedPrecondition,
                          "Asset payload cell kind is invalid");
    }
    auto loss = reader.ReadDouble();
    auto first_delay = reader.ReadSigned64();
    auto path_count = reader.ReadLittleEndian<std::uint64_t>();
    if(!loss || !first_delay || !path_count || !CountFitsSize(*path_count) ||
       *path_count > reader.remaining() / 24U) {
      return PackageError(contracts::ErrorCode::kFailedPrecondition,
                          "Asset payload signal cell is truncated");
    }
    std::vector<contracts::PropagationPath> paths;
    paths.reserve(static_cast<std::size_t>(*path_count));
    for(std::uint64_t path_index = 0U; path_index < *path_count;
        ++path_index) {
      auto excess = reader.ReadSigned64();
      auto gain = reader.ReadDouble();
      auto phase = reader.ReadDouble();
      if(!excess || !gain || !phase) {
        return PackageError(contracts::ErrorCode::kFailedPrecondition,
                            "Asset payload path is truncated");
      }
      auto path = contracts::PropagationPath::Create(
          contracts::SimDuration::FromNanoseconds(*excess), *gain, *phase);
      if(!path) return std::unexpected(path.error());
      paths.push_back(std::move(*path));
    }
    if(!std::is_sorted(paths.begin(), paths.end(), detail::CanonicalPathLess)) {
      return PackageError(contracts::ErrorCode::kFailedPrecondition,
                          "Asset payload paths are not canonical");
    }
    cells.emplace_back(AcousticFieldSignalCell{
        *loss,
        contracts::SimDuration::FromNanoseconds(*first_delay),
        std::move(paths)});
  }
  if(reader.remaining() != 0U) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset payload contains unexpected trailing bytes");
  }
  return DecodedPayload{*package_version,
                        *asset_version,
                        std::move(*asset_provenance),
                        *frame,
                        std::move(*frequency),
                        std::move(*source),
                        std::move(*receiver),
                        std::move(*range),
                        std::move(cells)};
}

[[nodiscard]] inline auto ValidateMetadataMatchesAsset(
    const AcousticFieldPackageMetadata& metadata,
    const AcousticFieldAsset& asset) -> contracts::Status {
  const auto expected = BuildMetadata(
      asset,
      metadata.provenance,
      metadata.payload_bytes,
      metadata.payload_checksum);
  if(metadata != expected) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset manifest metadata does not match payload");
  }
  return {};
}

[[nodiscard]] inline auto ValidatePackageEntries(
    const std::filesystem::path& package_directory) -> contracts::Status {
  std::error_code error;
  if(!std::filesystem::is_directory(package_directory, error) || error) {
    return PackageError(contracts::ErrorCode::kNotFound,
                        "Asset package directory does not exist");
  }
  std::size_t entry_count = 0U;
  for(std::filesystem::directory_iterator iterator{package_directory, error};
      !error && iterator != std::filesystem::directory_iterator{};
      iterator.increment(error)) {
    ++entry_count;
    const auto name = iterator->path().filename();
    if((name != "manifest.txt" && name != "field.bin") ||
       !iterator->is_regular_file(error) || error) {
      return PackageError(contracts::ErrorCode::kFailedPrecondition,
                          "Asset package contains an unexpected entry");
    }
  }
  if(error) {
    return PackageError(contracts::ErrorCode::kUnavailable,
                        "Cannot enumerate asset package directory");
  }
  if(entry_count != 2U) {
    return PackageError(contracts::ErrorCode::kFailedPrecondition,
                        "Asset package must contain exactly two files");
  }
  return {};
}

}  // namespace package_detail

inline auto EnvironmentAssetPackageProvenance::Create(
    EnvironmentAssetProducerType producer_type,
    std::string created_by_build_version,
    std::string source_description,
    std::string raw_source_logical_name,
    std::string normalization_policy_version)
    -> contracts::Result<EnvironmentAssetPackageProvenance> {
  if(!package_detail::IsKnownProducer(producer_type)) {
    return package_detail::PackageError(
        contracts::ErrorCode::kInvalidArgument,
        "Environment asset producer type is invalid");
  }
  for(const auto value : {std::string_view{created_by_build_version},
                          std::string_view{source_description},
                          std::string_view{raw_source_logical_name},
                          std::string_view{normalization_policy_version}}) {
    if(!package_detail::IsSingleLine(value)) {
      return package_detail::PackageError(
          contracts::ErrorCode::kInvalidArgument,
          "Environment asset provenance must use single-line values");
    }
  }
  if(source_description.empty()) {
    return package_detail::PackageError(
        contracts::ErrorCode::kInvalidArgument,
        "Environment asset source description must not be empty");
  }
  if(producer_type == EnvironmentAssetProducerType::kBellhopRawImport &&
     (raw_source_logical_name.empty() ||
      normalization_policy_version.empty())) {
    return package_detail::PackageError(
        contracts::ErrorCode::kInvalidArgument,
        "Bellhop asset provenance requires logical source and policy version");
  }
  if(!raw_source_logical_name.empty() &&
     std::filesystem::path{raw_source_logical_name}.is_absolute()) {
    return package_detail::PackageError(
        contracts::ErrorCode::kInvalidArgument,
        "Environment asset raw source must be a logical name, not an absolute path");
  }
  return EnvironmentAssetPackageProvenance{
      producer_type,
      std::move(created_by_build_version),
      std::move(source_description),
      std::move(raw_source_logical_name),
      std::move(normalization_policy_version)};
}

inline auto AcousticFieldAssetWriter::Write(
    const std::filesystem::path& package_directory,
    const AcousticFieldAsset& asset,
    const EnvironmentAssetPackageProvenance& provenance)
    -> contracts::Result<AcousticFieldPackageMetadata> {
  if(package_directory.empty()) {
    return package_detail::PackageError(
        contracts::ErrorCode::kInvalidArgument,
        "Asset package directory must not be empty");
  }
  auto payload = package_detail::SerializePayload(asset);
  if(!payload) return std::unexpected(payload.error());
  const auto payload_size = package_detail::ToUint64(payload->size());
  if(!payload_size) return std::unexpected(payload_size.error());
  const auto checksum = package_detail::Fnv1a64(*payload);
  const auto metadata = package_detail::BuildMetadata(
      asset, provenance, *payload_size, checksum);
  const auto manifest = package_detail::BuildManifest(metadata);

  std::error_code error;
  if(std::filesystem::exists(package_directory, error)) {
    return package_detail::PackageError(
        contracts::ErrorCode::kAlreadyExists,
        "Asset package directory already exists");
  }
  if(error || !std::filesystem::create_directory(package_directory, error) ||
     error) {
    return package_detail::PackageError(
        contracts::ErrorCode::kUnavailable,
        "Cannot create asset package directory");
  }
  const auto cleanup = [&package_directory] {
    std::error_code ignored;
    std::filesystem::remove_all(package_directory, ignored);
  };
  auto payload_write = package_detail::WriteBytes(
      package_directory / "field.bin", *payload);
  if(!payload_write) {
    cleanup();
    return std::unexpected(payload_write.error());
  }
  auto manifest_write = package_detail::WriteText(
      package_directory / "manifest.txt", manifest);
  if(!manifest_write) {
    cleanup();
    return std::unexpected(manifest_write.error());
  }
  return metadata;
}

inline auto AcousticFieldAssetLoader::Load(
    const std::filesystem::path& package_directory)
    -> contracts::Result<LoadedAcousticFieldPackage> {
  auto entries = package_detail::ValidatePackageEntries(package_directory);
  if(!entries) return std::unexpected(entries.error());
  auto manifest_bytes =
      package_detail::ReadFile(package_directory / "manifest.txt");
  if(!manifest_bytes) return std::unexpected(manifest_bytes.error());
  auto metadata = package_detail::ParseManifest(
      package_detail::BytesAsString(*manifest_bytes));
  if(!metadata) return std::unexpected(metadata.error());
  auto payload = package_detail::ReadFile(package_directory / "field.bin");
  if(!payload) return std::unexpected(payload.error());
  if(payload->size() != metadata->payload_bytes) {
    return package_detail::PackageError(
        contracts::ErrorCode::kFailedPrecondition,
        "Asset payload size does not match manifest");
  }
  if(package_detail::Fnv1a64(*payload) != metadata->payload_checksum) {
    return package_detail::PackageError(
        contracts::ErrorCode::kFailedPrecondition,
        "Asset payload checksum mismatch");
  }
  auto decoded = package_detail::DecodePayload(*payload);
  if(!decoded) return std::unexpected(decoded.error());
  if(decoded->package_version != metadata->package_format_version ||
     decoded->asset_version != metadata->asset_format_version) {
    return package_detail::PackageError(
        contracts::ErrorCode::kFailedPrecondition,
        "Asset payload versions do not match manifest");
  }
  auto asset = AcousticFieldAsset::Create(
      decoded->asset_version,
      std::move(decoded->asset_provenance),
      decoded->coordinate_frame,
      std::move(decoded->frequency_hz),
      std::move(decoded->source_depth_m),
      std::move(decoded->receiver_depth_m),
      std::move(decoded->range_m),
      std::move(decoded->cells));
  if(!asset) return std::unexpected(asset.error());
  auto metadata_validation =
      package_detail::ValidateMetadataMatchesAsset(*metadata, *asset);
  if(!metadata_validation) {
    return std::unexpected(metadata_validation.error());
  }
  return LoadedAcousticFieldPackage{
      std::move(*metadata),
      std::make_shared<const AcousticFieldAsset>(std::move(*asset))};
}

}  // namespace ns3_factory::environment::internal
