#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/acoustic_field_channel_provider.hpp"
#include "internal/environment_asset_repository.hpp"
#include "internal/configured_tx_phy.hpp"
#include "internal/import/bellhop_arrival_import_options.hpp"
#include "internal/import/bellhop_ascii_arrival_parser.hpp"
#include "internal/import/bellhop_raw_arrival_bundle.hpp"
#include "internal/import/bellhop_raw_arrival_normalizer.hpp"
#include "scenario_runtime_test_support.hpp"

using namespace ns3_factory::assembly::internal;
using namespace ns3_factory::assembly::test;
using namespace ns3_factory::environment::internal;
using namespace ns3_factory::environment::internal::import;
using namespace ns3_factory::phy::internal;

namespace {

class TemporaryAssetRepository final {
 public:
  TemporaryAssetRepository()
      : root_(std::filesystem::temp_directory_path() /
              "ns3_factory_bellhop_import_runtime_repository") {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
    std::filesystem::create_directory(root_);
  }

  ~TemporaryAssetRepository() {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  [[nodiscard]] auto root() const -> const std::filesystem::path& {
    return root_;
  }

 private:
  std::filesystem::path root_;
};

class CountingTxPhy final : public ITxPhy {
 public:
  explicit CountingTxPhy(const ConfiguredTxPhy& delegate) noexcept
      : delegate_(delegate) {}

  auto Encode(const DigitalPacket& packet,
              const TxEncodeRequest& request) const
      -> Result<TxEmission> override {
    ++count;
    return delegate_.get().Encode(packet, request);
  }

  mutable std::size_t count{0U};

 private:
  std::reference_wrapper<const ConfiguredTxPhy> delegate_;
};

class CountingChannelProvider final : public IChannelFieldProvider {
 public:
  explicit CountingChannelProvider(
      const AcousticFieldChannelProvider& delegate) noexcept
      : delegate_(delegate) {}

  auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldOutcome> override {
    ++query_count;
    receiver_ids.push_back(query.receiver_node_id());
    auto outcome = delegate_.get().Query(query);
    if(outcome) {
      if(std::holds_alternative<ChannelFieldResponse>(*outcome)) {
        ++response_count;
      } else {
        ++no_arrival_count;
      }
    }
    return outcome;
  }

  mutable std::size_t query_count{0U};
  mutable std::size_t response_count{0U};
  mutable std::size_t no_arrival_count{0U};
  mutable std::vector<NodeId> receiver_ids;

 private:
  std::reference_wrapper<const AcousticFieldChannelProvider> delegate_;
};

class CountingNoise final : public INoiseFieldProvider {
 public:
  auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    ++count;
    receiver_ids.push_back(query.receiver_node_id());
    return NoiseObservation::Create(query.receiver_node_id(),
                                    query.observed_from(),
                                    query.observed_until(),
                                    query.lower_frequency_hz(),
                                    query.upper_frequency_hz(),
                                    45.0);
  }

  mutable std::size_t count{0U};
  mutable std::vector<NodeId> receiver_ids;
};

class CountingRx final : public IRxPhy {
 public:
  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    ++count;
    const auto& signal = request.receiver_window().desired_signal();
    receiver_ids.push_back(signal.receiver_node_id());
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  DecodeOutcome::kDecoded);
  }

  mutable std::size_t count{0U};
  mutable std::vector<NodeId> receiver_ids;
};

constexpr auto PositionedNode(std::uint64_t id,
                              double x,
                              double depth) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{x, 0.0, -depth},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto Snapshot() -> Result<WorldSnapshot> {
  return WorldSnapshot::Create(
      SnapshotVersion{0},
      SimTime::Zero(),
      {PositionedNode(0, 0.0, 10.0),
       PositionedNode(1, 1.0, 20.0),
       PositionedNode(2, 2.0, 30.0),
       PositionedNode(3, 3.0, 40.0)});
}

auto ImportedField() -> Result<AcousticFieldAsset> {
  // One source depth, three receiver depths and three ranges. Only the
  // N1/range-1 and N3/range-3 diagonal cells contain arrivals; N2 is an
  // explicit Narr==0 coverage cell.
  std::istringstream ascii{
      "'2D'\n"
      "12000\n"
      "1 10\n"
      "3 20 30 40\n"
      "3 1 2 3\n"
      "1\n"
      "1\n"
      "0.2 30 0.020 0 -10 10 0 0\n"
      "0\n"
      "0\n"
      "0\n"
      "0\n"
      "0\n"
      "0\n"
      "0\n"
      "1\n"
      "0.1 -45 0.030 0 -20 20 0 1\n"};
  BellhopArrivalImportOptions options{BellhopReceiverRangeUnit::kMeters};
  auto dataset = BellhopAsciiArrivalParser::Parse(ascii, options);
  if(!dataset) return std::unexpected(dataset.error());
  std::vector<BellhopRawArrivalDataset> datasets;
  datasets.push_back(std::move(*dataset));
  auto bundle = BellhopRawArrivalBundle::Create(std::move(datasets));
  if(!bundle) return std::unexpected(bundle.error());
  auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!frame) return std::unexpected(frame.error());
  return BellhopRawArrivalNormalizer::Normalize(
      *bundle, 1U, "synthetic Bellhop ASCII integration", *frame);
}

auto QueueHasOnly(const PacketQueueStore& queues,
                  NodeId owner,
                  const DigitalPacket& packet) -> bool {
  for(const auto node_id : queues.node_ids()) {
    const auto size = queues.size(node_id);
    const auto front = queues.PeekFront(node_id);
    if(!size || !front) return false;
    if(node_id == owner) {
      if(*size != 1U || !*front || **front != packet) return false;
    } else if(*size != 0U || *front) {
      return false;
    }
  }
  return true;
}

