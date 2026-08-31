#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <variant>

#include <ns3_factory/contracts/channel.hpp>

#include "internal/acoustic_field_channel_provider.hpp"
#include "internal/discrete_frequency_selector.hpp"
#include "internal/reference_environment_asset.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::environment::internal;

namespace {

class TemporaryDirectory final {
 public:
  TemporaryDirectory()
      : path_(std::filesystem::temp_directory_path() /
              "ns3_factory_reference_environment_asset_test") {
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

auto MakeQuery(std::uint64_t id,
               double source_depth,
               double receiver_depth,
               double range,
               double frequency = 25'000.0) -> Result<ChannelQuery> {
  return ChannelQuery::Create(TransmissionId{id},
                              NodeId{10},
                              NodeId{99},
                              Position3d{0.0, 0.0, -source_depth},
                              Position3d{range, 0.0, -receiver_depth},
                              SimTime::Zero(),
                              frequency,
                              4'000.0);
}

auto TestImportAndNormalizedMetadata(const AcousticFieldAsset& asset)
    -> bool {
  std::size_t signal_count = 0U;
  std::size_t no_arrival_count = 0U;
  for(const auto& cell : asset.cells()) {
    if(std::holds_alternative<AcousticFieldSignalCell>(cell)) {
      ++signal_count;
    } else {
      ++no_arrival_count;
    }
  }
  return asset.format_version() == 1U &&
         asset.provenance().find("not field-measured") != std::string::npos &&
         asset.frequency_hz().size() == 1U &&
         asset.frequency_hz().front() == 25'000.0 &&
         asset.source_depth_m().size() == 5U &&
         asset.source_depth_m().front() == 8.0 &&
         asset.source_depth_m().back() == 75.0 &&
         asset.receiver_depth_m().size() == 5U &&
         asset.receiver_depth_m().front() == 8.0 &&
         asset.receiver_depth_m().back() == 75.0 &&
         asset.horizontal_range_m().size() == 26U &&
         asset.horizontal_range_m().front() == 0.0 &&
         asset.horizontal_range_m().back() == 2'500.0 &&
         asset.cells().size() == 650U && signal_count == 625U &&
         no_arrival_count == 25U;
}

auto TestDeterministicPackageAndRepository(const AcousticFieldAsset& asset)
    -> bool {
  TemporaryDirectory temporary;
  auto provenance = ReferenceShallowWaterV1Provenance();
  if(!provenance) return false;
  auto first = AcousticFieldAssetWriter::Write(
      temporary.path() / "first", asset, *provenance);
  auto second = AcousticFieldAssetWriter::Write(
      temporary.path() / "second", asset, *provenance);
  if(!first || !second || *first != *second ||
     !MatchesReferenceShallowWaterV1Metadata(*first)) {
    return false;
  }
  const auto repository_root = temporary.path() / "repository";
  if(!std::filesystem::create_directory(repository_root)) return false;
  auto repository = EnvironmentAssetRepository::Open(repository_root);
  auto asset_id = EnvironmentAssetId::Create(kReferenceShallowWaterV1AssetId);
  if(!repository || !asset_id) return false;
  auto registered = repository->Register(*asset_id, asset, *provenance);
  auto loaded = repository->Find(*asset_id);
  auto duplicate = repository->Register(*asset_id, asset, *provenance);
  return registered && loaded && !duplicate &&
         duplicate.error().code == ErrorCode::kAlreadyExists &&
         loaded->metadata == *registered &&
         MatchesReferenceShallowWaterV1Metadata(loaded->metadata) &&
         loaded->asset->cells().size() == 650U;
}

auto TestRepresentativeQueries(std::shared_ptr<const AcousticFieldAsset> asset)
    -> bool {
  auto policy = DiscreteFrequencySelectionPolicy::Create(0.0);
  auto provider = policy ? AcousticFieldChannelProvider::Create(
                               std::move(asset), *policy)
                         : Result<AcousticFieldChannelProvider>{
                               std::unexpected(policy.error())};
  if(!provider) return false;
  const double ranges[]{100.0, 800.0, 1'000.0, 2'500.0};
  for(std::size_t index = 0U; index < 4U; ++index) {
    auto query = MakeQuery(index + 1U, 60.0, 8.0, ranges[index]);
    auto outcome = query ? provider->Query(*query)
                         : Result<ChannelFieldOutcome>{
                               std::unexpected(query.error())};
    const auto* response = outcome
                               ? std::get_if<ChannelFieldResponse>(&*outcome)
                               : nullptr;
    if(response == nullptr ||
       !std::isfinite(response->aggregate_transmission_loss_db()) ||
       response->first_arrival_delay() <= SimDuration::Zero() ||
       response->paths().empty()) {
      return false;
    }
    std::cout << "range_m=" << ranges[index]
              << " tl_db=" << response->aggregate_transmission_loss_db()
              << " delay_ns=" << response->first_arrival_delay().nanoseconds()
              << " path_count=" << response->paths().size() << '\n';
  }

  auto no_arrival_query = MakeQuery(10U, 8.0, 8.0, 0.0);
  auto no_arrival = no_arrival_query
                        ? provider->Query(*no_arrival_query)
                        : Result<ChannelFieldOutcome>{
                              std::unexpected(no_arrival_query.error())};
  auto wrong_frequency_query = MakeQuery(11U, 60.0, 8.0, 1'000.0, 24'999.0);
  auto wrong_frequency = wrong_frequency_query
                             ? provider->Query(*wrong_frequency_query)
                             : Result<ChannelFieldOutcome>{std::unexpected(
                                   wrong_frequency_query.error())};
  return no_arrival &&
         std::holds_alternative<ChannelNoArrival>(*no_arrival) &&
         !wrong_frequency &&
         wrong_frequency.error().code ==
             ErrorCode::kOutOfRange;
}

}  // namespace

auto main() -> int {
  auto asset = BuildReferenceShallowWaterV1(
      std::filesystem::path{PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT});
  if(!asset || !TestImportAndNormalizedMetadata(*asset) ||
     !TestDeterministicPackageAndRepository(*asset)) {
    return EXIT_FAILURE;
  }
  return TestRepresentativeQueries(
             std::make_shared<const AcousticFieldAsset>(std::move(*asset)))
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
