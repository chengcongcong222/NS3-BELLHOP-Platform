#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/trace.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/application_delivery_store.hpp"
#include "internal/applied_mac_schedule.hpp"
#include "internal/candidate_receiver_resolver.hpp"
#include "internal/commit_service.hpp"
#include "internal/communication_id_allocator.hpp"
#include "internal/cycle_coordinator.hpp"
#include "internal/cycle_signal_runtime.hpp"
#include "internal/cycle_working_state.hpp"
#include "internal/event_dispatcher.hpp"
#include "internal/fifo_packet_selector.hpp"
#include "internal/in_flight_signal_ledger.hpp"
#include "internal/ns3_kernel_gateway.hpp"
#include "internal/ns3_transmission_session_event_sink.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/plan_bound_execution_hook.hpp"
#include "internal/plan_bound_tx_runtime.hpp"
#include "internal/plan_installer.hpp"
#include "internal/phy_execution_composition.hpp"
#include "internal/recovered_payload_required_rx_phy.hpp"
#include "internal/protocol_cycle_planner.hpp"
#include "internal/receiver_processor.hpp"
#include "internal/reception_disposition_applier.hpp"
#include "internal/reception_disposition_service.hpp"
#include "internal/reception_result_accumulator.hpp"
#include "internal/scenario_cycle_application.hpp"
#include "internal/flow_demand_snapshot.hpp"
#include "internal/structure_builder.hpp"
#include "internal/transmission_executor.hpp"
#include "internal/transmission_record_store.hpp"
#include "internal/tx_preparation.hpp"
#include "internal/world_state_store.hpp"

namespace ns3_factory::assembly::internal {

enum class ScenarioRuntimeState {
  kReady,
  kRunning,
  kCompleted,
  kFailed,
};

enum class MacScheduleUpdatePolicy {
  kNetworkUpdateCycle,
  kEveryCycle,
};

class ScenarioRuntime final {
 public:
  // Non-null caller-provided trace/application hooks must outlive this
  // runtime. Omitting the trace sink composes the assembly-owned null sink.
  ScenarioRuntime(
      kernel::internal::Ns3KernelGateway& gateway,
      runtime::internal::WorldStateStore& world_store,
      runtime::internal::PacketQueueStore& queue_store,
      runtime::internal::ApplicationDeliveryStore& delivery_store,
      runtime::internal::CommunicationIdAllocator& id_allocator,
      const structure::internal::StructureBuilder& structure_builder,
      const planning::internal::IProtocolCyclePlanner& cycle_planner,
      const contracts::ITxPhy& tx_phy,
      const contracts::IChannelFieldProvider& channel_provider,
      const contracts::INoiseFieldProvider& noise_provider,
      const contracts::IRxPhy& rx_phy,
      contracts::PlanningCycleId first_cycle_id,
      contracts::ITraceSink* trace_sink = nullptr,
      std::size_t network_update_interval_cycles = 1,
      IScenarioCycleApplication* cycle_application = nullptr,
      MacScheduleUpdatePolicy mac_update_policy =
          MacScheduleUpdatePolicy::kNetworkUpdateCycle) noexcept
      : gateway_(gateway),
        world_store_(world_store),
        queue_store_(queue_store),
        delivery_store_(delivery_store),
        id_allocator_(id_allocator),
        structure_builder_(structure_builder),
        cycle_planner_(cycle_planner),
        tx_phy_(tx_phy),
        channel_provider_(channel_provider),
        noise_provider_(noise_provider),
        rx_phy_(rx_phy),
        required_payload_rx_(rx_phy),
        first_cycle_id_(first_cycle_id),
        trace_sink_(trace_sink == nullptr ? &null_trace_sink_ : trace_sink),
        network_update_interval_cycles_(network_update_interval_cycles),
        cycle_application_(cycle_application),
        mac_update_policy_(mac_update_policy) {}

