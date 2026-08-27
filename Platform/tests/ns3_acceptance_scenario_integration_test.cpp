#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/link_feasibility.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/trace.hpp>

#include "internal/acceptance_scenario_config.hpp"
#include "internal/application_delivery_store.hpp"
#include "internal/communication_id_allocator.hpp"
#include "internal/composite_protocol_cycle_planner.hpp"
#include "internal/configured_role_assignment_policy.hpp"
#include "internal/configured_tdma_mac_planner.hpp"
#include "internal/configured_tdma_policy.hpp"
#include "internal/connectivity_decision_policy.hpp"
#include "internal/direct_to_sink_routing_planner.hpp"
#include "internal/ns3_kernel_gateway.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/rate_based_tx_phy.hpp"
#include "internal/scenario_runtime.hpp"
#include "internal/single_sink_star_topology_policy.hpp"
#include "internal/structure_builder.hpp"
#include "internal/world_state_store.hpp"

namespace {

using namespace ns3_factory;
using namespace contracts;
using namespace assembly::internal;
using namespace kernel::internal;
using namespace planning::internal;
using namespace runtime::internal;
using namespace structure::internal;

class RecordingTraceSink final : public ITraceSink {
 public:
  auto Emit(const TraceEvent& event) noexcept -> Status override {
    events.push_back(event);
    return {};
  }

  std::vector<TraceEvent> events;
};

class AcceptanceStarEstimator final : public ILinkFeasibilityEstimator {
 public:
  explicit AcceptanceStarEstimator(NodeId fusion_center) noexcept
      : fusion_center_(fusion_center) {}

  auto Estimate(const LinkFeasibilityQuery& query) const
      -> Result<LinkFeasibilityEstimate> override {
    observed_at.push_back(query.observed_at());
    const auto score =
        query.target_node_id() == fusion_center_ ? 1.0 : 0.0;
    return LinkFeasibilityEstimate::Create(query.source_node_id(),
                                           query.target_node_id(),
                                           query.observed_at(),
                                           score);
  }

  mutable std::vector<SimTime> observed_at;

 private:
  NodeId fusion_center_;
};

class AlternatingCandidatePlanner final : public IProtocolCyclePlanner {
 public:
  explicit AlternatingCandidatePlanner(
      const AcceptanceScenarioConfig& config) noexcept
      : config_(config) {}

  auto Build(const WorldSnapshot& snapshot,
             const StructureSnapshot& structure) const
      -> Result<ProtocolCyclePlan> override {
    ++build_count;
    auto owners = std::vector<NodeId>{config_.get().sensor_node_ids().begin(),
                                      config_.get().sensor_node_ids().end()};
    const auto cycle_index = structure.cycle_id().value();
    if(cycle_index != 0) std::reverse(owners.begin(), owners.end());
    candidate_orders.push_back(owners);
    auto policy = ConfiguredTdmaPolicy::Create(
        config_.get().tdma_slot_duration(),
        config_.get().tdma_guard_interval(),
        std::move(owners));
    if(!policy) return std::unexpected(policy.error());
    const ConfiguredTdmaMacPlanner mac{std::move(*policy)};
    if(cycle_index % config_.get().network_update_interval_cycles() != 0) {
      candidate_has_routing.push_back(false);
      auto schedule = mac.Build(snapshot, structure);
      if(!schedule) return std::unexpected(schedule.error());
      const auto opportunities = schedule->mac_plan().tx_opportunities();
      return ProtocolCyclePlan::Create(
          schedule->timing(),
          std::vector<TxOpportunity>{opportunities.begin(),
                                     opportunities.end()});
    }
    candidate_has_routing.push_back(true);
    const CompositeProtocolCyclePlanner planner{routing_, mac};
    return planner.Build(snapshot, structure);
  }

  mutable std::size_t build_count{0};
  mutable std::vector<std::vector<NodeId>> candidate_orders;
  mutable std::vector<bool> candidate_has_routing;

 private:
  std::reference_wrapper<const AcceptanceScenarioConfig> config_;
  DirectToSinkRoutingPlanner routing_;
};

class AcceptanceChannel final : public IChannelFieldProvider {
 public:
  auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldOutcome> override {
    queries.push_back(query);
    if(query.transmission_id() == TransmissionId{1'000} &&
       query.receiver_node_id() == NodeId{20}) {
      ++no_arrival_count;
      return ChannelNoArrival{query.transmission_id(),
                              query.receiver_node_id()};
    }
    return ChannelFieldResponse::Create(
        query.transmission_id(),
        query.receiver_node_id(),
        70.0,
        SimDuration::FromNanoseconds(500'000'000),
        {});
  }

  mutable std::vector<ChannelQuery> queries;
  mutable std::size_t no_arrival_count{0};
};

class AcceptanceNoise final : public INoiseFieldProvider {
 public:
  auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    ++query_count;
    return NoiseObservation::Create(query.receiver_node_id(),
                                    query.observed_from(),
                                    query.observed_until(),
                                    query.lower_frequency_hz(),
                                    query.upper_frequency_hz(),
                                    45.0);
  }

