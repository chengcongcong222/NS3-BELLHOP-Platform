#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/trace.hpp>

#include "internal/acceptance_feature.hpp"
#include "internal/acceptance_scenario_config.hpp"
#include "internal/application_delivery_store.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/scenario_cycle_application.hpp"

namespace ns3_factory::assembly::internal {

struct GeneratedFeatureReport final {
  contracts::PlanningCycleId cycle_id;
  contracts::NodeId sender_node_id;
  contracts::PacketId packet_id;
  contracts::SimTime sample_time;
  DetectionFeatureReportV1 report;

  auto operator==(const GeneratedFeatureReport&) const -> bool = default;
};

// Business configuration, independent of protocol roles, MAC and preset size.
struct BearingFeatureConfiguration final {
  std::vector<contracts::NodeId> sources;
  contracts::NodeId receiver;
  std::size_t minimum_observations;
  contracts::SimDuration period_reference;
  double target_x;
  double target_y;
  bool cycle_start_inputs = true;
  bool identified_flows = false;
  auto sensor_node_ids() const { return std::span<const contracts::NodeId>{sources}; }
  auto fusion_center_node_id() const { return receiver; }
  auto minimum_bearing_points() const { return minimum_observations; }
  auto maximum_fusion_period() const { return period_reference; }
  auto target_x_meters() const { return target_x; }
  auto target_y_meters() const { return target_y; }
};

class AcceptanceFeatureApplication final
    : public IScenarioCycleApplication {
 public:
  [[nodiscard]] static auto Create(
      const AcceptanceScenarioConfig& config,
      runtime::internal::PacketQueueStore& queue_store,
      const runtime::internal::ApplicationDeliveryStore& delivery_store,
      contracts::SimTime run_started_at,
      contracts::PacketId first_packet_id = contracts::PacketId{1'000'000},
      contracts::ITraceSink* trace_sink = nullptr)
      -> contracts::Result<AcceptanceFeatureApplication>;

  [[nodiscard]] static auto Create(
      BearingFeatureConfiguration config,
      runtime::internal::PacketQueueStore& queue_store,
      const runtime::internal::ApplicationDeliveryStore& delivery_store,
      contracts::SimTime run_started_at,
      contracts::PacketId first_packet_id = contracts::PacketId{1'000'000},
      contracts::ITraceSink* trace_sink = nullptr)
      -> contracts::Result<AcceptanceFeatureApplication>;

  [[nodiscard]] auto input_node_ids() const noexcept
      -> std::span<const contracts::NodeId> override {
    return config_.sensor_node_ids();
  }

  [[nodiscard]] auto input_timing() const noexcept -> ApplicationInputTiming override {
    return config_.cycle_start_inputs ? ApplicationInputTiming::kCycleStart
                                     : ApplicationInputTiming::kLegacyFirstTxOpportunity;
  }
  [[nodiscard]] auto forecast_packet_count(contracts::PlanningCycleId,
      contracts::NodeId node) const -> contracts::Result<std::uint64_t> override {
    return std::binary_search(config_.sources.begin(), config_.sources.end(), node) ? 1U : 0U;
  }
  auto forecast_flows(contracts::PlanningCycleId) const
      -> contracts::Result<std::vector<contracts::FlowInputForecast>> override {
    if(!config_.identified_flows) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,"Bearing flow identity was not explicitly enabled"});
    std::vector<contracts::FlowInputForecast> result;
    // One bearing flow per source in this workload, fixed for this Run. NodeId
    // is used as identity, never an array index, and 0 is a legal FlowId.
    for(auto source:config_.sources) result.push_back({contracts::FlowId{source.value()},source,config_.receiver,1});
    return result;
  }

  [[nodiscard]] auto OnInputReady(
      contracts::PlanningCycleId cycle_id,
      contracts::NodeId node_id,
      contracts::SimTime now,
      const runtime::internal::CycleWorkingState& working_state)
      -> contracts::Status override;

  [[nodiscard]] auto OnRuntimeDecision(
      contracts::PlanningCycleId cycle_id,
      contracts::SimTime now,
      const runtime::internal::CycleWorkingState& working_state)
      -> contracts::Status override;

  [[nodiscard]] auto generated_reports() const noexcept
      -> std::span<const GeneratedFeatureReport> {
    return std::span<const GeneratedFeatureReport>{generated_reports_};
  }

  auto BindFrozenInputs(std::vector<GeneratedFeatureReport> inputs) -> contracts::Status {
    if(!generated_reports_.empty() || frozen_inputs_ || inputs.empty() || !config_.cycle_start_inputs)
      return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
          "Frozen bearing inputs require an unused cycle-start application"});
    frozen_inputs_=std::move(inputs);
    return {};
  }

  [[nodiscard]] auto fusion_results() const noexcept
      -> std::span<const FusionResult> {
    return fusion_result_store_.results();
  }

  [[nodiscard]] constexpr auto fusion_result_store() const noexcept
      -> const FusionResultStore& {
    return fusion_result_store_;
  }

  [[nodiscard]] constexpr auto accepted_observation_count() const noexcept
      -> std::size_t {
    return fusion_accumulator_.unique_observation_count();
  }

 private:
  struct SensorSequence final {
    contracts::NodeId node_id;
    std::uint32_t next_sequence;
  };

  struct GeneratedCycleInput final {
    contracts::PlanningCycleId cycle_id;
    contracts::NodeId node_id;

    constexpr auto operator<=>(const GeneratedCycleInput&) const noexcept =
        default;
  };

  AcceptanceFeatureApplication(
      BearingFeatureConfiguration config,
      runtime::internal::PacketQueueStore& queue_store,
      const runtime::internal::ApplicationDeliveryStore& delivery_store,
      contracts::SimTime run_started_at,
      std::uint64_t next_packet_id,
      FeatureFusionAccumulator fusion_accumulator,
      std::vector<SensorSequence> sensor_sequences,
      contracts::ITraceSink* trace_sink) noexcept
      : config_(std::move(config)),
        queue_store_(queue_store),
        delivery_store_(delivery_store),
        run_started_at_(run_started_at),
        next_packet_id_(next_packet_id),
        fusion_accumulator_(std::move(fusion_accumulator)),
        sensor_sequences_(std::move(sensor_sequences)),
        trace_sink_(trace_sink) {}

  BearingFeatureConfiguration config_;
  std::reference_wrapper<runtime::internal::PacketQueueStore> queue_store_;
  std::reference_wrapper<
      const runtime::internal::ApplicationDeliveryStore> delivery_store_;
  contracts::SimTime run_started_at_;
  std::uint64_t next_packet_id_;
  FeatureFusionAccumulator fusion_accumulator_;
  std::vector<SensorSequence> sensor_sequences_;
  std::vector<GeneratedCycleInput> generated_cycle_inputs_;
  std::vector<GeneratedFeatureReport> generated_reports_;
  std::optional<std::vector<GeneratedFeatureReport>> frozen_inputs_;
  FusionResultStore fusion_result_store_;
  contracts::ITraceSink* trace_sink_{nullptr};
};