  ScenarioRuntime(
      kernel::internal::Ns3KernelGateway& gateway,
      runtime::internal::WorldStateStore& world_store,
      runtime::internal::PacketQueueStore& queue_store,
      runtime::internal::ApplicationDeliveryStore& delivery_store,
      runtime::internal::CommunicationIdAllocator& id_allocator,
      const structure::internal::StructureBuilder& structure_builder,
      const planning::internal::IProtocolCyclePlanner& cycle_planner,
      const PhyExecutionComposition& phy,
      contracts::PlanningCycleId first_cycle_id,
      contracts::ITraceSink* trace_sink = nullptr,
      std::size_t network_update_interval_cycles = 1,
      IScenarioCycleApplication* cycle_application = nullptr,
      MacScheduleUpdatePolicy mac_update_policy =
          MacScheduleUpdatePolicy::kNetworkUpdateCycle) noexcept
      : ScenarioRuntime(gateway,
                        world_store,
                        queue_store,
                        delivery_store,
                        id_allocator,
                        structure_builder,
                        cycle_planner,
                        phy.tx_phy(),
                        phy.channel_provider(),
                        phy.noise_provider(),
                        phy.rx_phy(),
                        first_cycle_id,
                        trace_sink,
                        network_update_interval_cycles,
                        cycle_application,
                        mac_update_policy) {
    physical_artifact_lifecycle_ = phy.artifact_lifecycle();
    if(phy.path() == PhyExecutionPath::kWaveform) {
      rx_phy_ = required_payload_rx_;
    }
  }

  ScenarioRuntime(const ScenarioRuntime&) = delete;
  auto operator=(const ScenarioRuntime&) -> ScenarioRuntime& = delete;
  ScenarioRuntime(ScenarioRuntime&&) = delete;
  auto operator=(ScenarioRuntime&&) -> ScenarioRuntime& = delete;

