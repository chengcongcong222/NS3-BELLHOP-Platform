#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/acoustic_field_channel_provider.hpp"
#include "internal/configured_tx_phy.hpp"
#include "scenario_runtime_test_support.hpp"

using namespace ns3_factory::assembly::internal;
using namespace ns3_factory::assembly::test;
using namespace ns3_factory::environment::internal;
using namespace ns3_factory::phy::internal;

namespace {

class CountingM5TxPhy final : public ITxPhy {
 public:
  explicit CountingM5TxPhy(const ConfiguredTxPhy& delegate) noexcept
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

class CountingAcousticFieldProvider final : public IChannelFieldProvider {
 public:
  explicit CountingAcousticFieldProvider(
      const AcousticFieldChannelProvider& delegate) noexcept
      : delegate_(delegate) {}

  auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldResponse> override {
    ++count;
    receiver_ids.push_back(query.receiver_node_id());
    auto response = delegate_.get().Query(query);
    if(response) responses.push_back(*response);
    return response;
  }

  mutable std::size_t count{0U};
  mutable std::vector<NodeId> receiver_ids;
  mutable std::vector<ChannelFieldResponse> responses;

 private:
  std::reference_wrapper<const AcousticFieldChannelProvider> delegate_;
};

class FixedNoiseFieldProvider final : public INoiseFieldProvider {
 public:
  auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    ++count;
    return NoiseObservation::Create(query.receiver_node_id(),
                                    query.observed_from(),
                                    query.observed_until(),
                                    query.lower_frequency_hz(),
                                    query.upper_frequency_hz(),
                                    45.0);
  }

  mutable std::size_t count{0U};
};

class AlwaysDecodeRxPhy final : public IRxPhy {
 public:
  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    ++count;
    const auto& signal = request.receiver_window().desired_signal();
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  DecodeOutcome::kDecoded);
  }

  mutable std::size_t count{0U};
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

auto IntegrationSnapshot() -> Result<WorldSnapshot> {
  return WorldSnapshot::Create(
      SnapshotVersion{0},
      SimTime::Zero(),
      {PositionedNode(0, 0.0, 10.0),
       PositionedNode(1, 1.0, 20.0),
       PositionedNode(2, 2.0, 30.0),
       PositionedNode(3, 3.0, 40.0)});
}

auto IntegrationField() -> Result<AcousticFieldAsset> {
  std::vector<AcousticFieldCell> cells;
  for(const auto receiver_depth : {20.0, 30.0, 40.0}) {
    for(const auto range : {1.0, 2.0, 3.0}) {
      auto path = PropagationPath::Create(
          SimDuration::Zero(),
          (receiver_depth + range) / 100.0,
          receiver_depth / 100.0);
      if(!path) return std::unexpected(path.error());
      cells.push_back(AcousticFieldCell{
          50.0 + receiver_depth + range,
          SimDuration::FromNanoseconds(static_cast<NanosecondCount>(
              (receiver_depth + range) * 1'000'000.0)),
          {*path}});
    }
  }
  auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!frame) return std::unexpected(frame.error());
  return AcousticFieldAsset::Create(1U,
                                    "P0-S2-02 integration fixture",
                                    *frame,
                                    {12'000.0},
                                    {10.0},
                                    {20.0, 30.0, 40.0},
                                    {1.0, 2.0, 3.0},
                                    std::move(cells));
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

auto ResponsesMatchThreePhysicalReceivers(
    const CountingAcousticFieldProvider& provider) -> bool {
  if(provider.receiver_ids !=
         std::vector<NodeId>{NodeId{1}, NodeId{2}, NodeId{3}} ||
     provider.responses.size() != 3U) {
    return false;
  }
  const std::vector<double> expected_loss{71.0, 82.0, 93.0};
  const std::vector<SimDuration> expected_delay{
      SimDuration::FromNanoseconds(21'000'000),
      SimDuration::FromNanoseconds(32'000'000),
      SimDuration::FromNanoseconds(43'000'000)};
  for(std::size_t index = 0U; index < provider.responses.size(); ++index) {
    const auto& response = provider.responses[index];
    if(response.receiver_node_id() != provider.receiver_ids[index] ||
       response.aggregate_transmission_loss_db() != expected_loss[index] ||
       response.first_arrival_delay() != expected_delay[index] ||
       response.paths().size() != 1U) {
      return false;
    }
  }
  return true;
}

auto TestScenarioRuntimeUsesM5AndAcousticFieldFanout() -> bool {
  auto snapshot = IntegrationSnapshot();
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
  auto field = IntegrationField();
  auto frequency = DiscreteFrequencySelectionPolicy::Create(0.0);
  if(!snapshot || !queues_result || !deliveries_result ||
     !connectivity_policy || !configured_tx || !field ||
     !frequency) {
    return false;
  }
  auto acoustic_provider = AcousticFieldChannelProvider::Create(
      std::make_shared<const AcousticFieldAsset>(std::move(*field)),
      *frequency);
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
  CountingM5TxPhy tx_phy{*configured_tx};
  CountingAcousticFieldProvider channel{*acoustic_provider};
  FixedNoiseFieldProvider noise;
  AlwaysDecodeRxPhy rx_phy;
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
                          rx_phy,
                          PlanningCycleId{0}};
  const auto packet = TestPacket();
  if(!queues.Enqueue(NodeId{0}, packet)) return false;

  const auto run = runtime.RunCycles(1U);
  if(!run) {
    std::cerr << "RunCycles failed: " << run.error().message << '\n';
    return false;
  }
  const auto valid =
      runtime.state() == ScenarioRuntimeState::kCompleted &&
      tx_phy.count == 1U && channel.count == 3U &&
      ResponsesMatchThreePhysicalReceivers(channel) &&
      noise.count == 3U && rx_phy.count == 3U &&
      QueueHasOnly(queues, NodeId{1}, packet) && deliveries.size() == 0U &&
      world.current_snapshot().version() == SnapshotVersion{1} &&
      world.current_snapshot().committed_at() == Seconds(10);
  if(!valid) {
    std::cerr << "Unexpected integration result: tx=" << tx_phy.count
              << " channel=" << channel.count << " noise=" << noise.count
              << " rx=" << rx_phy.count
              << " responses=" << channel.responses.size()
              << " deliveries=" << deliveries.size() << '\n';
    for(const auto& response : channel.responses) {
      std::cerr << "  receiver=" << response.receiver_node_id().value()
                << " loss="
                << response.aggregate_transmission_loss_db()
                << " delay="
                << response.first_arrival_delay().nanoseconds() << '\n';
    }
  }
  return valid;
}

}  // namespace

auto main() -> int {
  return TestScenarioRuntimeUsesM5AndAcousticFieldFanout() ? EXIT_SUCCESS
                                                           : EXIT_FAILURE;
}
