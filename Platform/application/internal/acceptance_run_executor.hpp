#pragma once

#include <ns3_factory/trajectory/generator.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <ns3_factory/application/result.hpp>
#include <ns3_factory/contracts/link_feasibility.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/trace.hpp>

#include "internal/acceptance_feature_application.hpp"
#include "internal/communication_traffic_application.hpp"
#include "internal/acceptance_run_report.hpp"
#include "internal/acceptance_scenario_config.hpp"
#include "internal/acoustic_field_channel_provider.hpp"
#include "internal/communication_id_allocator.hpp"
#include "internal/composite_protocol_cycle_planner.hpp"
#include "internal/configured_role_assignment_policy.hpp"
#include "internal/configured_tdma_mac_planner.hpp"
#include "internal/configured_tdma_policy.hpp"
#include "internal/connectivity_decision_policy.hpp"
#include "internal/direct_to_sink_routing_planner.hpp"
#include "internal/shortest_path_to_sink_routing_planner.hpp"
#include "internal/discrete_frequency_selector.hpp"
#include "internal/environment_asset_repository.hpp"
#include "internal/ns3_kernel_gateway.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/rate_based_tx_phy.hpp"
#include "internal/sampled_scalar_phy.hpp"
#include "internal/scalar_ber_rx_phy.hpp"
#include "internal/scenario_runtime.hpp"
#include "internal/single_sink_star_topology_policy.hpp"
#include "internal/structure_builder.hpp"
#include "internal/world_state_store.hpp"
#include "internal/waveform_phy.hpp"
#include "internal/node_waveform_phy.hpp"
#include "internal/planning_execution.hpp"

namespace ns3_factory::application::internal {

namespace acceptance_run_executor_detail {

using namespace contracts;

class RecordingTraceSink final : public ITraceSink {
 public:
  explicit RecordingTraceSink(ITraceSink& downstream) noexcept
      : downstream_(downstream) {}

  void BindPositionSource(
      const INodeTrajectory& trajectories,
      std::vector<NodeId> node_ids) noexcept {
    trajectories_ = &trajectories;
    node_ids_ = std::move(node_ids);
  }

  void BindWaveformSource(const std::vector<phy::internal::WaveformReceptionSummary>& rows) noexcept {
    waveform_rows_ = &rows;
  }
  void BindMixtureSource(const std::vector<phy::internal::RecordedMixtureSummary>& rows) noexcept {
    mixture_rows_ = &rows;
  }

  auto Emit(const TraceEvent& original) noexcept -> Status override {
    // Attach already observed waveform measurements to their own reception.
    // A missing/invalid observation cannot change the causal record or outcome.
    auto event = original;
    if(waveform_rows_) {
      if(const auto* reception = std::get_if<ReceptionTrace>(&original.payload())) {
        const auto found = std::find_if(waveform_rows_->rbegin(), waveform_rows_->rend(),
            [&](const auto& w) { return w.transmission_id == reception->transmission_id &&
                                       w.receiver_node_id == reception->receiver_node_id; });
        if(found != waveform_rows_->rend()) {
          auto payload = *reception;
          payload.waveform = TraceWaveformSummary{found->payload_bit_count, found->bit_error_count,
              found->crc_ok, found->channel_output_power_upa2, found->noise_power_upa2, found->normalized_power};
          auto enriched = TraceEvent::Create(original.occurred_at(), payload);
          if(enriched) event = std::move(*enriched);
        }
      }
    }
    if(mixture_rows_) {
      if(const auto* reception=std::get_if<ReceptionTrace>(&event.payload())) {
        const auto found=std::find_if(mixture_rows_->rbegin(),mixture_rows_->rend(),[&](const auto& w){
          return w.transmission_id==reception->transmission_id && w.receiver_node_id==reception->receiver_node_id;
        });
        if(found!=mixture_rows_->rend()) {
          auto p=*reception;
          p.mixture=TraceRecordedMixture{found->signal_count,found->sample_count,
              found->mixture_power_relative,found->noise_power_relative};
          auto enriched=TraceEvent::Create(original.occurred_at(),p);
          if(enriched)event=std::move(*enriched);
        }
      }
    }
    if(trajectories_ != nullptr &&
       (!last_position_time_ || *last_position_time_ != event.occurred_at())) {
      for(const auto node_id : node_ids_) {
        const auto motion = trajectories_->Query(node_id, event.occurred_at());
        if(!motion) continue;
        const auto& position = motion->position;
        const auto position_event = TraceEvent::Create(
            event.occurred_at(),
            NodePositionTrace{node_id,
                              position.x_meters,
                              position.y_meters,
                              position.z_meters});
        if(!position_event) continue;
        try {
          events.push_back(*position_event);
        } catch(...) {
          // Observation storage failure cannot change simulation causality.
        }
        const auto ignored = downstream_.get().Emit(*position_event);
        static_cast<void>(ignored);
      }
      last_position_time_ = event.occurred_at();
    }
    try {
      events.push_back(event);
    } catch(...) {
      // Observation storage failure cannot change simulation causality.
    }
    const auto ignored = downstream_.get().Emit(event);
    (void)ignored;
    return {};
  }

  std::vector<TraceEvent> events;