  [[nodiscard]] auto RunCycles(std::size_t cycle_count)
      -> contracts::Status;
  auto BindPlanningEvidence(contracts::IPlanningEvidenceSink* sink) -> contracts::Status {
    if(state_!=ScenarioRuntimeState::kReady) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,"Planning evidence must be bound before execution"});
    planning_evidence_=sink;return {};
  }

  [[nodiscard]] auto BindTrajectories(
      std::shared_ptr<const contracts::INodeTrajectory> trajectories) -> contracts::Status {
    if(state_ != ScenarioRuntimeState::kReady || !trajectories) {
      return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
          "Trajectory binding requires a ready run and nonnull immutable input"});
    }
    auto validation = runtime::internal::CycleWorkingState::Create(
        world_store_.current_snapshot(), first_cycle_id_,
        world_store_.current_snapshot().committed_at(), trajectories);
    if(!validation) return std::unexpected(validation.error());
    trajectories_ = std::move(trajectories);
    return {};
  }

  // Explicit immutable physical declarations for planned algorithms. The
  // assembly caller derives these bounds from its selected workload/PHY and
  // already loaded field; runtime/planners do not query either here.
  [[nodiscard]] auto BindPlanningInputs(
      std::vector<contracts::NodeAirtime> airtimes,
      contracts::SimDuration maximum_propagation_tail,
      bool allow_receiver_overlap = false,
      contracts::SimDuration emission_quantum = contracts::SimDuration::FromNanoseconds(1)) -> contracts::Status {
    if(state_ != ScenarioRuntimeState::kReady || planning_airtimes_ ||
       maximum_propagation_tail < contracts::SimDuration::Zero() ||
       emission_quantum <= contracts::SimDuration::Zero()) {
      return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
          "Planning inputs require a ready run, a single binding and a non-negative tail"});
    }
    std::sort(airtimes.begin(), airtimes.end(), [](const auto& a, const auto& b) {
      return a.node_id < b.node_id;
    });
    const auto nodes = world_store_.current_snapshot().nodes();
    if(airtimes.size() != nodes.size()) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument, "Planning airtime universe is incomplete"});
    for(std::size_t i = 0; i < nodes.size(); ++i) {
      if(airtimes[i].node_id != nodes[i].node_id ||
         airtimes[i].maximum_packet_airtime < contracts::SimDuration::Zero() ||
         (nodes[i].capability.can_transmit &&
          airtimes[i].maximum_packet_airtime == contracts::SimDuration::Zero())) {
        return std::unexpected(contracts::Error{contracts::ErrorCode::kInvalidArgument,
            "Planning airtime identity or duration is invalid"});
      }
    }
    planning_airtimes_ = std::move(airtimes);
    maximum_propagation_tail_ = maximum_propagation_tail;
    allow_receiver_overlap_ = allow_receiver_overlap;
    emission_quantum_ = emission_quantum;
    return {};
  }

  [[nodiscard]] constexpr auto state() const noexcept
      -> ScenarioRuntimeState {
    return state_;
  }

  [[nodiscard]] constexpr auto previous_connectivity() const noexcept
      -> const std::optional<contracts::ConnectivityGraph>& {
    return previous_connectivity_;
  }

  [[nodiscard]] constexpr auto candidate_plan_build_count() const noexcept
      -> std::size_t {
    return candidate_plan_build_count_;
  }

  [[nodiscard]] constexpr auto network_refresh_count() const noexcept
      -> std::size_t {
    return network_refresh_count_;
  }

  [[nodiscard]] constexpr auto applied_schedule_update_count() const noexcept
      -> std::size_t {
    return applied_schedule_update_count_;
  }

 private:
  class SimulatorDestroyGuard final {
   public:
    explicit SimulatorDestroyGuard(
        kernel::internal::Ns3KernelGateway& gateway) noexcept
        : gateway_(gateway) {}

    ~SimulatorDestroyGuard() { gateway_.Destroy(); }

   private:
    kernel::internal::Ns3KernelGateway& gateway_;
  };

  class PhysicalArtifactRunGuard final {
   public:
    explicit PhysicalArtifactRunGuard(
        phy::internal::IPhysicalArtifactLifecycle* lifecycle) noexcept
        : lifecycle_(lifecycle) {}

    ~PhysicalArtifactRunGuard() {
      if(lifecycle_ != nullptr) lifecycle_->ReleaseCycleArtifacts();
    }

   private:
    phy::internal::IPhysicalArtifactLifecycle* lifecycle_;
  };

  [[nodiscard]] auto RunOneCycle(contracts::PlanningCycleId cycle_id)
      -> contracts::Status;

  [[nodiscard]] static auto ValidateProvenance(
      contracts::PlanningCycleId cycle_id,
      const contracts::WorldSnapshot& snapshot,
      const contracts::StructureSnapshot& structure,
      const contracts::ProtocolCyclePlan& plan) -> contracts::Status;

  [[nodiscard]] auto Fail(contracts::Error error) -> contracts::Status {
    state_ = ScenarioRuntimeState::kFailed;
    return std::unexpected(std::move(error));
  }

  kernel::internal::Ns3KernelGateway& gateway_;
  runtime::internal::WorldStateStore& world_store_;
  runtime::internal::PacketQueueStore& queue_store_;
  runtime::internal::ApplicationDeliveryStore& delivery_store_;
  runtime::internal::CommunicationIdAllocator& id_allocator_;
  const structure::internal::StructureBuilder& structure_builder_;
  const planning::internal::IProtocolCyclePlanner& cycle_planner_;
  const contracts::ITxPhy& tx_phy_;
  const contracts::IChannelFieldProvider& channel_provider_;
  const contracts::INoiseFieldProvider& noise_provider_;
  std::reference_wrapper<const contracts::IRxPhy> rx_phy_;
  RecoveredPayloadRequiredRxPhy required_payload_rx_;
  std::shared_ptr<const contracts::INodeTrajectory> trajectories_;
  contracts::PlanningCycleId first_cycle_id_;
  contracts::NullTraceSink null_trace_sink_;
  contracts::ITraceSink* trace_sink_;
  std::size_t network_update_interval_cycles_;
  IScenarioCycleApplication* cycle_application_;
  MacScheduleUpdatePolicy mac_update_policy_;
  std::optional<std::vector<contracts::NodeAirtime>> planning_airtimes_;
  contracts::SimDuration maximum_propagation_tail_{contracts::SimDuration::Zero()};
  bool allow_receiver_overlap_{false};
  contracts::SimDuration emission_quantum_{contracts::SimDuration::FromNanoseconds(1)};
  phy::internal::IPhysicalArtifactLifecycle*
      physical_artifact_lifecycle_{nullptr};
  ScenarioRuntimeState state_{ScenarioRuntimeState::kReady};
  std::optional<contracts::ConnectivityGraph> previous_connectivity_;
  std::optional<contracts::StructureSnapshot> applied_structure_;
  std::optional<contracts::StructureOrigin> applied_structure_origin_;
  contracts::PreviousCycleFeedback previous_planning_feedback_;
  contracts::IPlanningEvidenceSink* planning_evidence_{};
  std::optional<AppliedMacSchedule> applied_mac_schedule_;
  std::size_t completed_cycle_count_{0};
  std::size_t candidate_plan_build_count_{0};
  std::size_t network_refresh_count_{0};
  std::size_t applied_schedule_update_count_{0};
};

