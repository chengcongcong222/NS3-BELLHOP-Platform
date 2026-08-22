#pragma once

#include <algorithm>
#include <functional>
#include <utility>
#include <variant>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

#include "internal/candidate_receiver_resolver.hpp"
#include "internal/cycle_signal_runtime.hpp"
#include "internal/cycle_working_state.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/transmission_session.hpp"
#include "internal/transmission_session_event_sink.hpp"
#include "internal/tx_preparation.hpp"

namespace ns3_factory::runtime::internal {

struct ExecutedTxStart final {
  TransmissionSession session;
};

struct UnusedNoPacketTxStart final {};
struct UnusedNoRouteTxStart final {};

using PlanBoundTxStartOutcome =
    std::variant<ExecutedTxStart,
                 UnusedNoPacketTxStart,
                 UnusedNoRouteTxStart>;

class PlanBoundTxRuntime final {
 public:
  [[nodiscard]] static auto Create(
      contracts::ProtocolCyclePlan plan,
      CycleWorkingState& working_state,
      PacketQueueStore& queue_store,
      const TxPreparationService& preparation_service,
      const CandidateReceiverResolver& candidate_resolver,
      CycleSignalRuntime& signal_runtime,
      ITransmissionSessionEventSink& event_sink)
      -> contracts::Result<PlanBoundTxRuntime>;

  PlanBoundTxRuntime(const PlanBoundTxRuntime&) = delete;
  auto operator=(const PlanBoundTxRuntime&) -> PlanBoundTxRuntime& = delete;
  PlanBoundTxRuntime(PlanBoundTxRuntime&&) noexcept = default;
  auto operator=(PlanBoundTxRuntime&&) noexcept
      -> PlanBoundTxRuntime& = default;

  [[nodiscard]] auto HandleTxStart(
      const contracts::TxOpportunity& opportunity,
      contracts::SimTime now) -> contracts::Result<PlanBoundTxStartOutcome>;

 private:
  PlanBoundTxRuntime(
      contracts::ProtocolCyclePlan plan,
      CycleWorkingState& working_state,
      PacketQueueStore& queue_store,
      const TxPreparationService& preparation_service,
      const CandidateReceiverResolver& candidate_resolver,
      CycleSignalRuntime& signal_runtime,
      ITransmissionSessionEventSink& event_sink) noexcept
      : plan_(std::move(plan)),
        working_state_(working_state),
        queue_store_(queue_store),
        preparation_service_(preparation_service),
        candidate_resolver_(candidate_resolver),
        signal_runtime_(signal_runtime),
        event_sink_(event_sink) {}

  contracts::ProtocolCyclePlan plan_;
  std::reference_wrapper<CycleWorkingState> working_state_;
  std::reference_wrapper<PacketQueueStore> queue_store_;
  std::reference_wrapper<const TxPreparationService> preparation_service_;
  std::reference_wrapper<const CandidateReceiverResolver> candidate_resolver_;
  std::reference_wrapper<CycleSignalRuntime> signal_runtime_;
  std::reference_wrapper<ITransmissionSessionEventSink> event_sink_;
};

inline auto PlanBoundTxRuntime::Create(
    contracts::ProtocolCyclePlan plan,
    CycleWorkingState& working_state,
    PacketQueueStore& queue_store,
    const TxPreparationService& preparation_service,
    const CandidateReceiverResolver& candidate_resolver,
    CycleSignalRuntime& signal_runtime,
    ITransmissionSessionEventSink& event_sink)
    -> contracts::Result<PlanBoundTxRuntime> {
  if(plan.timing().cycle_id() != working_state.cycle_id() ||
     plan.timing().base_snapshot_version() !=
         working_state.base_snapshot().version()) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "ProtocolCyclePlan provenance does not match CycleWorkingState"});
  }

  const auto queue_nodes = queue_store.node_ids();
  const auto cycle_nodes = working_state.base_snapshot().nodes();
  const auto same_universe =
      queue_nodes.size() == cycle_nodes.size() &&
      std::equal(queue_nodes.begin(),
                 queue_nodes.end(),
                 cycle_nodes.begin(),
                 [](contracts::NodeId queue_node,
                    const contracts::NodeCommittedState& cycle_node) {
                   return queue_node == cycle_node.node_id;
                 });
  if(!same_universe) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Packet queues and cycle base snapshot use different node "
            "universes"});
  }

  return PlanBoundTxRuntime{std::move(plan),
                            working_state,
                            queue_store,
                            preparation_service,
                            candidate_resolver,
                            signal_runtime,
                            event_sink};
}

inline auto PlanBoundTxRuntime::HandleTxStart(
    const contracts::TxOpportunity& opportunity,
    contracts::SimTime now) -> contracts::Result<PlanBoundTxStartOutcome> {
  const auto opportunities = plan_.mac_plan().tx_opportunities();
  if(std::find(opportunities.begin(), opportunities.end(), opportunity) ==
     opportunities.end()) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "TxOpportunity does not belong to the bound plan"});
  }
  if(now != opportunity.eligible_at) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "TxStart time does not exactly match TxOpportunity"});
  }

  auto preparation = preparation_service_.get().Prepare(
      opportunity, queue_store_.get(), plan_.routing_plan());
  if(!preparation) {
    return std::unexpected(preparation.error());
  }
  if(std::holds_alternative<NoPacketTxPreparation>(*preparation)) {
    return UnusedNoPacketTxStart{};
  }
  if(std::holds_alternative<NoRouteTxPreparation>(*preparation)) {
    return UnusedNoRouteTxStart{};
  }

  auto ready = std::get<ReadyTxPreparation>(std::move(*preparation));
  auto candidates = candidate_resolver_.get().Resolve(
      working_state_.get(), opportunity.sender_node_id);
  if(!candidates) {
    return std::unexpected(candidates.error());
  }

  const auto queue_owner = ready.selected_packet.queue_owner;
  const auto packet_id = ready.selected_packet.packet.packet_id;
  TransmissionExecutionRequest request{
      opportunity,
      std::move(ready.selected_packet.packet),
      std::move(ready.target),
      now,
      std::move(*candidates)};
  auto session = signal_runtime_.get().HandleTxStart(now, std::move(request));
  if(!session) {
    return std::unexpected(session.error());
  }

  const auto published = event_sink_.get().Publish(*session);
  if(!published) {
    return std::unexpected(published.error());
  }

  const auto consumed =
      queue_store_.get().ConsumeFront(queue_owner, packet_id);
  if(!consumed) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Physical transmission succeeded but conditional queue consume "
            "failed"});
  }
  return ExecutedTxStart{std::move(*session)};
}

}  // namespace ns3_factory::runtime::internal
