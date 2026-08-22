#include <cstddef>
#include <cstdlib>
#include <functional>
#include <optional>
#include <utility>

#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/configured_tx_phy.hpp"
#include "scenario_runtime_test_support.hpp"

using namespace ns3_factory::assembly::internal;
using namespace ns3_factory::assembly::test;
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
    auto emission = delegate_.get().Encode(packet, request);
    if(emission) last_emission = *emission;
    return emission;
  }

  mutable std::size_t count{0};
  mutable std::optional<TxEmission> last_emission;

 private:
  std::reference_wrapper<const ConfiguredTxPhy> delegate_;
};

auto QueueHasOnly(const PacketQueueStore& queues,
                  NodeId owner,
                  const DigitalPacket& packet) -> bool {
  for(const auto node_id : queues.node_ids()) {
    const auto size = queues.size(node_id);
    const auto front = queues.PeekFront(node_id);
    if(!size || !front) return false;
    if(node_id == owner) {
      if(*size != 1 || !*front || **front != packet) return false;
    } else if(*size != 0 || *front) {
      return false;
    }
  }
  return true;
}

auto TestScenarioRuntimeUsesOneM5EncodeForThreeReceivers() -> bool {
  auto snapshot = InitialSnapshot();
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
  if(!snapshot || !queues_result || !deliveries_result ||
     !connectivity_policy || !configured_tx) {
    return false;
  }

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
  MockChannel channel;
  MockNoise noise;
  MockRxPhy rx_phy;
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

  const auto run = runtime.RunCycles(1);
  const auto now = gateway.PlatformNow();
  return run && runtime.state() == ScenarioRuntimeState::kCompleted &&
         tx_phy.count == 1 && tx_phy.last_emission &&
         tx_phy.last_emission->transmission_id() == TransmissionId{100} &&
         tx_phy.last_emission->packet_id() == PacketId{10} &&
         tx_phy.last_emission->sender_node_id() == NodeId{0} &&
         tx_phy.last_emission->started_at() == SimTime::Zero() &&
         tx_phy.last_emission->duration() ==
             SimDuration::FromNanoseconds(24'000'000) &&
         channel.receiver_audit.size() == 3 && noise.count == 3 &&
         rx_phy.count == 3 && QueueHasOnly(queues, NodeId{1}, packet) &&
         deliveries.size() == 0 &&
         world.current_snapshot().version() == SnapshotVersion{1} &&
         world.current_snapshot().committed_at() == Seconds(10) && now &&
         *now == SimTime::Zero();
}

}  // namespace

auto main() -> int {
  return TestScenarioRuntimeUsesOneM5EncodeForThreeReceivers()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