inline auto ScenarioRuntime::ValidateProvenance(
    contracts::PlanningCycleId cycle_id,
    const contracts::WorldSnapshot& snapshot,
    const contracts::StructureSnapshot& structure,
    const contracts::ProtocolCyclePlan& plan) -> contracts::Status {
  if(structure.cycle_id() != cycle_id ||
     structure.base_snapshot_version() != snapshot.version() ||
     plan.timing().cycle_id() != cycle_id ||
     plan.timing().base_snapshot_version() != snapshot.version()) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Scenario planning provenance does not match authoritative "
            "cycle state"});
  }
  if(const auto& routing = plan.routing_plan();
     routing &&
     (routing->cycle_id() != cycle_id ||
      routing->base_snapshot_version() != snapshot.version())) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "RoutingPlan provenance does not match authoritative cycle "
            "state"});
  }
  return {};
}

inline auto ScenarioRuntime::RunOneCycle(
    contracts::PlanningCycleId cycle_id) -> contracts::Status {
  const auto& snapshot = world_store_.current_snapshot();
  const auto base_version = snapshot.version();
  const auto now = gateway_.PlatformNow();
  if(!now) return std::unexpected(now.error());
  if(*now > snapshot.committed_at()) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Kernel time is later than the authoritative snapshot time"});
  }

  const auto is_network_update_cycle =
      completed_cycle_count_ % network_update_interval_cycles_ == 0;
  contracts::Result<contracts::StructureSnapshot> structure =
      std::unexpected(
          contracts::Error{contracts::ErrorCode::kInternal,
                           "Structure selection was not initialized"});
  if(is_network_update_cycle) {
    std::optional<std::reference_wrapper<
        const contracts::ConnectivityGraph>> previous;
    if(previous_connectivity_) previous = std::cref(*previous_connectivity_);
    structure = structure_builder_.Build(
        structure::internal::StructureBuildRequest{
            cycle_id, snapshot, previous});
  } else if(applied_structure_) {
    structure = contracts::StructureSnapshot::Create(
        cycle_id,
        snapshot.version(),
        applied_structure_->role_table(),
        applied_structure_->connectivity_graph(),
        applied_structure_->logical_topology());
  } else {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Non-update cycle has no applied network structure"});
  }
  if(!structure) return std::unexpected(structure.error());
  std::optional<contracts::PlanningInput> planning_input;
  if(planning_airtimes_) {
    std::vector<contracts::NodeTrafficDemand> demands;
    for(const auto& airtime : *planning_airtimes_) {
      const auto queued = queue_store_.size(airtime.node_id);
      if(!queued) return std::unexpected(queued.error());
      contracts::Result<std::uint64_t> ready = std::uint64_t{0};
      if(cycle_application_ != nullptr) {
        ready = cycle_application_->forecast_packet_count(cycle_id, airtime.node_id);
      }
      if(!ready) return std::unexpected(ready.error());
      demands.push_back({airtime.node_id, *queued, *ready, airtime.maximum_packet_airtime});
    }
    auto input = contracts::PlanningInput::Create(cycle_id, snapshot.version(),
        snapshot.committed_at(), std::move(demands), maximum_propagation_tail_, allow_receiver_overlap_,
        emission_quantum_);
    if(!input) return std::unexpected(input.error());
    planning_input = std::move(*input);
    const auto valid_input = contracts::ValidatePlanningInput(
        *planning_input, snapshot, *structure);
    if(!valid_input) return valid_input;
  }
  const bool flow_planning = cycle_planner_.uses_flow_planning();
  const contracts::StructureOrigin structure_origin = is_network_update_cycle
      ? contracts::StructureOrigin{cycle_id, snapshot.version(), snapshot.committed_at()}
      : applied_structure_origin_.value();
  std::optional<contracts::FlowPlanningState> flow_state;
  if(flow_planning) {
    if(!planning_input || mac_update_policy_ != MacScheduleUpdatePolicy::kEveryCycle ||
       (cycle_application_ && cycle_application_->input_timing() != ApplicationInputTiming::kCycleStart)) {
      return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
          "Flow planning requires physical inputs, cycle-start input and fresh per-cycle planning"});
    }
    auto forecast = cycle_application_ ? cycle_application_->forecast_flows(cycle_id)
        : contracts::Result<std::vector<contracts::FlowInputForecast>>{std::vector<contracts::FlowInputForecast>{}};
    if(!forecast) return std::unexpected(forecast.error());
    auto demands = SnapshotFlowDemands(queue_store_,*planning_input,*forecast);
    if(!demands) return std::unexpected(demands.error());
    flow_state = contracts::FlowPlanningState{structure_origin,std::move(*demands),previous_planning_feedback_};
  }
  auto candidate_result = flow_state
      ? cycle_planner_.BuildFlow(snapshot,*structure,*planning_input,*flow_state)
      : planning_input
      ? cycle_planner_.Build(snapshot, *structure, *planning_input)
      : cycle_planner_.Build(snapshot, *structure);
  if(!candidate_result) return std::unexpected(candidate_result.error());
  if(planning_input) {
    const auto admitted = contracts::ValidatePlanningPlan(
        *candidate_result, *planning_input, snapshot, *structure);
    if(!admitted) return admitted;
  }
  ++candidate_plan_build_count_;
  std::optional<contracts::ProtocolCyclePlan> candidate_plan{
      std::move(*candidate_result)};
  const auto provenance =
      ValidateProvenance(cycle_id, snapshot, *structure, *candidate_plan);
  if(!provenance) return provenance;
  if(candidate_plan->timing().starts_at() < snapshot.committed_at() ||
     candidate_plan->timing().starts_at() < *now) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Cycle plan starts before snapshot or kernel time"});
  }

  std::optional<AppliedMacSchedule> candidate_schedule;
  if(!flow_planning) {
    auto captured = AppliedMacSchedule::Capture(*candidate_plan);
    if(!captured) return std::unexpected(captured.error());
    candidate_schedule = std::move(*captured);
  }
  const bool apply_candidate_mac = is_network_update_cycle ||
      mac_update_policy_ == MacScheduleUpdatePolicy::kEveryCycle;
  contracts::Result<contracts::ProtocolCyclePlan> execution_result =
      apply_candidate_mac
          ? contracts::Result<contracts::ProtocolCyclePlan>{*candidate_plan}
          : applied_mac_schedule_
                ? applied_mac_schedule_->Bind(
                      *structure, snapshot.committed_at())
                : contracts::Result<contracts::ProtocolCyclePlan>{
                      std::unexpected(
                          contracts::Error{
                              contracts::ErrorCode::kFailedPrecondition,
                              "Non-update cycle has no applied MAC schedule"})};
  if(!execution_result) return std::unexpected(execution_result.error());
  std::optional<contracts::ProtocolCyclePlan> plan{
      std::move(*execution_result)};
  const auto execution_provenance =
      ValidateProvenance(cycle_id, snapshot, *structure, *plan);
  if(!execution_provenance) return execution_provenance;

  auto working_state = runtime::internal::CycleWorkingState::Create(
      snapshot, cycle_id, plan->timing().starts_at(), trajectories_);
  if(!working_state) return std::unexpected(working_state.error());

  // Reject insufficient immutable input coverage before publishing any events.
  for(const auto& node : snapshot.nodes()) {
    auto endpoint = working_state->ProjectNodeState(node.node_id, plan->timing().closes_at());
    if(!endpoint) return std::unexpected(endpoint.error());
  }

  runtime::internal::TransmissionExecutor transmission_executor{
      id_allocator_, tx_phy_, channel_provider_};
  runtime::internal::ReceiverProcessor receiver_processor{
      id_allocator_, noise_provider_, rx_phy_.get()};
  runtime::internal::CommitService commit_service{world_store_};
  runtime::internal::InFlightSignalLedger ledger;
  runtime::internal::ReceptionResultAccumulator reception_results;
  runtime::internal::TransmissionRecordStore transmission_records;
  runtime::internal::ReceptionDispositionService disposition_service;
  runtime::internal::ReceptionDispositionApplier disposition_applier{
      queue_store_, delivery_store_};
  runtime::internal::CycleSignalRuntime signal_runtime{
      transmission_executor,
      receiver_processor,
      *working_state,
      commit_service,
      ledger,
      reception_results,
      transmission_records,
      disposition_service,
      disposition_applier,
      base_version,
      plan->timing().closes_at(),
      trace_sink_};
  runtime::internal::PlanningFeedbackStore planning_feedback;
  if(flow_planning) signal_runtime.BindPlanningFeedback(planning_feedback);
  runtime::internal::FifoPacketSelector selector;
  runtime::internal::TxPreparationService preparation{selector};
  runtime::internal::CandidateReceiverResolver candidate_resolver;
  kernel::internal::EventDispatcher dispatcher{gateway_};
  if(cycle_application_ != nullptr) {
    auto input_nodes = std::vector<contracts::NodeId>{
        cycle_application_->input_node_ids().begin(),
        cycle_application_->input_node_ids().end()};
    std::sort(input_nodes.begin(), input_nodes.end());
    if(std::adjacent_find(input_nodes.begin(), input_nodes.end()) !=
       input_nodes.end()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kAlreadyExists,
                           "Cycle application input node is duplicated"});
    }
    std::vector<contracts::NodeId> scheduled_inputs;
    std::vector<kernel::internal::ScheduledEventIntent> application_events;
    application_events.reserve(input_nodes.size() + 1);
    auto* const application = cycle_application_;
    auto* const current_working_state = &*working_state;
    auto* const gateway = &gateway_;
    std::vector<contracts::TxOpportunity> input_opportunities;
    if(application->input_timing() == ApplicationInputTiming::kCycleStart) {
      for(const auto node_id : input_nodes) {
        const auto node = snapshot.FindNode(node_id);
        if(!node || !node->get().capability.can_transmit) {
          return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
              "Cycle application input node cannot transmit"});
        }
        input_opportunities.push_back({node_id, plan->timing().starts_at()});
      }
    } else {
      const auto opportunities = plan->mac_plan().tx_opportunities();
      input_opportunities.assign(opportunities.begin(), opportunities.end());
    }
    for(const auto& opportunity : input_opportunities) {
      if(!std::binary_search(input_nodes.begin(),
                             input_nodes.end(),
                             opportunity.sender_node_id) ||
         std::binary_search(scheduled_inputs.begin(),
                            scheduled_inputs.end(),
                            opportunity.sender_node_id)) {
        continue;
      }
      const auto insertion = std::lower_bound(
          scheduled_inputs.begin(),
          scheduled_inputs.end(),
          opportunity.sender_node_id);
      scheduled_inputs.insert(insertion, opportunity.sender_node_id);
      const auto expected_time = opportunity.eligible_at;
      const auto input_node_id = opportunity.sender_node_id;
      application_events.push_back(
          kernel::internal::ScheduledEventIntent{
              expected_time,
              kernel::internal::EventPhase::kInputReady,
              [application,
               current_working_state,
               gateway,
               cycle_id,
               input_node_id,
               expected_time]() -> contracts::Status {
                const auto now = gateway->PlatformNow();
                if(!now) return std::unexpected(now.error());
                if(*now != expected_time) {
                  return std::unexpected(
                      contracts::Error{
                          contracts::ErrorCode::kFailedPrecondition,
                          "Application input event time mismatch"});
                }
                return application->OnInputReady(
                    cycle_id, input_node_id, *now, *current_working_state);
              }});
    }
    if(scheduled_inputs != input_nodes) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kFailedPrecondition,
              "Cycle application input node has no TxOpportunity"});
    }
    const auto decision_time = plan->timing().closes_at();
    application_events.push_back(
        kernel::internal::ScheduledEventIntent{
            decision_time,
            kernel::internal::EventPhase::kRuntimeDecision,
            [application,
             current_working_state,
             gateway,
             cycle_id,
             decision_time]() -> contracts::Status {
              const auto now = gateway->PlatformNow();
              if(!now) return std::unexpected(now.error());
              if(*now != decision_time) {
                return std::unexpected(
                    contracts::Error{
                        contracts::ErrorCode::kFailedPrecondition,
                        "Application decision event time mismatch"});
              }
              return application->OnRuntimeDecision(
                  cycle_id, *now, *current_working_state);
            }});
    const auto scheduled_application_events =
        dispatcher.ScheduleBatch(std::move(application_events));
    if(!scheduled_application_events) {
      return std::unexpected(scheduled_application_events.error());
    }
  }
  Ns3TransmissionSessionEventSink event_sink{dispatcher, signal_runtime};
  auto tx_runtime = runtime::internal::PlanBoundTxRuntime::Create(
      *plan,
      *working_state,
      queue_store_,
      preparation,
      candidate_resolver,
      signal_runtime,
      event_sink);
  if(!tx_runtime) return std::unexpected(tx_runtime.error());
  if(flow_planning) tx_runtime->BindPlanningEvidence(planning_evidence_);
  PlanBoundExecutionHook execution_hook{*tx_runtime, signal_runtime};
  kernel::internal::PlanInstaller installer{dispatcher};
  kernel::internal::CycleCoordinator coordinator{installer,
                                                  execution_hook};
  const auto installed = coordinator.InstallPlan(*plan, base_version);
  if(!installed) return std::unexpected(installed.error());
  const auto timing = plan->timing();
  plan.reset();
  candidate_plan.reset();

  const auto run = dispatcher.Run();
  if(!run) return run;
  const auto& committed = world_store_.current_snapshot();
  const auto last_cycle = world_store_.last_committed_cycle_id();
  if(coordinator.state() !=
         kernel::internal::CycleCoordinatorState::kCompleted ||
     base_version.value() ==
         std::numeric_limits<contracts::SnapshotVersion::value_type>::max() ||
     committed.version().value() != base_version.value() + 1 ||
     committed.committed_at() != timing.closes_at() || !last_cycle ||
     *last_cycle != cycle_id || !ledger.empty() ||
     transmission_records.size() != 0) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Completed cycle did not satisfy commit or cleanup invariants"});
  }

  if(is_network_update_cycle) {
    applied_structure_origin_ = structure_origin;
    previous_connectivity_ = structure->connectivity_graph();
    applied_structure_ = std::move(*structure);
    ++network_refresh_count_;
  }
  if(apply_candidate_mac) {
    applied_mac_schedule_ = std::move(candidate_schedule);
    ++applied_schedule_update_count_;
  }
  if(flow_planning) previous_planning_feedback_ = planning_feedback.Complete(
      cycle_id, committed.version(), timing.starts_at(), timing.closes_at());
  ++completed_cycle_count_;
  return {};
}