  mutable std::size_t query_count{0};
};

class AcceptanceRxPhy final : public IRxPhy {
 public:
  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    ++decode_count;
    const auto& signal = request.receiver_window().desired_signal();
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  DecodeOutcome::kDecoded);
  }

  mutable std::size_t decode_count{0};
};

auto CountTrace(const RecordingTraceSink& sink, TraceKind kind)
    -> std::size_t {
  return static_cast<std::size_t>(std::count_if(
      sink.events.begin(), sink.events.end(), [kind](const TraceEvent& event) {
        return event.kind() == kind;
      }));
}

auto EnqueueTraffic(const AcceptanceScenarioConfig& config,
                    PacketQueueStore& queues) -> Status {
  for(std::size_t cycle = 0; cycle < 12; ++cycle) {
    for(const auto sensor : config.sensor_node_ids()) {
      const auto packet_id = PacketId{
          sensor.value() * 100 + static_cast<std::uint64_t>(cycle)};
      const auto status = queues.Enqueue(
          sensor,
          DigitalPacket{packet_id,
                        sensor,
                        UnicastDestination{config.fusion_center_node_id()},
                        std::vector<std::byte>(
                            config.maximum_planned_payload_bytes())});
      if(!status) return status;
    }
  }
  return {};
}

auto TestAcceptance4NodeTwelveCycleExecution() -> bool {
  auto config = MakeAcceptance4NodeConfig();
  if(!config) return false;
  auto snapshot = config->InitialWorldSnapshot();
  std::vector<NodeId> node_ids;
  if(snapshot) {
    for(const auto& node : snapshot->nodes()) node_ids.push_back(node.node_id);
  }
  auto queues = PacketQueueStore::Create(node_ids);
  auto deliveries = ApplicationDeliveryStore::Create(node_ids);
  auto decision = ConnectivityDecisionPolicy::Create(
      std::nullopt, 0.5, 0.5);
  auto tx_phy = phy::internal::RateBasedTxPhy::Create(config->TxPhyConfig());
  if(!snapshot || !queues || !deliveries || !decision || !tx_phy) {
    return false;
  }
  if(!EnqueueTraffic(*config, *queues)) return false;

  Ns3KernelGateway gateway;
  WorldStateStore world{std::move(*snapshot)};
  CommunicationIdAllocator ids{TransmissionId{1'000}, ReceptionId{2'000}};
  AcceptanceStarEstimator estimator{config->fusion_center_node_id()};
  ConfiguredRoleAssignmentPolicy roles{config->RoleBindings()};
  SingleSinkStarTopologyPolicy topology;
  StructureBuilder structure_builder{*decision,
                                     estimator,
                                     roles,
                                     topology};
  AlternatingCandidatePlanner planner{*config};
  AcceptanceChannel channel;
  AcceptanceNoise noise;
  AcceptanceRxPhy rx_phy;
  RecordingTraceSink trace;
  ScenarioRuntime runtime{gateway,
                          world,
                          *queues,
                          *deliveries,
                          ids,
                          structure_builder,
                          planner,
                          *tx_phy,
                          channel,
                          noise,
                          rx_phy,
                          PlanningCycleId{0},
                          &trace,
                          config->network_update_interval_cycles()};

  const auto run = runtime.RunCycles(12);
  if(!run || runtime.state() != ScenarioRuntimeState::kCompleted ||
     world.current_snapshot().version() != SnapshotVersion{12} ||
     world.current_snapshot().committed_at().nanoseconds() !=
         144'000'000'000 ||
     world.last_committed_cycle_id() != PlanningCycleId{11} ||
     planner.build_count != 12 || runtime.candidate_plan_build_count() != 12 ||
     runtime.network_refresh_count() != 2 ||
     runtime.applied_schedule_update_count() != 2 ||
     channel.queries.size() != 108 || channel.no_arrival_count != 1 ||
     noise.query_count != 107 || rx_phy.decode_count != 107 ||
     deliveries->size() != 36 ||
     CountTrace(trace, TraceKind::kTransmission) != 36 ||
     CountTrace(trace, TraceKind::kChannelOutcome) != 108 ||
     CountTrace(trace, TraceKind::kReception) != 107 ||
     CountTrace(trace, TraceKind::kCycleCommit) != 12) {
    return false;
  }

  for(const auto node_id : queues->node_ids()) {
    const auto size = queues->size(node_id);
    if(!size || *size != 0) return false;
  }
  if(estimator.observed_at.size() != 18 ||
     std::count(estimator.observed_at.begin(),
                estimator.observed_at.end(),
                SimTime::Zero()) != 9 ||
     std::count(estimator.observed_at.begin(),
                estimator.observed_at.end(),
                SimTime::FromNanoseconds(120'000'000'000)) != 9) {
    return false;
  }

  std::vector<TransmissionTrace> transmissions;
  std::size_t traced_no_arrival = 0;
  bool no_arrival_has_reception = false;
  for(const auto& event : trace.events) {
    if(const auto* transmission =
           std::get_if<TransmissionTrace>(&event.payload())) {
      transmissions.push_back(*transmission);
    }
    if(const auto* channel_trace =
           std::get_if<ChannelOutcomeTrace>(&event.payload());
       channel_trace != nullptr &&
       std::holds_alternative<TraceNoArrivalChannelOutcome>(
           channel_trace->outcome)) {
      ++traced_no_arrival;
      for(const auto& candidate : trace.events) {
        const auto* reception =
            std::get_if<ReceptionTrace>(&candidate.payload());
        if(reception != nullptr &&
           reception->transmission_id == channel_trace->transmission_id &&
           reception->receiver_node_id == channel_trace->receiver_node_id) {
          no_arrival_has_reception = true;
        }
      }
    }
  }
  if(transmissions.size() != 36 || traced_no_arrival != 1 ||
     no_arrival_has_reception) {
    return false;
  }

  const std::vector<NodeId> first_applied{NodeId{10}, NodeId{20}, NodeId{30}};
  const std::vector<NodeId> second_applied{NodeId{30}, NodeId{20}, NodeId{10}};
  for(std::size_t cycle = 0; cycle < 12; ++cycle) {
    const auto& expected = cycle < 10 ? first_applied : second_applied;
    for(std::size_t slot = 0; slot < 3; ++slot) {
      const auto& transmission = transmissions[cycle * 3 + slot];
      const auto expected_time =
          static_cast<std::int64_t>(cycle * 12 + slot * 4) *
          1'000'000'000;
      if(transmission.sender_node_id != expected[slot] ||
         transmission.started_at.nanoseconds() != expected_time ||
         transmission.ended_at.nanoseconds() !=
             expected_time + 2'000'000'000) {
        return false;
      }
    }
  }
  if(planner.candidate_orders[1] != second_applied ||
     planner.candidate_orders[11] != second_applied ||
     planner.candidate_has_routing.size() != 12 ||
     !planner.candidate_has_routing[0] ||
     planner.candidate_has_routing[1] ||
     !planner.candidate_has_routing[10] ||
     planner.candidate_has_routing[11] ||
     transmissions[3].sender_node_id != first_applied[0] ||
     transmissions[33].sender_node_id != second_applied[0]) {
    return false;
  }

  constexpr auto kTolerance = 1.0e-9;
  for(const auto& initial : config->nodes()) {
    const auto final = world.current_snapshot().FindNode(
        initial.initial_state.node_id);
    if(!final) return false;
    const auto& initial_motion = initial.initial_state.motion;
    const auto& final_motion = final->get().motion;
    const auto expected_x = initial_motion.position.x_meters +
                            initial_motion.velocity.x_meters_per_second *
                                144.0;
    const auto expected_y = initial_motion.position.y_meters +
                            initial_motion.velocity.y_meters_per_second *
                                144.0;
    if(std::abs(final_motion.position.x_meters - expected_x) > kTolerance ||
       std::abs(final_motion.position.y_meters - expected_y) > kTolerance ||
       final_motion.position.z_meters != initial_motion.position.z_meters) {
      return false;
    }
  }
  const auto fusion = world.current_snapshot().FindNode(
      config->fusion_center_node_id());
  return fusion && fusion->get().motion.position ==
                       Position3d{0.0, 0.0, -8.0};
}

}  // namespace

auto main() -> int {
  return TestAcceptance4NodeTwelveCycleExecution() ? EXIT_SUCCESS
                                                    : EXIT_FAILURE;
}