 private:
  std::reference_wrapper<ITraceSink> downstream_;
  const std::vector<phy::internal::WaveformReceptionSummary>* waveform_rows_{nullptr};
  const std::vector<phy::internal::RecordedMixtureSummary>* mixture_rows_{nullptr};
  const INodeTrajectory* trajectories_{nullptr};
  std::vector<NodeId> node_ids_;
  std::optional<SimTime> last_position_time_;
};

class FusionCenterEstimator final : public ILinkFeasibilityEstimator {
 public:
  explicit FusionCenterEstimator(NodeId fusion_center) noexcept
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

class ConfiguredPlanner final
    : public planning::internal::IProtocolCyclePlanner {
 public:
  ConfiguredPlanner(const assembly::internal::AcceptanceScenarioConfig& config,
                    RoutingMode routing_mode = RoutingMode::kDirectToFusionCenter) noexcept
      : config_(config), routing_mode_(routing_mode) {}

  auto Build(const WorldSnapshot& snapshot,
             const StructureSnapshot& structure) const
      -> Result<ProtocolCyclePlan> override {
    auto policy = planning::internal::ConfiguredTdmaPolicy::Create(
        config_.get().tdma_slot_duration(),
        config_.get().tdma_guard_interval(),
        std::vector<NodeId>{config_.get().sensor_node_ids().begin(),
                            config_.get().sensor_node_ids().end()});
    if(!policy) return std::unexpected(policy.error());
    const planning::internal::ConfiguredTdmaMacPlanner mac{
        std::move(*policy)};
    if(routing_mode_ == RoutingMode::kShortestPath) {
      const planning::internal::ShortestPathToSinkRoutingPlanner routing;
      return planning::internal::CompositeProtocolCyclePlanner{routing, mac}
          .Build(snapshot, structure);
    }
    const planning::internal::DirectToSinkRoutingPlanner routing;
    return planning::internal::CompositeProtocolCyclePlanner{routing, mac}
        .Build(snapshot, structure);
  }

 private:
  std::reference_wrapper<
      const assembly::internal::AcceptanceScenarioConfig>
      config_;
  RoutingMode routing_mode_;
};

using AcceptancePlanner = ConfiguredPlanner;

class ConstantNoise final : public INoiseFieldProvider {
 public:
  explicit ConstantNoise(double power_db) noexcept : power_db_(power_db) {}

  auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    return NoiseObservation::Create(
        query.receiver_node_id(),
        query.observed_from(),
        query.observed_until(),
        query.lower_frequency_hz(),
        query.upper_frequency_hz(),
        power_db_);
  }

 private:
  double power_db_;
};

class DeterministicRxPhy final : public IRxPhy {
 public:
  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    const auto& signal = request.receiver_window().desired_signal();
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  DecodeOutcome::kDecoded);
  }
};

[[nodiscard]] inline auto ToMetricStatus(
    assembly::internal::AcceptanceMetricStatus status) -> MetricStatus {
  switch(status) {
    case assembly::internal::AcceptanceMetricStatus::kPass:
      return MetricStatus::kPass;
    case assembly::internal::AcceptanceMetricStatus::kFail:
      return MetricStatus::kFail;
    case assembly::internal::AcceptanceMetricStatus::kNotEvaluated:
      return MetricStatus::kNotEvaluated;
  }
  return MetricStatus::kNotEvaluated;
}

[[nodiscard]] inline auto ToOverallStatus(
    assembly::internal::AcceptanceOverallStatus status) -> OverallStatus {
  switch(status) {
    case assembly::internal::AcceptanceOverallStatus::kPass:
      return OverallStatus::kPass;
    case assembly::internal::AcceptanceOverallStatus::kFail:
      return OverallStatus::kFail;
    case assembly::internal::AcceptanceOverallStatus::kNotFullyEvaluated:
      return OverallStatus::kNotFullyEvaluated;
  }
  return OverallStatus::kNotFullyEvaluated;
}

[[nodiscard]] inline auto ToAcceptanceSummary(
    const assembly::internal::AcceptanceRunReport& report)
    -> AcceptanceReportSummary {
  return AcceptanceReportSummary{
      ToMetricStatus(report.network_node_count.status),
      ToMetricStatus(report.communication_rate.status),
      ToMetricStatus(report.bit_error_rate.status),
      ToMetricStatus(report.feature_level_fusion.status),
      ToMetricStatus(report.bearing_point_count.status),
      ToMetricStatus(report.fusion_period.status),
      ToOverallStatus(report.overall_status),
      report.bit_error_rate.evaluated_receptions,
      report.bit_error_rate.missing_evidence_count,
      report.bit_error_rate.maximum_ber,
      report.bit_error_rate.mean_ber,
      report.bit_error_rate.required_maximum_ber,
      report.bearing_point_count.measured_minimum_per_result,
      report.bearing_point_count.required_minimum_per_result,
      report.fusion_period.measured_maximum_period,
      report.fusion_period.required_maximum_period,
      std::string{report.bit_error_rate.reason}};
}

