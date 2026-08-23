#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/link_feasibility.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/all_feasible_links_topology_policy.hpp"
#include "internal/application_delivery_store.hpp"
#include "internal/communication_id_allocator.hpp"
#include "internal/composite_protocol_cycle_planner.hpp"
#include "internal/configured_role_assignment_policy.hpp"
#include "internal/configured_tdma_mac_planner.hpp"
#include "internal/configured_tdma_policy.hpp"
#include "internal/connectivity_decision_policy.hpp"
#include "internal/ns3_kernel_gateway.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/scenario_runtime.hpp"
#include "internal/shortest_path_to_sink_routing_planner.hpp"
#include "internal/structure_builder.hpp"
#include "internal/world_state_store.hpp"

namespace ns3_factory::assembly::test {

using namespace contracts;
using namespace kernel::internal;
using namespace planning::internal;
using namespace runtime::internal;
using namespace structure::internal;

constexpr auto Seconds(std::int64_t value) -> SimTime {
  return SimTime::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto DurationSeconds(std::int64_t value) -> SimDuration {
  return SimDuration::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto Node(std::uint64_t id) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, -10.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

inline auto InitialSnapshot() -> Result<WorldSnapshot> {
  return WorldSnapshot::Create(
      SnapshotVersion{0},
      SimTime::Zero(),
      {Node(3), Node(0), Node(2), Node(1)});
}

inline auto NodeIds() -> std::vector<NodeId> {
  return {NodeId{3}, NodeId{0}, NodeId{2}, NodeId{1}};
}

inline auto TestPacket() -> DigitalPacket {
  return DigitalPacket{PacketId{10},
                       NodeId{0},
                       UnicastDestination{NodeId{3}},
                       {std::byte{0x10},
                        std::byte{0x20},
                        std::byte{0x30}}};
}

enum class FeasibilityMode {
  kStableChain,
  kHysteresisFailureCandidate,
};

class ChainFeasibilityEstimator final : public ILinkFeasibilityEstimator {
 public:
  explicit ChainFeasibilityEstimator(FeasibilityMode mode) noexcept
      : mode_(mode) {}

  auto Estimate(const LinkFeasibilityQuery& query) const
      -> Result<LinkFeasibilityEstimate> override {
    observed_at.push_back(query.observed_at());
    const bool chain =
        (query.source_node_id() == NodeId{0} &&
         query.target_node_id() == NodeId{1}) ||
        (query.source_node_id() == NodeId{1} &&
         query.target_node_id() == NodeId{2}) ||
        (query.source_node_id() == NodeId{2} &&
         query.target_node_id() == NodeId{3});
    double score = chain ? 1.0 : 0.0;
    if(mode_ == FeasibilityMode::kHysteresisFailureCandidate) {
      const bool first_cycle = query.observed_at() == SimTime::Zero();
      score = chain ? (first_cycle ? 0.9 : 0.7) : 0.0;
      if(!first_cycle && query.source_node_id() == NodeId{0} &&
         query.target_node_id() == NodeId{2}) {
        score = 0.9;
      }
    }
    return LinkFeasibilityEstimate::Create(query.source_node_id(),
                                           query.target_node_id(),
                                           query.observed_at(),
                                           score);
  }

  mutable std::vector<SimTime> observed_at;

 private:
  FeasibilityMode mode_;
};

struct TxAudit final {
  DigitalPacket packet;
  TxEncodeRequest request;
};

class MockTxPhy final : public ITxPhy {
 public:
  auto Encode(const DigitalPacket& packet,
              const TxEncodeRequest& request) const
      -> Result<TxEmission> override {
    audit.push_back(TxAudit{packet, request});
    return TxEmission::Create(request.transmission_id,
                              packet.packet_id,
                              request.sender_node_id,
                              request.started_at,
                              DurationSeconds(1),
                              25'000.0,
                              4'000.0,
                              180.0);
  }

  mutable std::vector<TxAudit> audit;
};

class MockChannel final : public IChannelFieldProvider {
 public:
  auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldOutcome> override {
    receiver_audit.push_back(query.receiver_node_id());
    if(std::find(no_arrival_receivers.begin(),
                 no_arrival_receivers.end(),
                 query.receiver_node_id()) !=
       no_arrival_receivers.end()) {
      return ChannelNoArrival{query.transmission_id(),
                              query.receiver_node_id()};
    }
    return ChannelFieldResponse::Create(query.transmission_id(),
                                        query.receiver_node_id(),
                                        70.0,
                                        propagation_delay,
                                        {});
  }

  mutable std::vector<NodeId> receiver_audit;
  std::vector<NodeId> no_arrival_receivers;
  SimDuration propagation_delay{DurationSeconds(1)};
};

class MockNoise final : public INoiseFieldProvider {
 public:
  auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    ++count;
    receiver_audit.push_back(query.receiver_node_id());
    return NoiseObservation::Create(query.receiver_node_id(),
                                    query.observed_from(),
                                    query.observed_until(),
                                    query.lower_frequency_hz(),
                                    query.upper_frequency_hz(),
                                    45.0);
  }

  mutable std::size_t count{0};
  mutable std::vector<NodeId> receiver_audit;
};

class MockRxPhy final : public IRxPhy {
 public:
  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    ++count;
    const auto& signal = request.receiver_window().desired_signal();
    receiver_audit.push_back(signal.receiver_node_id());
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  DecodeOutcome::kDecoded);
  }

