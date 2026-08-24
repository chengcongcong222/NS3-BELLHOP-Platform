#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>

#include "internal/acoustic_field_asset.hpp"
#include "internal/environment_asset_package.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::environment::internal;

namespace {

class TemporaryDirectory final {
 public:
  explicit TemporaryDirectory(std::string name)
      : path_(std::filesystem::temp_directory_path() / std::move(name)) {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
    std::filesystem::create_directory(path_);
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] auto path() const -> const std::filesystem::path& {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

auto FixtureProvenance() -> Result<EnvironmentAssetPackageProvenance> {
  return EnvironmentAssetPackageProvenance::Create(
      EnvironmentAssetProducerType::kBellhopRawImport,
      "platform=test=build",
      "synthetic=two-cell=Bellhop fixture",
      "synthetic=fixture.arr",
      "bellhop=normalization=v1");
}

auto FixtureAsset() -> Result<AcousticFieldAsset> {
  auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  auto first = PropagationPath::Create(
      SimDuration::Zero(), 0.25, -0.5);
  auto second = PropagationPath::Create(
      SimDuration::FromNanoseconds(25), 0.125, 1.25);
  if(!frame || !first || !second) return std::unexpected(Error{});
  return AcousticFieldAsset::Create(
      7U,
      "asset fixture provenance",
      *frame,
      {12'000.0},
      {10.0},
      {20.0},
      {100.0, 200.0},
      {AcousticFieldSignalCell{
           42.5,
           SimDuration::FromNanoseconds(3'350'000),
           {*second, *first}},
       AcousticFieldNoArrivalCell{}});
}

auto ReadBytes(const std::filesystem::path& path)
    -> std::vector<std::byte> {
  std::ifstream input{path, std::ios::binary};
  std::vector<std::byte> bytes;
  for(char character = 0; input.get(character);) {
    bytes.push_back(static_cast<std::byte>(character));
  }
  return bytes;
}

auto ReadText(const std::filesystem::path& path) -> std::string {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

auto WriteText(const std::filesystem::path& path, std::string_view value)
    -> bool {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
  return static_cast<bool>(output);
}

auto WriteBytes(const std::filesystem::path& path,
                std::span<const std::byte> value) -> bool {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  output.write(reinterpret_cast<const char*>(value.data()),
               static_cast<std::streamsize>(value.size()));
  return static_cast<bool>(output);
}

auto ReplaceManifestValue(std::string& manifest,
                          std::string_view key,
                          std::string_view value) -> bool {
  std::string prefix{key};
  prefix.push_back('=');
  const auto start = manifest.find(prefix);
  if(start == std::string::npos) return false;
  const auto value_start = start + prefix.size();
  const auto end = manifest.find('\n', value_start);
  if(end == std::string::npos) return false;
  manifest.replace(value_start, end - value_start, value);
  return true;
}

auto AssetsEqual(const AcousticFieldAsset& lhs,
                 const AcousticFieldAsset& rhs) -> bool {
  return lhs.format_version() == rhs.format_version() &&
         lhs.provenance() == rhs.provenance() &&
         lhs.coordinate_frame() == rhs.coordinate_frame() &&
         std::ranges::equal(lhs.frequency_hz(), rhs.frequency_hz()) &&
         std::ranges::equal(lhs.source_depth_m(), rhs.source_depth_m()) &&
         std::ranges::equal(lhs.receiver_depth_m(), rhs.receiver_depth_m()) &&
         std::ranges::equal(lhs.horizontal_range_m(),
                            rhs.horizontal_range_m()) &&
         std::ranges::equal(lhs.cells(), rhs.cells());
}

auto TestRoundTripAndDeterministicBytes() -> bool {
  TemporaryDirectory temporary{"ns3_factory_asset_package_round_trip"};
  auto asset = FixtureAsset();
  auto provenance = FixtureProvenance();
  if(!asset || !provenance) return false;
  const auto first_path = temporary.path() / "first";
  const auto second_path = temporary.path() / "second";
  auto first_metadata =
      AcousticFieldAssetWriter::Write(first_path, *asset, *provenance);
  auto second_metadata =
      AcousticFieldAssetWriter::Write(second_path, *asset, *provenance);
  auto loaded = AcousticFieldAssetLoader::Load(first_path);
  if(!first_metadata || !second_metadata || !loaded) return false;
  return *first_metadata == *second_metadata &&
         loaded->metadata == *first_metadata &&
         AssetsEqual(*asset, *loaded->asset) &&
         ReadText(first_path / "manifest.txt") ==
             ReadText(second_path / "manifest.txt") &&
         ReadBytes(first_path / "field.bin") ==
             ReadBytes(second_path / "field.bin") &&
         first_metadata->signal_cell_count == 1U &&
         first_metadata->no_arrival_cell_count == 1U;
}

auto TestManifestCorruption() -> bool {
  TemporaryDirectory temporary{"ns3_factory_asset_manifest_corruption"};
  auto asset = FixtureAsset();
  auto provenance = FixtureProvenance();
  if(!asset || !provenance) return false;

  const auto bad_magic = temporary.path() / "bad-magic";
  const auto bad_version = temporary.path() / "bad-version";
  const auto truncated = temporary.path() / "truncated";
  if(!AcousticFieldAssetWriter::Write(bad_magic, *asset, *provenance) ||
     !AcousticFieldAssetWriter::Write(bad_version, *asset, *provenance) ||
     !AcousticFieldAssetWriter::Write(truncated, *asset, *provenance)) {
    return false;
  }
  auto magic_text = ReadText(bad_magic / "manifest.txt");
  magic_text.replace(7U, kAcousticFieldPackageFormat.size(), "BAD_FORMAT");
  if(!WriteText(bad_magic / "manifest.txt", magic_text)) return false;

  auto version_text = ReadText(bad_version / "manifest.txt");
  const auto version_position = version_text.find("version=1\n");
  if(version_position == std::string::npos) return false;
  version_text.replace(version_position, 10U, "version=99\n");
  if(!WriteText(bad_version / "manifest.txt", version_text)) return false;

  auto truncated_text = ReadText(truncated / "manifest.txt");
  truncated_text.pop_back();
  if(!WriteText(truncated / "manifest.txt", truncated_text)) return false;

  const auto bad_magic_load = AcousticFieldAssetLoader::Load(bad_magic);
  const auto bad_version_load = AcousticFieldAssetLoader::Load(bad_version);
  const auto truncated_load = AcousticFieldAssetLoader::Load(truncated);
  return !bad_magic_load && !bad_version_load && !truncated_load &&
         bad_version_load.error().code == ErrorCode::kUnsupported;
}

template <typename Unsigned>
auto SetLittleEndian(std::vector<std::byte>& bytes,
                     std::size_t offset,
                     Unsigned value) -> void {
  for(std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
  }
}

auto TestPayloadStructuralCorruption() -> bool {
  TemporaryDirectory temporary{"ns3_factory_asset_payload_corruption"};
  auto asset = FixtureAsset();
  auto provenance = FixtureProvenance();
  if(!asset || !provenance) return false;
  auto payload = package_detail::SerializePayload(*asset);
  if(!payload) return false;

  constexpr auto kFixedHeaderBeforeProvenance = 16U;
  const auto provenance_bytes = asset->provenance().size();
  const auto count_offset =
      kFixedHeaderBeforeProvenance + 8U + provenance_bytes + 8U + 1U;
  constexpr auto kFourCountsBytes = 4U * sizeof(std::uint64_t);
  const auto axes_bytes =
      (asset->frequency_hz().size() + asset->source_depth_m().size() +
       asset->receiver_depth_m().size() +
       asset->horizontal_range_m().size()) *
      sizeof(double);
  const auto cell_count_offset = count_offset + kFourCountsBytes + axes_bytes;
  const auto first_cell_kind_offset = cell_count_offset + 8U;
  const auto first_path_count_offset =
      first_cell_kind_offset + 1U + 8U + 8U;
  const auto first_path_offset = first_path_count_offset + 8U;

  auto bad_magic = *payload;
  bad_magic.front() = std::byte{0U};
  auto bad_axis_count = *payload;
  SetLittleEndian(bad_axis_count,
                  count_offset,
                  std::numeric_limits<std::uint64_t>::max());
  auto bad_cell_count = *payload;
  SetLittleEndian(bad_cell_count,
                  cell_count_offset,
                  std::numeric_limits<std::uint64_t>::max());
  auto bad_path_count = *payload;
  SetLittleEndian(bad_path_count,
                  first_path_count_offset,
                  std::numeric_limits<std::uint64_t>::max());
  auto bad_cell_kind = *payload;
  bad_cell_kind[first_cell_kind_offset] = std::byte{99U};
  auto bad_loss = *payload;
  SetLittleEndian(
      bad_loss,
      first_cell_kind_offset + 1U,
      std::bit_cast<std::uint64_t>(
          std::numeric_limits<double>::infinity()));
  auto bad_path = *payload;
  SetLittleEndian(
      bad_path,
      first_path_offset + 8U,
      std::bit_cast<std::uint64_t>(-1.0));
  auto truncated = *payload;
  truncated.pop_back();
  auto extra = *payload;
  extra.push_back(std::byte{0U});

  const auto loader_rejects = [&](std::string_view name,
                                  const std::vector<std::byte>& corrupted) {
    const auto package = temporary.path() / name;
    if(!AcousticFieldAssetWriter::Write(package, *asset, *provenance)) {
      return false;
    }
    auto manifest = ReadText(package / "manifest.txt");
    if(!ReplaceManifestValue(
           manifest,
           "payload_bytes",
           package_detail::Decimal(corrupted.size())) ||
       !ReplaceManifestValue(
           manifest,
           "payload_checksum",
           package_detail::ChecksumHex(
               package_detail::Fnv1a64(corrupted))) ||
       !WriteBytes(package / "field.bin", corrupted) ||
       !WriteText(package / "manifest.txt", manifest)) {
      return false;
    }
    return !AcousticFieldAssetLoader::Load(package);
  };

  return loader_rejects("bad-magic", bad_magic) &&
         loader_rejects("bad-axis-count", bad_axis_count) &&
         loader_rejects("bad-cell-count", bad_cell_count) &&
         loader_rejects("bad-path-count", bad_path_count) &&
         loader_rejects("bad-cell-kind", bad_cell_kind) &&
         loader_rejects("bad-loss", bad_loss) &&
         loader_rejects("bad-path", bad_path) &&
         loader_rejects("truncated", truncated) &&
         loader_rejects("extra", extra);
}

auto TestProvenanceValidation() -> bool {
  const auto missing_bellhop_fields =
      EnvironmentAssetPackageProvenance::Create(
          EnvironmentAssetProducerType::kBellhopRawImport,
          "build",
          "source",
          "",
          "");
  const auto absolute_source =
      EnvironmentAssetPackageProvenance::Create(
          EnvironmentAssetProducerType::kBellhopRawImport,
          "build",
          "source",
          "/home/user/input.arr",
          "policy-v1");
  const auto multiline = EnvironmentAssetPackageProvenance::Create(
      EnvironmentAssetProducerType::kManual,
      "build\nother",
      "source",
      "",
      "");
  return !missing_bellhop_fields && !absolute_source && !multiline;
}

auto TestChecksumMismatchAndUnexpectedEntry() -> bool {
  TemporaryDirectory temporary{"ns3_factory_asset_checksum_corruption"};
  auto asset = FixtureAsset();
  auto provenance = FixtureProvenance();
  if(!asset || !provenance) return false;
  const auto checksum_path = temporary.path() / "checksum";
  const auto extra_path = temporary.path() / "extra";
  if(!AcousticFieldAssetWriter::Write(checksum_path, *asset, *provenance) ||
     !AcousticFieldAssetWriter::Write(extra_path, *asset, *provenance)) {
    return false;
  }
  auto payload = ReadBytes(checksum_path / "field.bin");
  if(payload.empty()) return false;
  payload.back() ^= std::byte{1U};
  std::ofstream output{checksum_path / "field.bin",
                       std::ios::binary | std::ios::trunc};
  output.write(reinterpret_cast<const char*>(payload.data()),
               static_cast<std::streamsize>(payload.size()));
  output.close();
  if(!output) return false;
  if(!WriteText(extra_path / "unexpected.tmp", "unexpected")) return false;

  const auto checksum_load = AcousticFieldAssetLoader::Load(checksum_path);
  const auto extra_load = AcousticFieldAssetLoader::Load(extra_path);
  return !checksum_load && !extra_load;
}

auto TestWriterRejectsExistingPackage() -> bool {
  TemporaryDirectory temporary{"ns3_factory_asset_existing_package"};
  auto asset = FixtureAsset();
  auto provenance = FixtureProvenance();
  if(!asset || !provenance) return false;
  const auto package = temporary.path() / "asset";
  const auto first =
      AcousticFieldAssetWriter::Write(package, *asset, *provenance);
  const auto duplicate =
      AcousticFieldAssetWriter::Write(package, *asset, *provenance);
  return first && !duplicate &&
         duplicate.error().code == ErrorCode::kAlreadyExists;
}

}  // namespace

auto main() -> int {
  return TestRoundTripAndDeterministicBytes() &&
                 TestManifestCorruption() &&
                 TestPayloadStructuralCorruption() &&
                 TestChecksumMismatchAndUnexpectedEntry() &&
                 TestWriterRejectsExistingPackage() &&
                 TestProvenanceValidation()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
