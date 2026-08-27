#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/link_feasibility.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/trace.hpp>

#include "internal/acceptance_feature_application.hpp"
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
using namespace assembly::internal;
using namespace contracts;
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

class StarEstimator final : public ILinkFeasibilityEstimator {
 public:
  explicit StarEstimator(NodeId fusion_center) noexcept
      : fusion_center_(fusion_center) {}

  auto Estimate(const LinkFeasibilityQuery& query) const
      -> Result<LinkFeasibilityEstimate> override {
    return LinkFeasibilityEstimate::Create(
        query.source_node_id(),
        query.target_node_id(),
        query.observed_at(),
        query.target_node_id() == fusion_center_ ? 1.0 : 0.0);
  }

 private:
  NodeId fusion_center_;
};

class AcceptancePlanner final : public IProtocolCyclePlanner {
 public:
  explicit AcceptancePlanner(const AcceptanceScenarioConfig& config) noexcept
      : config_(config) {}

  auto Build(const WorldSnapshot& snapshot,
             const StructureSnapshot& structure) const
      -> Result<ProtocolCyclePlan> override {
    auto policy = ConfiguredTdmaPolicy::Create(
        config_.get().tdma_slot_duration(),
        config_.get().tdma_guard_interval(),
        std::vector<NodeId>{config_.get().sensor_node_ids().begin(),
                            config_.get().sensor_node_ids().end()});
    if(!policy) return std::unexpected(policy.error());
    const DirectToSinkRoutingPlanner routing;
    const ConfiguredTdmaMacPlanner mac{std::move(*policy)};
    return CompositeProtocolCyclePlanner{routing, mac}.Build(
        snapshot, structure);
  }

 private:
  std::reference_wrapper<const AcceptanceScenarioConfig> config_;
};

class SelectiveChannel final : public IChannelFieldProvider {
 public:
  SelectiveChannel(std::optional<TransmissionId> no_arrival,
                   NodeId fusion_center_id) noexcept
      : no_arrival_transmission(no_arrival),
        fusion_center(fusion_center_id) {}

  std::optional<TransmissionId> no_arrival_transmission;
  NodeId fusion_center;

  auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldOutcome> override {
    if(no_arrival_transmission == query.transmission_id() &&
       query.receiver_node_id() == fusion_center) {
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

  mutable std::size_t no_arrival_count{0};
};

class ConstantNoise final : public INoiseFieldProvider {
 public:
  auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    return NoiseObservation::Create(query.receiver_node_id(),
                                    query.observed_from(),
                                    query.observed_until(),
                                    query.lower_frequency_hz(),
                                    query.upper_frequency_hz(),
                                    45.0);
  }
};

class SelectiveRxPhy final : public IRxPhy {
 public:
  SelectiveRxPhy(std::optional<TransmissionId> not_decoded,
                 NodeId fusion_center_id) noexcept
      : not_decoded_transmission(not_decoded),
        fusion_center(fusion_center_id) {}

  std::optional<TransmissionId> not_decoded_transmission;
  NodeId fusion_center;

  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    const auto& signal = request.receiver_window().desired_signal();
    const auto not_decoded =
        not_decoded_transmission == signal.transmission_id() &&
        signal.receiver_node_id() == fusion_center;
    if(not_decoded) ++not_decoded_count;
    return RxDecodeResult::Create(
        signal.transmission_id(),
        signal.emission().packet_id(),
        signal.receiver_node_id(),
        not_decoded ? DecodeOutcome::kNotDecoded
                    : DecodeOutcome::kDecoded);
  }

  mutable std::size_t not_decoded_count{0};
};

struct RunOptions final {
  AcceptanceScenarioProfile profile;
  std::size_t cycle_count;
  std::optional<TransmissionId> no_arrival_transmission;
  std::optional<TransmissionId> not_decoded_transmission;
};

struct RunObservation final {
  WorldSnapshot final_snapshot;
  std::vector<GeneratedFeatureReport> generated_reports;
  std::vector<FusionResult> fusion_results;
  std::vector<TraceEvent> trace_events;
  std::size_t delivery_count;
  std::size_t accepted_observation_count;
  std::size_t no_arrival_count;
  std::size_t not_decoded_count;
  std::size_t network_refresh_count;
  std::size_t applied_schedule_update_count;
};