  mutable std::size_t count{0};
  mutable std::vector<NodeId> receiver_audit;
};

class CyclingPlanner final : public IProtocolCyclePlanner {
 public:
  CyclingPlanner(const Ns3KernelGateway& gateway,
                 PlanningCycleId first_cycle_id,
                 std::vector<NodeId> owners,
                 std::optional<std::size_t> fail_on_call = std::nullopt)
      noexcept
      : gateway_(gateway),
        first_cycle_id_(first_cycle_id),
        owners_(std::move(owners)),
        fail_on_call_(fail_on_call) {}

  auto Build(const WorldSnapshot& snapshot,
             const StructureSnapshot& structure) const
      -> Result<ProtocolCyclePlan> override {
    ++build_count;
    cycle_ids.push_back(structure.cycle_id());
    base_versions.push_back(snapshot.version());
    connectivity_graphs.push_back(structure.connectivity_graph());
    const auto now = gateway_.get().PlatformNow();
    if(!now) return std::unexpected(now.error());
    kernel_times.push_back(*now);
    if(fail_on_call_ && build_count == *fail_on_call_) {
      return std::unexpected(
          Error{ErrorCode::kInternal,
                "Injected protocol-cycle planning failure"});
    }
    if(structure.cycle_id() < first_cycle_id_) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "Planner received a cycle before its configured first id"});
    }
    const auto index = static_cast<std::size_t>(
        structure.cycle_id().value() - first_cycle_id_.value());
    if(index >= owners_.size()) {
      return std::unexpected(
          Error{ErrorCode::kOutOfRange,
                "No configured slot owner for planning cycle"});
    }
    auto policy = ConfiguredTdmaPolicy::Create(
        DurationSeconds(10), {owners_[index]});
    if(!policy) return std::unexpected(policy.error());
    ConfiguredTdmaMacPlanner mac_planner{std::move(*policy)};
    CompositeProtocolCyclePlanner planner{routing_planner_, mac_planner};
    auto plan = planner.Build(snapshot, structure);
    if(plan) timings.push_back(plan->timing());
    return plan;
  }

  mutable std::size_t build_count{0};
  mutable std::vector<PlanningCycleId> cycle_ids;
  mutable std::vector<SnapshotVersion> base_versions;
  mutable std::vector<SimTime> kernel_times;
  mutable std::vector<ConnectivityGraph> connectivity_graphs;
  mutable std::vector<CycleTiming> timings;

 private:
  std::reference_wrapper<const Ns3KernelGateway> gateway_;
  PlanningCycleId first_cycle_id_;
  std::vector<NodeId> owners_;
  std::optional<std::size_t> fail_on_call_;
  ShortestPathToSinkRoutingPlanner routing_planner_;
};