inline auto AcceptanceFeatureApplication::Create(
    const AcceptanceScenarioConfig& config,
    runtime::internal::PacketQueueStore& queue_store,
    const runtime::internal::ApplicationDeliveryStore& delivery_store,
    contracts::SimTime run_started_at,
    contracts::PacketId first_packet_id,
    contracts::ITraceSink* trace_sink)
    -> contracts::Result<AcceptanceFeatureApplication> {
  return Create(BearingFeatureConfiguration{
      {config.sensor_node_ids().begin(), config.sensor_node_ids().end()},
      config.fusion_center_node_id(), config.minimum_bearing_points(),
      config.maximum_fusion_period(), config.target_x_meters(), config.target_y_meters(), false},
      queue_store, delivery_store, run_started_at, first_packet_id, trace_sink);
}

inline auto AcceptanceFeatureApplication::Create(
    BearingFeatureConfiguration config,
    runtime::internal::PacketQueueStore& queue_store,
    const runtime::internal::ApplicationDeliveryStore& delivery_store,
    contracts::SimTime run_started_at,
    contracts::PacketId first_packet_id,
    contracts::ITraceSink* trace_sink)
    -> contracts::Result<AcceptanceFeatureApplication> {
  std::sort(config.sources.begin(), config.sources.end());
  if(config.sources.empty() ||
     std::adjacent_find(config.sources.begin(), config.sources.end()) != config.sources.end() ||
     std::binary_search(config.sources.begin(), config.sources.end(), config.receiver) ||
     !std::isfinite(config.target_x) || !std::isfinite(config.target_y)) {
    return std::unexpected(contracts::Error{contracts::ErrorCode::kInvalidArgument,
        "Bearing report sources and application receiver must be explicit and distinct"});
  }
  const auto queue_nodes = queue_store.node_ids();
  const auto delivery_nodes = delivery_store.node_ids();
  for(const auto sensor : config.sensor_node_ids()) {
    if(!std::binary_search(queue_nodes.begin(), queue_nodes.end(), sensor) ||
       !std::binary_search(delivery_nodes.begin(),
                           delivery_nodes.end(),
                           sensor)) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kFailedPrecondition,
              "Acceptance feature sensor is outside runtime stores"});
    }
  }
  if(!std::binary_search(queue_nodes.begin(),
                         queue_nodes.end(),
                         config.fusion_center_node_id()) ||
     !std::binary_search(delivery_nodes.begin(),
                         delivery_nodes.end(),
                         config.fusion_center_node_id())) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Acceptance fusion center is outside runtime stores"});
  }
  auto accumulator = FeatureFusionAccumulator::Create(
      config.minimum_bearing_points(),
      run_started_at,
      config.maximum_fusion_period());
  if(!accumulator) return std::unexpected(accumulator.error());
  std::vector<SensorSequence> sequences;
  sequences.reserve(config.sensor_node_ids().size());
  for(const auto sensor : config.sensor_node_ids()) {
    sequences.push_back(SensorSequence{sensor, 1});
  }
  return AcceptanceFeatureApplication{config,
                                      queue_store,
                                      delivery_store,
                                      run_started_at,
                                      first_packet_id.value(),
                                      std::move(*accumulator),
                                      std::move(sequences),
                                      trace_sink};
}