auto TestRawImportPackageToScenarioRuntime() -> bool {
  TemporaryAssetRepository temporary_repository;
  auto snapshot = Snapshot();
  auto queues_result = PacketQueueStore::Create(NodeIds());
  auto deliveries_result = ApplicationDeliveryStore::Create(NodeIds());
  auto connectivity_policy =
      ConnectivityDecisionPolicy::Create(std::nullopt, 0.5, 0.5);
  auto configured_tx = ConfiguredTxPhy::Create(
      ConfiguredTxPhyConfig{
          ModulationConfig{ModulationScheme::kBpsk,
                           8'000.0,
                           1'000.0,
                           1.0e6,
                           12'000.0,
                           0.0},
          SimDuration::FromNanoseconds(125'000),
          4'000.0,
          120.0});
  auto field = ImportedField();
  auto frequency = DiscreteFrequencySelectionPolicy::Create(0.0);
  auto repository =
      EnvironmentAssetRepository::Open(temporary_repository.root());
  auto asset_id = EnvironmentAssetId::Create("synthetic-bellhop-field-v1");
  auto package_provenance = EnvironmentAssetPackageProvenance::Create(
      EnvironmentAssetProducerType::kBellhopRawImport,
      "platform-integration-test",
      "synthetic Bellhop import-to-runtime field",
      "synthetic-runtime.arr",
      "bellhop-normalization-v1");
  if(!snapshot || !queues_result || !deliveries_result ||
     !connectivity_policy || !configured_tx || !field || !frequency) {
    return false;
  }
  if(!repository || !asset_id || !package_provenance) return false;
  const auto registration = repository->Register(
      *asset_id, *field, *package_provenance);
  auto loaded_field = repository->Load(*asset_id);
  if(!registration || !loaded_field ||
     registration->signal_cell_count != 2U ||
     registration->no_arrival_cell_count != 7U) {
    return false;
  }

  // Runtime receives the immutable in-memory handle. Removing the offline
  // package before execution proves ChannelProvider::Query has no filesystem
  // dependency.
  std::error_code remove_error;
  std::filesystem::remove_all(temporary_repository.root(), remove_error);
  if(remove_error) return false;
  auto acoustic_provider = AcousticFieldChannelProvider::Create(
      std::move(*loaded_field), *frequency);
  if(!acoustic_provider) return false;

  Ns3KernelGateway gateway;
  WorldStateStore world{std::move(*snapshot)};
  PacketQueueStore queues{std::move(*queues_result)};
  ApplicationDeliveryStore deliveries{std::move(*deliveries_result)};
  CommunicationIdAllocator ids{TransmissionId{100}, ReceptionId{1'000}};
  ChainFeasibilityEstimator estimator{FeasibilityMode::kStableChain};
  ConfiguredRoleAssignmentPolicy roles{
      {RoleBinding{NodeId{0}, ProtocolRole::kMember},
       RoleBinding{NodeId{1}, ProtocolRole::kRelay},
       RoleBinding{NodeId{2}, ProtocolRole::kRelay},
       RoleBinding{NodeId{3}, ProtocolRole::kSink}}};
  AllFeasibleLinksTopologyPolicy topology;
  StructureBuilder structure_builder{
      *connectivity_policy, estimator, roles, topology};
  CyclingPlanner planner{gateway, PlanningCycleId{0}, {NodeId{0}}};
  CountingTxPhy tx_phy{*configured_tx};
  CountingChannelProvider channel{*acoustic_provider};
  CountingNoise noise;
  CountingRx rx;
  ScenarioRuntime runtime{gateway,
                          world,
                          queues,
                          deliveries,
                          ids,
                          structure_builder,
                          planner,
                          tx_phy,
                          channel,
                          noise,
                          rx,
                          PlanningCycleId{0}};
  const auto packet = TestPacket();
  if(!queues.Enqueue(NodeId{0}, packet)) return false;

  const auto run = runtime.RunCycles(1U);
  if(!run) {
    std::cerr << "RunCycles failed: " << run.error().message << '\n';
    return false;
  }
  const auto next_transmission = ids.NextTransmissionId();
  const auto next_reception = ids.NextReceptionId();
  const auto valid =
      runtime.state() == ScenarioRuntimeState::kCompleted &&
      tx_phy.count == 1U && channel.query_count == 3U &&
      channel.response_count == 2U &&
      channel.no_arrival_count == 1U &&
      channel.receiver_ids ==
          std::vector<NodeId>{NodeId{1}, NodeId{2}, NodeId{3}} &&
      noise.count == 2U &&
      noise.receiver_ids == std::vector<NodeId>{NodeId{1}, NodeId{3}} &&
      rx.count == 2U &&
      rx.receiver_ids == std::vector<NodeId>{NodeId{1}, NodeId{3}} &&
      next_transmission && *next_transmission == TransmissionId{101} &&
      next_reception && *next_reception == ReceptionId{1'002} &&
      QueueHasOnly(queues, NodeId{1}, packet) &&
      deliveries.size() == 0U &&
      world.current_snapshot().version() == SnapshotVersion{1} &&
      world.current_snapshot().committed_at() == Seconds(10);
  if(!valid) {
    std::cerr << "Unexpected import integration result: tx="
              << tx_phy.count << " query=" << channel.query_count
              << " response=" << channel.response_count
              << " no_arrival=" << channel.no_arrival_count
              << " noise=" << noise.count << " rx=" << rx.count
              << '\n';
  }
  return valid;
}

}  // namespace

auto main() -> int {
  return TestRawImportPackageToScenarioRuntime() ? EXIT_SUCCESS
                                                  : EXIT_FAILURE;
}