auto Execute(RunOptions options) -> Result<RunObservation> {
  auto config = options.profile == AcceptanceScenarioProfile::kAcceptance4Node
                    ? MakeAcceptance4NodeConfig()
                    : MakeExtended6NodeConfig();
  if(!config) return std::unexpected(config.error());
  auto snapshot = config->InitialWorldSnapshot();
  if(!snapshot) return std::unexpected(snapshot.error());
  std::vector<NodeId> node_ids;
  for(const auto& node : snapshot->nodes()) node_ids.push_back(node.node_id);
  auto queues = PacketQueueStore::Create(node_ids);
  auto deliveries = ApplicationDeliveryStore::Create(node_ids);
  auto decision = ConnectivityDecisionPolicy::Create(std::nullopt, 0.5, 0.5);
  auto tx_phy = phy::internal::RateBasedTxPhy::Create(config->TxPhyConfig());
  if(!queues) return std::unexpected(queues.error());
  if(!deliveries) return std::unexpected(deliveries.error());
  if(!decision) return std::unexpected(decision.error());
  if(!tx_phy) return std::unexpected(tx_phy.error());
  auto application = AcceptanceFeatureApplication::Create(
      *config, *queues, *deliveries, SimTime::Zero());
  if(!application) return std::unexpected(application.error());

  Ns3KernelGateway gateway;
  WorldStateStore world{std::move(*snapshot)};
  CommunicationIdAllocator ids{TransmissionId{1'000}, ReceptionId{2'000}};
  StarEstimator estimator{config->fusion_center_node_id()};
  ConfiguredRoleAssignmentPolicy roles{config->RoleBindings()};
  SingleSinkStarTopologyPolicy topology;
  StructureBuilder structure_builder{*decision, estimator, roles, topology};
  AcceptancePlanner planner{*config};
  SelectiveChannel channel{options.no_arrival_transmission,
                           config->fusion_center_node_id()};
  ConstantNoise noise;
  SelectiveRxPhy rx_phy{options.not_decoded_transmission,
                        config->fusion_center_node_id()};
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
                          config->network_update_interval_cycles(),
                          &*application};
  const auto run = runtime.RunCycles(options.cycle_count);
  if(!run) return std::unexpected(run.error());
  for(const auto node_id : queues->node_ids()) {
    const auto queued = queues->size(node_id);
    if(!queued || *queued != 0) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "Acceptance feature queue did not drain"});
    }
  }
  return RunObservation{
      world.current_snapshot(),
      std::vector<GeneratedFeatureReport>{application->generated_reports().begin(),
                                          application->generated_reports().end()},
      std::vector<FusionResult>{application->fusion_results().begin(),
                                application->fusion_results().end()},
      trace.events,
      deliveries->size(),
      application->accepted_observation_count(),
      channel.no_arrival_count,
      rx_phy.not_decoded_count,
      runtime.network_refresh_count(),
      runtime.applied_schedule_update_count()};
}

auto TransmissionTraces(std::span<const TraceEvent> events)
    -> std::vector<TransmissionTrace> {
  std::vector<TransmissionTrace> transmissions;
  for(const auto& event : events) {
    if(const auto* trace = std::get_if<TransmissionTrace>(&event.payload())) {
      transmissions.push_back(*trace);
    }
  }
  return transmissions;
}

auto CountTrace(std::span<const TraceEvent> events, TraceKind kind)
    -> std::size_t {
  return static_cast<std::size_t>(std::count_if(
      events.begin(), events.end(), [kind](const TraceEvent& event) {
        return event.kind() == kind;
      }));
}

auto CountReceptionDisposition(
    std::span<const TraceEvent> events,
    TraceReceptionDisposition disposition) -> std::size_t {
  return static_cast<std::size_t>(std::count_if(
      events.begin(), events.end(), [disposition](const TraceEvent& event) {
        const auto* reception =
            std::get_if<ReceptionTrace>(&event.payload());
        return reception != nullptr && reception->disposition == disposition;
      }));
}

auto CountNoArrivalTrace(std::span<const TraceEvent> events)
    -> std::size_t {
  return static_cast<std::size_t>(std::count_if(
      events.begin(), events.end(), [](const TraceEvent& event) {
        const auto* channel =
            std::get_if<ChannelOutcomeTrace>(&event.payload());
        return channel != nullptr &&
               std::holds_alternative<TraceNoArrivalChannelOutcome>(
                   channel->outcome);
      }));
}

auto HasReception(std::span<const TraceEvent> events,
                  TransmissionId transmission_id,
                  NodeId receiver_node_id) -> bool {
  return std::any_of(
      events.begin(), events.end(), [&](const TraceEvent& event) {
        const auto* reception =
            std::get_if<ReceptionTrace>(&event.payload());
        return reception != nullptr &&
               reception->transmission_id == transmission_id &&
               reception->receiver_node_id == receiver_node_id;
      });
}

