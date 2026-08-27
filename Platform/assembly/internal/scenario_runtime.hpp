#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

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
#include "internal/protocol_cycle_planner.hpp"
#include "internal/receiver_processor.hpp"
#include "internal/reception_disposition_applier.hpp"
#include "internal/reception_disposition_service.hpp"
#include "internal/reception_result_accumulator.hpp"
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

class ScenarioRuntime final {
 public:
  // A non-null caller-provided sink must outlive this runtime. Omitting it
  // composes the assembly-owned NullTraceSink.
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
      contracts::ITraceSink* trace_sink = nullptr) noexcept
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
        first_cycle_id_(first_cycle_id),
        trace_sink_(trace_sink == nullptr ? &null_trace_sink_ : trace_sink) {}

  ScenarioRuntime(const ScenarioRuntime&) = delete;
  auto operator=(const ScenarioRuntime&) -> ScenarioRuntime& = delete;
  ScenarioRuntime(ScenarioRuntime&&) = delete;
  auto operator=(ScenarioRuntime&&) -> ScenarioRuntime& = delete;

  [[nodiscard]] auto RunCycles(std::size_t cycle_count)
      -> contracts::Status;

  [[nodiscard]] constexpr auto state() const noexcept
      -> ScenarioRuntimeState {
    return state_;
  }

  [[nodiscard]] constexpr auto previous_connectivity() const noexcept
      -> const std::optional<contracts::ConnectivityGraph>& {
    return previous_connectivity_;
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
  const contracts::IRxPhy& rx_phy_;
  contracts::PlanningCycleId first_cycle_id_;
  contracts::NullTraceSink null_trace_sink_;
  contracts::ITraceSink* trace_sink_;
  ScenarioRuntimeState state_{ScenarioRuntimeState::kReady};
  std::optional<contracts::ConnectivityGraph> previous_connectivity_;
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

  std::optional<std::reference_wrapper<
      const contracts::ConnectivityGraph>> previous;
  if(previous_connectivity_) previous = std::cref(*previous_connectivity_);
  auto structure = structure_builder_.Build(
      structure::internal::StructureBuildRequest{
          cycle_id, snapshot, previous});
  if(!structure) return std::unexpected(structure.error());
  auto plan_result = cycle_planner_.Build(snapshot, *structure);
  if(!plan_result) return std::unexpected(plan_result.error());
  std::optional<contracts::ProtocolCyclePlan> plan{
      std::move(*plan_result)};
  const auto provenance =
      ValidateProvenance(cycle_id, snapshot, *structure, *plan);
  if(!provenance) return provenance;
  if(plan->timing().starts_at() < snapshot.committed_at() ||
     plan->timing().starts_at() < *now) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Cycle plan starts before snapshot or kernel time"});
  }

  auto working_state = runtime::internal::CycleWorkingState::Create(
      snapshot, cycle_id, plan->timing().starts_at());
  if(!working_state) return std::unexpected(working_state.error());

  runtime::internal::TransmissionExecutor transmission_executor{
      id_allocator_, tx_phy_, channel_provider_};
  runtime::internal::ReceiverProcessor receiver_processor{
      id_allocator_, noise_provider_, rx_phy_};
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
  runtime::internal::FifoPacketSelector selector;
  runtime::internal::TxPreparationService preparation{selector};
  runtime::internal::CandidateReceiverResolver candidate_resolver;
  kernel::internal::EventDispatcher dispatcher{gateway_};
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
  PlanBoundExecutionHook execution_hook{*tx_runtime, signal_runtime};
  kernel::internal::PlanInstaller installer{dispatcher};
  kernel::internal::CycleCoordinator coordinator{installer,
                                                  execution_hook};
  const auto installed = coordinator.InstallPlan(*plan, base_version);
  if(!installed) return std::unexpected(installed.error());
  const auto timing = plan->timing();
  plan.reset();

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

  previous_connectivity_ = structure->connectivity_graph();
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
    if(!status) return Fail(status.error());
  }
  state_ = ScenarioRuntimeState::kCompleted;
  return {};
}

}  // namespace ns3_factory::assembly::internal