inline auto ScenarioRuntime::RunCycles(std::size_t cycle_count)
    -> contracts::Status {
  if(state_ != ScenarioRuntimeState::kReady) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "ScenarioRuntime can run exactly once from the ready state"});
  }
  state_ = ScenarioRuntimeState::kRunning;
  SimulatorDestroyGuard destroy_guard{gateway_};
  PhysicalArtifactRunGuard physical_artifact_guard{
      physical_artifact_lifecycle_};
  if(network_update_interval_cycles_ == 0) {
    return Fail(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "ScenarioRuntime network update interval must be positive"});
  }
  if(mac_update_policy_ != MacScheduleUpdatePolicy::kNetworkUpdateCycle &&
     mac_update_policy_ != MacScheduleUpdatePolicy::kEveryCycle) {
    return Fail(contracts::Error{contracts::ErrorCode::kInvalidArgument,
        "ScenarioRuntime MAC update policy is unsupported"});
  }
  if(cycle_application_ != nullptr &&
     cycle_application_->input_timing() != ApplicationInputTiming::kCycleStart &&
     cycle_application_->input_timing() != ApplicationInputTiming::kLegacyFirstTxOpportunity) {
    return Fail(contracts::Error{contracts::ErrorCode::kInvalidArgument,
        "ScenarioRuntime application input timing is unsupported"});
  }
  if(cycle_count == 0) {
    return Fail(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "ScenarioRuntime requires at least one planning cycle"});
  }

  const auto additional_cycles = cycle_count - 1;
  const auto available =
      std::numeric_limits<contracts::PlanningCycleId::value_type>::max() -
      first_cycle_id_.value();
  if(additional_cycles > available) {
    return Fail(contracts::Error{
        contracts::ErrorCode::kOverflow,
        "ScenarioRuntime PlanningCycleId range overflows"});
  }

  for(std::size_t index = 0; index < cycle_count; ++index) {
    const auto cycle_id = contracts::PlanningCycleId{
        first_cycle_id_.value() + index};
    const auto status = RunOneCycle(cycle_id);
    if(physical_artifact_lifecycle_ != nullptr) {
      physical_artifact_lifecycle_->ReleaseCycleArtifacts();
    }
    if(!status) return Fail(status.error());
  }
  state_ = ScenarioRuntimeState::kCompleted;
  return {};
}

}  // namespace ns3_factory::assembly::internal