[[nodiscard]] inline auto BuildAcceptanceConfig(
    const ScenarioDefinition& scenario,
    const ExperimentDefinition& experiment,
    const environment::internal::EnvironmentAssetId& environment_asset_id,
    double environment_depth_meters,
    std::size_t encoded_payload_bytes =
        assembly::internal::kDetectionFeatureV1PayloadBytes)
    -> Result<assembly::internal::AcceptanceScenarioConfig> {
  if(experiment.scenario().scenario_id != scenario.scenario_id() ||
     experiment.scenario().scenario_version != scenario.version()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Experiment does not reference the supplied Scenario version"});
  }
  std::vector<assembly::internal::AcceptanceNodeConfig> nodes;
  nodes.reserve(scenario.nodes().size());
  for(const auto& node : scenario.nodes()) {
    nodes.push_back(assembly::internal::AcceptanceNodeConfig{
        node,
        node.node_id == scenario.fusion_center_node_id()
            ? ProtocolRole::kSink
            : ProtocolRole::kMember});
  }
  return assembly::internal::AcceptanceScenarioConfig::Create(
      assembly::internal::AcceptanceScenarioParameters{
          experiment.fusion().acceptance_profile ==
                  AcceptanceProfile::kAcceptance4Node
              ? assembly::internal::AcceptanceScenarioProfile::kAcceptance4Node
              : assembly::internal::AcceptanceScenarioProfile::kExtended6Node,
          std::move(nodes),
          experiment.phy().bit_rate_bits_per_second,
          encoded_payload_bytes,
          experiment.mac().slot_duration,
          experiment.mac().guard_interval,
          experiment.network_update_interval_cycles(),
          experiment.fusion().maximum_ber,
          experiment.fusion().workload == ApplicationWorkload::kCommunicationOnly
              ? 5U : experiment.fusion().minimum_bearing_points,
          experiment.fusion().maximum_fusion_period,
          experiment.fusion().target_x_meters,
          experiment.fusion().target_y_meters,
          experiment.phy().center_frequency_hz,
          experiment.phy().occupied_bandwidth_hz,
          experiment.phy().source_level_db_re_1upa_at_1m,
          environment_depth_meters,
          environment_asset_id});
}

}  // namespace acceptance_run_executor_detail

// Internal composition entry for Channel 2. No default/frontend experiment is
// silently switched. The observer is optional and must outlive Execute().
struct WaveformExecutionOptions final {
  phy::internal::WaveformPhyConfig phy;
  phy::internal::IWaveformReceptionObserver* observer = nullptr;
  std::map<contracts::NodeId,phy::internal::WaveformPhyConfig> node_modems{};
};

struct ScalarSamplingExecutionOptions final {
  std::uint64_t deterministic_seed;
  phy::internal::ISampledScalarReceptionObserver* observer;
};

inline auto RecordedPropagationTail(const phy::internal::RecordedWaveformChannel& source,
    contracts::SimDuration airtime) -> contracts::Result<contracts::SimDuration> {
  using namespace contracts;
  auto maximum=SimDuration::Zero();
  for(const auto& link:source.links()) {
    // Only frozen timing bounds are exposed to the planner, not future CIR.
    auto query=ChannelQuery::Create(TransmissionId{0},link.transmitter,link.receiver,
        {},{},SimTime::Zero(),link.channel->carrier_hz,link.usable_bandwidth_hz,airtime);
    if(!query)return std::unexpected(query.error());
    auto result=source.Query(*query);
    if(!result)return std::unexpected(result.error());
    const auto& response=std::get<RecordedChannelResponse>(*result);
    auto total=CheckedAdd(response.reference_delay(),response.receive_duration());
    if(!total)return std::unexpected(Error{ErrorCode::kOverflow,"Recorded planning envelope overflow"});
    maximum=std::max(maximum,SimDuration::FromNanoseconds(
        std::max<std::int64_t>(0,total->nanoseconds()-airtime.nanoseconds())));
  }
  return maximum;
}

class RunExecutionExecutor final : public IRunExecutor {
 public:
  void BindFrozenBearingInputs(std::vector<assembly::internal::GeneratedFeatureReport> inputs) {
    frozen_bearing_inputs_=std::move(inputs);
  }
  void BindPlanningEvidence(contracts::IPlanningEvidenceSink* sink) noexcept { planning_evidence_=sink; }
  explicit RunExecutionExecutor(
      const environment::internal::EnvironmentAssetRepository& environments)
      noexcept
      : environments_(environments) {}

  RunExecutionExecutor(
      const environment::internal::EnvironmentAssetRepository& environments,
      WaveformExecutionOptions waveform)
      : environments_(environments), waveform_(std::move(waveform)) {}

  RunExecutionExecutor(
      const environment::internal::EnvironmentAssetRepository& environments,
      std::shared_ptr<const contracts::INodeTrajectory> trajectories,
      std::optional<WaveformExecutionOptions> waveform = std::nullopt,
      std::shared_ptr<const phy::internal::RecordedWaveformChannel> recorded = nullptr)
      : environments_(environments),
        waveform_(std::move(waveform)),
        trajectories_(std::move(trajectories)), recorded_(std::move(recorded)) {}
  RunExecutionExecutor(
      const environment::internal::EnvironmentAssetRepository& environments,
      ScalarSamplingExecutionOptions scalar_sampling)
      : environments_(environments),
        scalar_sampling_(std::move(scalar_sampling)) {}