auto HasMatchingInputAndTransmission(
    const RunObservation& run) -> bool {
  const auto transmissions = TransmissionTraces(run.trace_events);
  if(transmissions.size() != run.generated_reports.size()) return false;
  for(const auto& generated : run.generated_reports) {
    const auto match = std::find_if(
        transmissions.begin(),
        transmissions.end(),
        [&](const TransmissionTrace& transmission) {
          return transmission.packet_id == generated.packet_id;
        });
    if(match == transmissions.end() ||
       match->sender_node_id != generated.sender_node_id ||
       match->started_at != generated.sample_time ||
       match->ended_at.nanoseconds() - match->started_at.nanoseconds() !=
           2'000'000'000) {
      return false;
    }
  }
  return true;
}

auto Accurate(const FusionResult& result) -> bool {
  return std::abs(result.estimated_target_x_meters - 200.0) < 3.0 &&
         std::abs(result.estimated_target_y_meters - 150.0) < 3.0;
}

auto ObservationWindowsAreDisjoint(
    std::span<const FusionResult> results) -> bool {
  std::vector<ObservationIdentity> identities;
  for(const auto& result : results) {
    if(result.observation_identities.size() != result.observation_count) {
      return false;
    }
    if(!std::is_sorted(result.observation_identities.begin(),
                       result.observation_identities.end())) {
      return false;
    }
    identities.insert(identities.end(),
                      result.observation_identities.begin(),
                      result.observation_identities.end());
  }
  std::sort(identities.begin(), identities.end());
  return std::adjacent_find(identities.begin(), identities.end()) ==
         identities.end();
}

auto TestAcceptance4NodeGoldenTwoCycleFusion() -> bool {
  const auto run = Execute(RunOptions{
      AcceptanceScenarioProfile::kAcceptance4Node, 2, std::nullopt, std::nullopt});
  if(!run || run->fusion_results.size() != 1) return false;
  const auto& fusion = run->fusion_results.front();
  const auto first_sensor_first = std::find_if(
      run->generated_reports.begin(),
      run->generated_reports.end(),
      [](const GeneratedFeatureReport& generated) {
        return generated.sender_node_id == NodeId{10} &&
               generated.cycle_id == PlanningCycleId{0};
      });
  const auto first_sensor_second = std::find_if(
      run->generated_reports.begin(),
      run->generated_reports.end(),
      [](const GeneratedFeatureReport& generated) {
        return generated.sender_node_id == NodeId{10} &&
               generated.cycle_id == PlanningCycleId{1};
      });
  return run->generated_reports.size() == 6 && run->delivery_count == 6 &&
         run->accepted_observation_count == 6 &&
         run->final_snapshot.version() == SnapshotVersion{2} &&
         run->final_snapshot.committed_at().nanoseconds() == 24'000'000'000 &&
         fusion.observation_count == 6 &&
         fusion.started_at == SimTime::Zero() &&
         fusion.completed_at.nanoseconds() == 24'000'000'000 &&
         fusion.fusion_period.nanoseconds() == 24'000'000'000 &&
         fusion.meets_period_requirement && Accurate(fusion) &&
         first_sensor_first != run->generated_reports.end() &&
         first_sensor_second != run->generated_reports.end() &&
         first_sensor_first->report.sensor_x_meters_quantized == 1'000 &&
         first_sensor_second->report.sensor_x_meters_quantized == 1'017 &&
         HasMatchingInputAndTransmission(*run) &&
         CountTrace(run->trace_events, TraceKind::kTransmission) == 6 &&
         CountTrace(run->trace_events, TraceKind::kCycleCommit) == 2;
}