inline auto AcceptanceFeatureApplication::OnInputReady(
    contracts::PlanningCycleId cycle_id,
    contracts::NodeId node_id,
    contracts::SimTime now,
    const runtime::internal::CycleWorkingState& working_state)
    -> contracts::Status {
  const auto input = GeneratedCycleInput{cycle_id, node_id};
  const auto generated_position = std::lower_bound(
      generated_cycle_inputs_.begin(), generated_cycle_inputs_.end(), input);
  if(generated_position != generated_cycle_inputs_.end() &&
     *generated_position == input) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kAlreadyExists,
                         "Feature input already generated for node/cycle"});
  }
  const auto sequence = std::lower_bound(
      sensor_sequences_.begin(),
      sensor_sequences_.end(),
      node_id,
      [](const SensorSequence& candidate, contracts::NodeId id) {
        return candidate.node_id < id;
      });
  if(sequence == sensor_sequences_.end() || sequence->node_id != node_id) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kNotFound,
                         "Feature input node is not an acceptance sensor"});
  }
  if(sequence->next_sequence > std::numeric_limits<std::uint16_t>::max() ||
     next_packet_id_ == std::numeric_limits<std::uint64_t>::max()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Acceptance feature identity range exhausted"});
  }
  const auto projected = working_state.ProjectNodeState(node_id, now);
  if(!projected) return std::unexpected(projected.error());
  const auto report = MakeDetectionFeatureReportV1(
      static_cast<std::uint16_t>(sequence->next_sequence),
      run_started_at_,
      now,
      projected->motion.position.x_meters,
      projected->motion.position.y_meters,
      config_.target_x_meters(),
      config_.target_y_meters());
  if(!report) return std::unexpected(report.error());
  const auto bytes = EncodeDetectionFeatureReportV1(*report);
  if(!bytes) return std::unexpected(bytes.error());
  const auto packet_id = contracts::PacketId{next_packet_id_};
  if(frozen_inputs_) {
    const auto index=generated_reports_.size();
    const GeneratedFeatureReport actual{cycle_id,node_id,packet_id,now,*report};
    if(index>=frozen_inputs_->size() || (*frozen_inputs_)[index]!=actual)
      return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
          "Generated bearing input differs from frozen common observation"});
  }
  auto payload = std::vector<std::byte>{bytes->begin(), bytes->end()};
  const auto queued = queue_store_.get().Enqueue(
      node_id,
      contracts::DigitalPacket{
          packet_id,
          node_id,
          contracts::UnicastDestination{
              config_.fusion_center_node_id()},
          std::move(payload),config_.identified_flows
              ? std::optional{contracts::FlowId{node_id.value()}} : std::nullopt});
  if(!queued) return queued;
  generated_cycle_inputs_.insert(generated_position, input);
  generated_reports_.push_back(
      GeneratedFeatureReport{cycle_id, node_id, packet_id, now, *report});
  ++sequence->next_sequence;
  ++next_packet_id_;
  return {};
}