class RuntimeFixture final {
 public:
  static auto Create(
      PlanningCycleId first_cycle_id,
      std::vector<NodeId> owners,
      FeasibilityMode mode = FeasibilityMode::kStableChain,
      std::optional<std::size_t> fail_on_call = std::nullopt)
      -> std::unique_ptr<RuntimeFixture> {
    auto snapshot = InitialSnapshot();
    auto queues = PacketQueueStore::Create(NodeIds());
    auto deliveries = ApplicationDeliveryStore::Create(NodeIds());
    auto decision = ConnectivityDecisionPolicy::Create(
        std::nullopt,
        mode == FeasibilityMode::kStableChain ? 0.5 : 0.8,
        mode == FeasibilityMode::kStableChain ? 0.5 : 0.6);
    if(!snapshot || !queues || !deliveries || !decision) return nullptr;
    return std::unique_ptr<RuntimeFixture>{new RuntimeFixture{
        std::move(*snapshot),
        std::move(*queues),
        std::move(*deliveries),
        std::move(*decision),
        first_cycle_id,
        std::move(owners),
        mode,
        fail_on_call}};
  }

  auto Enqueue(DigitalPacket packet) -> Status {
    return queues.Enqueue(NodeId{0}, std::move(packet));
  }

  [[nodiscard]] auto QueueHasOnly(std::optional<NodeId> owner,
                                  const DigitalPacket& packet) const
      -> bool {
    for(const auto node_id : queues.node_ids()) {
      const auto size = queues.size(node_id);
      const auto front = queues.PeekFront(node_id);
      if(!size || !front) return false;
      if(owner && node_id == *owner) {
        if(*size != 1 || !*front || **front != packet) return false;
      } else if(*size != 0 || *front) {
        return false;
      }
    }
    return true;
  }

  Ns3KernelGateway gateway;
  WorldStateStore world;
  PacketQueueStore queues;
  ApplicationDeliveryStore deliveries;
  CommunicationIdAllocator ids;
  ChainFeasibilityEstimator estimator;
  ConnectivityDecisionPolicy connectivity_policy;
  ConfiguredRoleAssignmentPolicy roles;
  AllFeasibleLinksTopologyPolicy topology;
  StructureBuilder structure_builder;
  MockTxPhy tx_phy;
  MockChannel channel;
  MockNoise noise;
  MockRxPhy rx_phy;
  CyclingPlanner planner;
  internal::ScenarioRuntime runtime;

 private:
  RuntimeFixture(WorldSnapshot snapshot,
                 PacketQueueStore queue_store,
                 ApplicationDeliveryStore delivery_store,
                 ConnectivityDecisionPolicy decision,
                 PlanningCycleId first_cycle_id,
                 std::vector<NodeId> owners,
                 FeasibilityMode mode,
                 std::optional<std::size_t> fail_on_call)
      : world(std::move(snapshot)),
        queues(std::move(queue_store)),
        deliveries(std::move(delivery_store)),
        ids(TransmissionId{100}, ReceptionId{1'000}),
        estimator(mode),
        connectivity_policy(std::move(decision)),
        roles({RoleBinding{NodeId{0}, ProtocolRole::kMember},
               RoleBinding{NodeId{1}, ProtocolRole::kRelay},
               RoleBinding{NodeId{2}, ProtocolRole::kRelay},
               RoleBinding{NodeId{3}, ProtocolRole::kSink}}),
        structure_builder(connectivity_policy,
                          estimator,
                          roles,
                          topology),
        planner(gateway,
                first_cycle_id,
                std::move(owners),
                fail_on_call),
        runtime(gateway,
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
                first_cycle_id) {}
};

}  // namespace ns3_factory::assembly::test