auto TestNoArrivalAndNotDecodedDoNotEnterFusion() -> bool {
  const auto run = Execute(RunOptions{
      AcceptanceScenarioProfile::kAcceptance4Node,
      5,
      TransmissionId{1'000},
      TransmissionId{1'001}});
  if(!run || run->fusion_results.size() != 2) return false;
  const auto& first_fusion = run->fusion_results.front();
  const auto& second_fusion = run->fusion_results.back();
  const auto node10_first = std::find_if(
      run->generated_reports.begin(),
      run->generated_reports.end(),
      [](const GeneratedFeatureReport& generated) {
        return generated.sender_node_id == NodeId{10} &&
               generated.cycle_id == PlanningCycleId{0};
      });
  const auto node10_second = std::find_if(
      run->generated_reports.begin(),
      run->generated_reports.end(),
      [](const GeneratedFeatureReport& generated) {
        return generated.sender_node_id == NodeId{10} &&
               generated.cycle_id == PlanningCycleId{1};
      });
  return run->generated_reports.size() == 15 && run->delivery_count == 13 &&
         run->accepted_observation_count == 13 &&
         run->no_arrival_count == 1 && run->not_decoded_count == 1 &&
         first_fusion.observation_count == 7 &&
         first_fusion.started_at.nanoseconds() == 8'000'000'000 &&
         first_fusion.completed_at.nanoseconds() == 36'000'000'000 &&
         first_fusion.fusion_period.nanoseconds() == 28'000'000'000 &&
         first_fusion.meets_period_requirement && Accurate(first_fusion) &&
         second_fusion.observation_count == 6 &&
         second_fusion.started_at.nanoseconds() == 36'000'000'000 &&
         second_fusion.completed_at.nanoseconds() == 60'000'000'000 &&
         second_fusion.fusion_period.nanoseconds() == 24'000'000'000 &&
         second_fusion.meets_period_requirement && Accurate(second_fusion) &&
         ObservationWindowsAreDisjoint(run->fusion_results) &&
         run->final_snapshot.version() == SnapshotVersion{5} &&
         run->final_snapshot.committed_at().nanoseconds() == 60'000'000'000 &&
         CountTrace(run->trace_events, TraceKind::kCycleCommit) == 5 &&
         CountNoArrivalTrace(run->trace_events) == 1 &&
         !HasReception(run->trace_events, TransmissionId{1'000}, NodeId{99}) &&
         HasReception(run->trace_events, TransmissionId{1'001}, NodeId{99}) &&
         CountReceptionDisposition(
             run->trace_events,
             TraceReceptionDisposition::kNotDecoded) == 1 &&
         node10_first != run->generated_reports.end() &&
         node10_second != run->generated_reports.end() &&
         node10_first->report.sensor_x_meters_quantized !=
             node10_second->report.sensor_x_meters_quantized &&
         HasMatchingInputAndTransmission(*run);
}

auto TestAcceptance4NodeRecurringFusionWindows() -> bool {
  const auto run = Execute(RunOptions{
      AcceptanceScenarioProfile::kAcceptance4Node, 4, std::nullopt, std::nullopt});
  if(!run || run->fusion_results.size() != 2) return false;
  const auto& first = run->fusion_results.front();
  const auto& second = run->fusion_results.back();
  return run->generated_reports.size() == 12 &&
         run->accepted_observation_count == 12 &&
         first.fusion_sequence == 1 && second.fusion_sequence == 2 &&
         first.observation_count == 6 && second.observation_count == 6 &&
         first.started_at == SimTime::Zero() &&
         first.completed_at.nanoseconds() == 24'000'000'000 &&
         first.fusion_period.nanoseconds() == 24'000'000'000 &&
         second.started_at.nanoseconds() == 24'000'000'000 &&
         second.completed_at.nanoseconds() == 48'000'000'000 &&
         second.fusion_period.nanoseconds() == 24'000'000'000 &&
         first.meets_period_requirement && second.meets_period_requirement &&
         Accurate(first) && Accurate(second) &&
         ObservationWindowsAreDisjoint(run->fusion_results) &&
         CountTrace(run->trace_events, TraceKind::kCycleCommit) == 4;
}

auto TestExtended6NodeOneCycleFusion() -> bool {
  const auto run = Execute(RunOptions{
      AcceptanceScenarioProfile::kExtended6Node, 1, std::nullopt, std::nullopt});
  if(!run || run->fusion_results.size() != 1) return false;
  const auto& fusion = run->fusion_results.front();
  return run->generated_reports.size() == 5 && run->delivery_count == 5 &&
         run->accepted_observation_count == 5 &&
         fusion.observation_count == 5 &&
         fusion.started_at == SimTime::Zero() &&
         fusion.completed_at.nanoseconds() == 20'000'000'000 &&
         fusion.fusion_period.nanoseconds() == 20'000'000'000 &&
         fusion.meets_period_requirement && Accurate(fusion) &&
         run->network_refresh_count == 1 &&
         run->applied_schedule_update_count == 1 &&
         HasMatchingInputAndTransmission(*run) &&
         CountTrace(run->trace_events, TraceKind::kCycleCommit) == 1;
}

}  // namespace

auto main() -> int {
  return TestAcceptance4NodeGoldenTwoCycleFusion() &&
                 TestAcceptance4NodeRecurringFusionWindows() &&
                 TestNoArrivalAndNotDecodedDoNotEnterFusion() &&
                 TestExtended6NodeOneCycleFusion()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