inline auto AcceptanceFeatureApplication::OnRuntimeDecision(
    contracts::PlanningCycleId,
    contracts::SimTime now,
    const runtime::internal::CycleWorkingState& working_state)
    -> contracts::Status {
  const auto sensors = config_.sensor_node_ids();
  for(const auto& delivery : delivery_store_.get().deliveries()) {
    if(delivery.receiver_node_id != config_.fusion_center_node_id() ||
       !std::binary_search(
           sensors.begin(), sensors.end(), delivery.packet.source_node_id)) {
      continue;
    }
    const auto* destination = std::get_if<contracts::UnicastDestination>(
        &delivery.packet.destination);
    if(destination == nullptr ||
       destination->node_id != config_.fusion_center_node_id()) {
      continue;
    }
    const auto payload = std::span<const std::byte>{delivery.packet.payload};
    if(payload.size() != kDetectionFeatureV1PayloadBytes ||
       std::to_integer<std::uint8_t>(payload.front()) !=
           kDetectionFeatureV1TypeAndVersion) {
      continue;
    }
    const auto report = DecodeDetectionFeatureReportV1(payload);
    if(!report) return std::unexpected(report.error());
    const auto ingested = fusion_accumulator_.Ingest(
        delivery.packet.source_node_id, *report);
    if(!ingested) return std::unexpected(ingested.error());
  }
  const auto fusion_center = working_state.ProjectNodeState(
      config_.fusion_center_node_id(), now);
  if(!fusion_center) return std::unexpected(fusion_center.error());
  const auto fused = fusion_accumulator_.TryFuse(
      now,
      fusion_center->motion.position.x_meters,
      fusion_center->motion.position.y_meters);
  if(!fused) return std::unexpected(fused.error());
  if(*fused) {
    const auto trace = contracts::FusionTrace{
        (**fused).fusion_sequence,
        config_.fusion_center_node_id(),
        (**fused).started_at,
        (**fused).completed_at,
        static_cast<std::uint64_t>((**fused).observation_count),
        (**fused).estimated_target_x_meters,
        (**fused).estimated_target_y_meters};
    const auto stored = fusion_result_store_.Append(std::move(**fused));
    if(!stored) return stored;
    if(trace_sink_ != nullptr) {
      const auto event = contracts::TraceEvent::Create(now, trace);
      if(event) {
        const auto ignored = trace_sink_->Emit(*event);
        static_cast<void>(ignored);
      }
    }
  }
  return {};
}

}  // namespace ns3_factory::assembly::internal