  [[nodiscard]] auto Execute(
      const RunId& run_id,
      const ScenarioDefinition& scenario,
      const ExperimentDefinition& experiment,
      contracts::ITraceSink& event_sink) const
      -> contracts::Result<RunResult> override {
    using namespace acceptance_run_executor_detail;
    auto asset_id = environment::internal::EnvironmentAssetId::Create(
        scenario.environment().asset_id);
    if(!asset_id) return std::unexpected(asset_id.error());
    auto asset_record = recorded_ ? Result<environment::internal::EnvironmentAssetRecord>{
        std::unexpected(Error{ErrorCode::kUnsupported,"Recorded source does not use Bellhop"})}
        : environments_.get().Find(*asset_id);
    if(!recorded_ && !asset_record) {
      return std::unexpected(
          contracts::Error{asset_record.error().code,
                           "Environment asset missing: " +
                               asset_record.error().message});
    }
    std::shared_ptr<const environment::internal::AcousticFieldAsset> asset=recorded_ ? nullptr : asset_record->asset;
    if(!recorded_ && asset->format_version() !=
       scenario.environment().asset_format_version) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kFailedPrecondition,
              "Environment asset format version does not match Scenario"});
    }
    const auto maximum_depth = recorded_ ? 0.0 : std::max(
        asset->source_depth_m().back(), asset->receiver_depth_m().back());
    const auto workload_payload_bytes =
        experiment.fusion().workload == ApplicationWorkload::kCommunicationOnly
            ? assembly::internal::kCommunicationTrafficPayload.size()
            : assembly::internal::kDetectionFeatureV1PayloadBytes;
    const bool planning_execution = experiment.planning().has_value();
    const bool flow_scoped = planning_execution && experiment.planning()->mac_algorithm &&
        experiment.planning()->mac_algorithm->interface=="planning-mac-v2";
    if(recorded_ && (!planning_execution || !waveform_ || flow_scoped ||
        experiment.fusion().workload!=ApplicationWorkload::kCommunicationOnly))
      return std::unexpected(Error{ErrorCode::kUnsupported,
          "Recorded execution requires explicit communication planning; no synthetic bearing observations or Bellhop link-prediction fallback"});
    if(experiment.scenario().scenario_id != scenario.scenario_id() ||
       experiment.scenario().scenario_version != scenario.version())
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,
          "Experiment does not reference the supplied Scenario version"});
    std::optional<assembly::internal::AcceptanceScenarioConfig> config;
    if(!planning_execution) {
    auto legacy_config = BuildAcceptanceConfig(
        scenario, experiment, *asset_id, maximum_depth,
        workload_payload_bytes +
            ((waveform_ || scalar_sampling_) ? phy::internal::kWaveformCrcBytes : 0U));
    if(!legacy_config) return std::unexpected(legacy_config.error());
    config.emplace(std::move(*legacy_config));
    }
    auto frequency_policy =
        environment::internal::DiscreteFrequencySelectionPolicy::Create(0.0);
    if(!frequency_policy) return std::unexpected(frequency_policy.error());
    std::optional<environment::internal::AcousticFieldChannelProvider> acoustic_channel;
    if(!recorded_) {
      auto created=environment::internal::AcousticFieldChannelProvider::Create(asset,*frequency_policy);
      if(!created) return std::unexpected(created.error());
      acoustic_channel.emplace(std::move(*created));
    }
    const IChannelFieldProvider* channel=recorded_ ? static_cast<const IChannelFieldProvider*>(recorded_.get())
                                                 : static_cast<const IChannelFieldProvider*>(&*acoustic_channel);
    auto initial_snapshot = planning_execution
        ? WorldSnapshot::Create(SnapshotVersion{0}, SimTime::Zero(),
            {scenario.nodes().begin(), scenario.nodes().end()})
        : config->InitialWorldSnapshot();
    if(!initial_snapshot) return std::unexpected(initial_snapshot.error());
    std::vector<contracts::NodeId> node_ids;
    for(const auto& node : initial_snapshot->nodes()) {
      node_ids.push_back(node.node_id);
    }
    auto queues = runtime::internal::PacketQueueStore::Create(node_ids);
    auto deliveries =
        runtime::internal::ApplicationDeliveryStore::Create(node_ids);
    auto connectivity =
        structure::internal::ConnectivityDecisionPolicy::Create(
            std::nullopt, 0.5, 0.5);
    auto tx_phy = phy::internal::RateBasedTxPhy::Create(
        phy::internal::RateBasedTxPhyConfig{
            experiment.phy().bit_rate_bits_per_second,
            experiment.phy().center_frequency_hz,
            experiment.phy().occupied_bandwidth_hz,
            experiment.phy().source_level_db_re_1upa_at_1m});
    if(!queues) return std::unexpected(queues.error());
    if(!deliveries) return std::unexpected(deliveries.error());
    if(!connectivity) return std::unexpected(connectivity.error());
    if(!tx_phy) return std::unexpected(tx_phy.error());
    phy::internal::TransmissionWaveformStore waveform_store;
    std::optional<phy::internal::WaveformTxPhy> waveform_tx;
    std::optional<phy::internal::WaveformRxPhy> waveform_rx;
    std::optional<phy::internal::NodeWaveformPhy> node_waveform;
    struct ResultObserver final : phy::internal::IWaveformReceptionObserver {
      phy::internal::IWaveformReceptionObserver* downstream = nullptr;
      std::vector<WaveformSummary> rows;
      std::vector<phy::internal::WaveformReceptionSummary> measurements;
      std::vector<phy::internal::RecordedMixtureSummary> mixtures;
      void ObserveMixture(const phy::internal::RecordedMixtureSummary& row) noexcept override {
        if(downstream)downstream->ObserveMixture(row);
        try { mixtures.push_back(row); } catch(...) {}
      }
      void Observe(const phy::internal::WaveformReceptionSummary& row) noexcept override {
        if(downstream) downstream->Observe(row);
        try {
          measurements.push_back(row);
          rows.push_back({row.transmission_id, row.receiver_node_id,
                          row.payload_bit_count, row.bit_error_count, row.crc_ok});
        } catch(...) { /* Best-effort observation must not affect decoding. */ }
      }
    } result_observer;
    result_observer.downstream = waveform_ ? waveform_->observer : nullptr;
    if(waveform_) {
      const auto& modulation = waveform_->phy.modulation;
      if(experiment.phy().quality_mode != RxQualityMode::kNone) {
        return std::unexpected(Error{
            ErrorCode::kFailedPrecondition,
            "Waveform execution requires quality_mode=None; observe actual waveform BER separately"});
      }
      // Legacy one-bit symbols use the experiment bit rate directly. OSDM is
      // block modulation: its symbol rate is the complex sample clock, while
      // actual airtime comes from K/M/CP framing in WaveformTxPhy.
      const auto rate_matches =
          modulation.scheme == phy::internal::ModulationScheme::kOsdm ||
          modulation.symbol_rate_baud ==
              static_cast<double>(experiment.phy().bit_rate_bits_per_second);
      if(!rate_matches ||
         modulation.acoustic_carrier_hz !=
             experiment.phy().center_frequency_hz ||
         waveform_->phy.occupied_bandwidth_hz !=
             experiment.phy().occupied_bandwidth_hz ||
         std::abs(20.0 * std::log10(modulation.symbol_amplitude_upa) -
                  experiment.phy().source_level_db_re_1upa_at_1m) > 1e-9) {
        return std::unexpected(Error{
            ErrorCode::kFailedPrecondition,
            "Waveform settings do not match the Experiment PHY configuration"});
      }
      auto created_tx =
          phy::internal::WaveformTxPhy::Create(waveform_->phy, waveform_store);
      auto created_rx = phy::internal::WaveformRxPhy::Create(
          waveform_->phy, waveform_store, &result_observer, recorded_.get());
      if(!created_tx) return std::unexpected(created_tx.error());
      if(!created_rx) return std::unexpected(created_rx.error());
      waveform_tx.emplace(std::move(*created_tx));
      waveform_rx.emplace(std::move(*created_rx));
      if(!waveform_->node_modems.empty()) {
        auto node_phy=phy::internal::NodeWaveformPhy::Create(
            waveform_->node_modems,waveform_store,&result_observer,recorded_.get());
        if(!node_phy)return std::unexpected(node_phy.error());
        node_waveform.emplace(std::move(*node_phy));
      }
    }
    RecordingTraceSink trace{event_sink};
    trace.BindWaveformSource(result_observer.measurements);
    trace.BindMixtureSource(result_observer.mixtures);
    const bool bearing_fusion =
        experiment.fusion().workload != ApplicationWorkload::kCommunicationOnly;
    const auto receiver = planning_execution ? experiment.planning()->fusion_receiver_node_id
                                             : scenario.fusion_center_node_id();
    std::vector<NodeId> report_sources;
    for(const auto& node : scenario.nodes())
      if(node.capability.can_transmit && node.node_id != receiver) report_sources.push_back(node.node_id);
    if(planning_execution) {
      auto endpoint = [&](NodeId id, bool transmit) {
        const auto node = initial_snapshot->FindNode(id);
        return node && (transmit ? node->get().capability.can_transmit : node->get().capability.can_receive);
      };
      if(bearing_fusion && (!receiver || !endpoint(*receiver, false) || report_sources.empty()))
        return std::unexpected(Error{ErrorCode::kInvalidArgument, "Invalid bearing application receiver or sources"});
      for(const auto& flow : experiment.planning()->flows)
        if(!endpoint(flow.source_node_id, true) || !endpoint(flow.destination_node_id, false))
          return std::unexpected(Error{ErrorCode::kInvalidArgument, "Traffic flow endpoint capability is unavailable"});
    }
    std::optional<assembly::internal::AcceptanceFeatureApplication> feature_application;
    if(bearing_fusion) {
      auto created = planning_execution
        ? assembly::internal::AcceptanceFeatureApplication::Create(
            assembly::internal::BearingFeatureConfiguration{report_sources, *receiver,
              experiment.fusion().minimum_bearing_points, experiment.fusion().maximum_fusion_period,
              experiment.fusion().target_x_meters, experiment.fusion().target_y_meters,true,flow_scoped},
            *queues, *deliveries, SimTime::Zero(), PacketId{1'000'000}, &trace)
        : assembly::internal::AcceptanceFeatureApplication::Create(
            *config,
            *queues,
            *deliveries,
            contracts::SimTime::Zero(),
            contracts::PacketId{1'000'000},
            &trace);
      if(!created) return std::unexpected(created.error());
      feature_application.emplace(std::move(*created));
      if(!frozen_bearing_inputs_.empty()) {
        auto frozen=feature_application->BindFrozenInputs(frozen_bearing_inputs_);
        if(!frozen)return std::unexpected(frozen.error());
      }
    }
    std::optional<assembly::internal::CommunicationTrafficApplication> traffic_application;
    if(!bearing_fusion) {
      if(planning_execution) {
        std::vector<assembly::internal::CommunicationTrafficFlow> flows;
        for(const auto& flow : experiment.planning()->flows)
          flows.push_back({flow.source_node_id, flow.destination_node_id, flow.packets_per_cycle,flow.flow_id});
        traffic_application.emplace(std::move(flows), *queues);
      } else {
      traffic_application.emplace(
          std::vector<contracts::NodeId>{config->sensor_node_ids().begin(),
                                         config->sensor_node_ids().end()},
          config->fusion_center_node_id(), *queues);
      }
    }
    phy::internal::ScalarFrameStore scalar_frame_store;
    std::optional<phy::internal::SampledScalarTxPhy> sampled_tx;
    std::optional<phy::internal::SampledScalarRxPhy> sampled_rx;
    if(scalar_sampling_) {
      if(experiment.phy().quality_mode != RxQualityMode::kNone ||
         scalar_sampling_->observer == nullptr) {
        return std::unexpected(Error{
            ErrorCode::kFailedPrecondition,
            "Sampled scalar comparison requires quality_mode=None and observer"});
      }
      auto created_tx = phy::internal::SampledScalarTxPhy::Create(
          tx_phy->config(), scalar_frame_store);
      auto created_rx = phy::internal::SampledScalarRxPhy::Create(
          experiment.phy().bit_rate_bits_per_second,
          scalar_sampling_->deterministic_seed, scalar_frame_store,
          *scalar_sampling_->observer);
      if(!created_tx) return std::unexpected(created_tx.error());
      if(!created_rx) return std::unexpected(created_rx.error());
      sampled_tx.emplace(std::move(*created_tx));
      sampled_rx.emplace(std::move(*created_rx));
    }

    kernel::internal::Ns3KernelGateway gateway;
    runtime::internal::WorldStateStore world{std::move(*initial_snapshot)};
    runtime::internal::CommunicationIdAllocator ids{
        contracts::TransmissionId{1'000}, contracts::ReceptionId{2'000}};
    std::optional<FusionCenterEstimator> legacy_estimator;
    if(config) legacy_estimator.emplace(config->fusion_center_node_id());
    CapabilityLinkEstimator generic_estimator;
    std::vector<RoleBinding> role_bindings;
    if(config) role_bindings = config->RoleBindings();
    else for(const auto& node : scenario.nodes()) role_bindings.push_back({node.node_id, ProtocolRole::kMember});
    structure::internal::ConfiguredRoleAssignmentPolicy roles{
        std::move(role_bindings)};
    structure::internal::SingleSinkStarTopologyPolicy topology;
    DirectedConnectivityTopology generic_topology;
    structure::internal::StructureBuilder structure_builder{
        *connectivity,
        planning_execution ? static_cast<const ILinkFeasibilityEstimator&>(generic_estimator)
                           : static_cast<const ILinkFeasibilityEstimator&>(*legacy_estimator),
        roles, planning_execution ? static_cast<const structure::internal::ILogicalTopologyPolicy&>(generic_topology)
                                  : static_cast<const structure::internal::ILogicalTopologyPolicy&>(topology)};
    std::optional<ConfiguredPlanner> legacy_planner;
    if(config) legacy_planner.emplace(*config, experiment.routing().mode);
    // This catalog stores immutable revision-qualified IDs, not an independent
    // numeric revision column. Use its content version, not the format version.
    PlanningExecutionPlanner generic_planner{experiment};
    if(!recorded_) {
      const auto checksum=environment::internal::package_detail::ChecksumHex(asset_record->metadata.payload_checksum);
      generic_planner=PlanningExecutionPlanner{experiment,run_id.value(),PlanningEnvironment{
          std::cref(*acoustic_channel),scenario.environment().asset_id,"fnv1a64:"+checksum,checksum},planning_evidence_};
    }
    ConstantNoise noise{
        experiment.phy().equivalent_noise_power_db_re_1upa2};
    DeterministicRxPhy deterministic_rx;
    auto modeled_rx = phy::internal::ScalarBerRxPhyDecorator::Create(
        deterministic_rx, experiment.phy().bit_rate_bits_per_second);
    if(!modeled_rx) return std::unexpected(modeled_rx.error());
    const contracts::IRxPhy& rx_phy =
        experiment.phy().quality_mode == RxQualityMode::kModeledBpskAwgn
            ? static_cast<const contracts::IRxPhy&>(*modeled_rx)
            : static_cast<const contracts::IRxPhy&>(deterministic_rx);
    const auto composition =
        waveform_
            ? assembly::internal::PhyExecutionComposition::Waveform(
                  node_waveform ? static_cast<const contracts::ITxPhy&>(*node_waveform) : *waveform_tx,
                  *channel, noise,
                  node_waveform ? static_cast<const contracts::IRxPhy&>(*node_waveform) : *waveform_rx, waveform_store)
            : scalar_sampling_
                  ? assembly::internal::PhyExecutionComposition::AbstractWithArtifacts(
                        *sampled_tx, *channel, noise, *sampled_rx,
                        scalar_frame_store)
                  : assembly::internal::PhyExecutionComposition::Abstract(
                        *tx_phy, *channel, noise, rx_phy);
    assembly::internal::ScenarioRuntime runtime{
        gateway,
        world,
        *queues,
        *deliveries,
        ids,
        structure_builder,
        planning_execution ? static_cast<const planning::internal::IProtocolCyclePlanner&>(generic_planner)
                           : static_cast<const planning::internal::IProtocolCyclePlanner&>(*legacy_planner),
        composition,
        contracts::PlanningCycleId{0},
        &trace,
        experiment.network_update_interval_cycles(),
        bearing_fusion
            ? static_cast<assembly::internal::IScenarioCycleApplication*>(&*feature_application)
            : static_cast<assembly::internal::IScenarioCycleApplication*>(&*traffic_application),
        planning_execution ? assembly::internal::MacScheduleUpdatePolicy::kEveryCycle
                           : assembly::internal::MacScheduleUpdatePolicy::kNetworkUpdateCycle};
    if(flow_scoped) {
      const auto bound_evidence=runtime.BindPlanningEvidence(planning_evidence_);
      if(!bound_evidence) return std::unexpected(bound_evidence.error());
    }
    if(planning_execution) {
      auto airtime = waveform_
          ? phy::internal::ComputeWaveformAirtime(workload_payload_bytes, waveform_->phy)
          : phy::internal::ComputePayloadAirtime(workload_payload_bytes +
                (scalar_sampling_ ? phy::internal::kWaveformCrcBytes : 0U),
                experiment.phy().bit_rate_bits_per_second);
      if(!airtime) return std::unexpected(airtime.error());
      std::vector<NodeAirtime> airtimes;
      for(const auto& n:scenario.nodes()) {
        auto duration=*airtime;
        if(waveform_ && !waveform_->node_modems.empty() && n.capability.can_transmit) {
          const auto found=waveform_->node_modems.find(n.node_id);
          if(found==waveform_->node_modems.end())
            return std::unexpected(Error{ErrorCode::kInvalidArgument,"Missing sender modem airtime"});
          auto exact=phy::internal::ComputeWaveformAirtime(workload_payload_bytes,found->second);
          if(!exact)return std::unexpected(exact.error());
          duration=*exact;
          *airtime=std::max(*airtime,duration);
        }
        airtimes.push_back({n.node_id,n.capability.can_transmit ? duration : SimDuration::Zero()});
      }
      const auto tail = recorded_ ? RecordedPropagationTail(*recorded_,*airtime)
                                  : MaximumAssetPropagationTail(*asset);
      if(!tail) return std::unexpected(tail.error());
      auto occupied = CheckedAdd(*airtime, *tail);
      if(occupied) occupied = CheckedAdd(*occupied, experiment.planning()->safety_margin);
      if(!occupied || *occupied > experiment.planning()->cycle_duration)
        return std::unexpected(Error{ErrorCode::kFailedPrecondition,
            "Planning cycle cannot contain one encoded packet plus acoustic propagation tail and safety margin"});
      const auto quantum = recorded_ ? recorded_->EmissionQuantumNanoseconds()
          : Result<std::int64_t>{1};
      if(!quantum)return std::unexpected(quantum.error());
      if(experiment.planning()->cycle_duration.nanoseconds()%*quantum)
        return std::unexpected(Error{ErrorCode::kInvalidArgument,"Planning cycle is off recorded emission grid"});
      const auto bound_inputs = runtime.BindPlanningInputs(std::move(airtimes), *tail,
          recorded_ && recorded_->SupportsMixture(),SimDuration::FromNanoseconds(*quantum));
      if(!bound_inputs) return std::unexpected(bound_inputs.error());
    }
    const auto duration_ns = planning_execution ? experiment.planning()->cycle_duration.nanoseconds()
                                               : config->communication_cycle_duration().nanoseconds();
    if(experiment.simulation_cycle_count() >
       static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / duration_ns))
      return std::unexpected(Error{ErrorCode::kOverflow, "Trajectory run horizon overflow"});
    const auto trajectory_end = contracts::SimTime::FromNanoseconds(
        duration_ns * static_cast<std::int64_t>(experiment.simulation_cycle_count()));
    std::shared_ptr<const contracts::INodeTrajectory> trajectories = trajectories_;
    if(!trajectories) {
      if(planning_execution) return std::unexpected(Error{ErrorCode::kFailedPrecondition,
          "Planning execution requires exact bound trajectory assets; no generated fallback"});
      auto generated = trajectory::BindGenerated(world.current_snapshot(), trajectory_end);
      if(!generated) return std::unexpected(generated.error());
      trajectories = *generated;
    }
    auto bound = runtime.BindTrajectories(trajectories);
    if(!bound) return std::unexpected(bound.error());
    trace.BindPositionSource(*trajectories, node_ids);
    const auto executed =
        runtime.RunCycles(experiment.simulation_cycle_count());
    if(!executed) return std::unexpected(executed.error());

    const assembly::internal::FusionResultStore empty_fusion_results;
    const auto& fusion_store = bearing_fusion
        ? feature_application->fusion_result_store() : empty_fusion_results;
    std::optional<assembly::internal::AcceptanceRunProjection> projection;
    if(config) {
    auto projected = assembly::internal::AcceptanceRunProjection::Build(
        *config, experiment.phy().bit_rate_bits_per_second, trace.events,
        fusion_store, world.current_snapshot());
    if(!projected) return std::unexpected(projected.error());
    projection.emplace(std::move(*projected));
    }
    const auto report = bearing_fusion && projection
        ? assembly::internal::BuildAcceptanceRunReport(*projection)
        : std::optional<assembly::internal::AcceptanceRunReport>{};

    std::vector<FusionResultSummary> fusion_results;
    const auto fusion_results_view = bearing_fusion
        ? feature_application->fusion_results() : std::span<const assembly::internal::FusionResult>{};
    for(const auto& result : fusion_results_view) {
      fusion_results.push_back(FusionResultSummary{
          result.fusion_sequence,
          result.started_at,
          result.completed_at,
          result.fusion_period,
          result.observation_count,
          result.estimated_target_x_meters,
          result.estimated_target_y_meters});
    }
    std::vector<NodeSummary> nodes;
    for(const auto& node : world.current_snapshot().nodes()) {
      nodes.push_back(NodeSummary{
          node.node_id,
          node.motion.position,
          bearing_fusion && node.node_id == receiver});
    }
    RunVisualization visualization;
    visualization.generated_feature_count = bearing_fusion
        ? feature_application->generated_reports().size() : 0U;
    visualization.waveform = std::move(result_observer.rows);
    for(const auto& fused : fusion_results_view) {
      for(const auto& identity : fused.observation_identities) {
        visualization.fusion_sources.push_back({fused.fusion_sequence,
            identity.sender_node_id, identity.observation_sequence});
      }
    }
    RunProjectionSummary summary{SimTime::Zero(), world.current_snapshot().committed_at(),
        SimDuration::FromNanoseconds(world.current_snapshot().committed_at().nanoseconds()),
        world.current_snapshot().version(), 0, world.current_snapshot().nodes().size(), 0, 0, 0, 0, 0};
    for(const auto& event : trace.events) {
      if(std::holds_alternative<CycleCommitTrace>(event.payload())) ++summary.cycle_count;
      if(std::holds_alternative<TransmissionTrace>(event.payload())) ++summary.transmission_count;
      if(const auto* channel_event = std::get_if<ChannelOutcomeTrace>(&event.payload())) {
        if(HasChannelSignal(channel_event->outcome)) ++summary.channel_signal_count;
        else ++summary.channel_no_arrival_count;
      }
      if(const auto* reception = std::get_if<ReceptionTrace>(&event.payload())) {
        ++summary.reception_count;
        if(reception->disposition == TraceReceptionDisposition::kLocalDelivery) ++summary.local_delivery_count;
      }
    }
    std::vector<contracts::SimTime> sample_times{summary.simulation_started_at, summary.simulation_ended_at};
    for(const auto& event : trace.events) sample_times.push_back(event.occurred_at());
    for(const auto& fused : fusion_results_view) sample_times.push_back(fused.completed_at);
    std::sort(sample_times.begin(), sample_times.end());
    sample_times.erase(std::unique(sample_times.begin(), sample_times.end()), sample_times.end());
    // Bounded terminal read projection, never a new Runtime sampling loop.
    for(const auto at : sample_times) {
      for(const auto& node : world.current_snapshot().nodes()) {
        if(visualization.positions.size() >= 8192) {
          visualization.positions_complete = false;
          break;
        }
        const auto motion = trajectories->Query(node.node_id, at);
        if(!motion) { visualization.positions_complete = false; continue; }
        visualization.positions.push_back({node.node_id, at, motion->position});
      }
      if(!visualization.positions_complete) break;
    }
    return RunResult{
        run_id,
        summary,
        report ? std::optional{ToAcceptanceSummary(*report)} : std::nullopt,
        std::move(fusion_results),
        std::move(nodes), std::move(visualization)};
  }

 private:
  std::reference_wrapper<
      const environment::internal::EnvironmentAssetRepository>
      environments_;
  std::optional<WaveformExecutionOptions> waveform_;
  std::vector<assembly::internal::GeneratedFeatureReport> frozen_bearing_inputs_;
  std::shared_ptr<const contracts::INodeTrajectory> trajectories_;
  std::shared_ptr<const phy::internal::RecordedWaveformChannel> recorded_;
  contracts::IPlanningEvidenceSink* planning_evidence_{};
  std::optional<ScalarSamplingExecutionOptions> scalar_sampling_;
};

// Compatibility name for preset-focused callers. Both paths now execute the
// same generic ExperimentDefinition.
using AcceptanceRunExecutor = RunExecutionExecutor;
using AcceptancePlanner = acceptance_run_executor_detail::ConfiguredPlanner;

}  // namespace ns3_factory::application::internal
