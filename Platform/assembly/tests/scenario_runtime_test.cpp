#include <cstdlib>
#include <limits>
#include <optional>
#include <variant>
#include <vector>

#include "scenario_runtime_test_support.hpp"

using namespace ns3_factory::assembly::internal;
using namespace ns3_factory::assembly::test;

namespace {

class RecordingTraceSink final : public ITraceSink {
 public:
  auto Emit(const TraceEvent& event) noexcept -> Status override {
    events.push_back(event);
    return {};
  }

  std::vector<TraceEvent> events;
};

class AlwaysFailTraceSink final : public ITraceSink {
 public:
  auto Emit(const TraceEvent& event) noexcept -> Status override {
    ++attempt_count;
    if(const auto* reception =
           std::get_if<ReceptionTrace>(&event.payload());
       reception != nullptr && reception->quality) {
      ++quality_reception_count;
    }
    return std::unexpected(
        Error{ErrorCode::kUnavailable, "Injected trace sink failure"});
  }

  std::size_t attempt_count{0};
  std::size_t quality_reception_count{0};
};

auto CountKind(const RecordingTraceSink& sink, TraceKind kind)
    -> std::size_t {
  return static_cast<std::size_t>(std::count_if(
      sink.events.begin(), sink.events.end(), [kind](const TraceEvent& event) {
        return event.kind() == kind;
      }));
}

auto ReceptionDispositions(const RecordingTraceSink& sink)
    -> std::vector<TraceReceptionDisposition> {
  std::vector<TraceReceptionDisposition> values;
  for(const auto& event : sink.events) {
    if(const auto* reception =
           std::get_if<ReceptionTrace>(&event.payload())) {
      values.push_back(reception->disposition);
    }
  }
  return values;
}

auto HasCanonicalTraceOrder(const RecordingTraceSink& sink) -> bool {
  for(std::size_t index = 1; index < sink.events.size(); ++index) {
    if(sink.events[index].occurred_at() <
       sink.events[index - 1].occurred_at()) {
      return false;
    }
  }
  return true;
}

auto TestArgumentPreflightAndTerminalState() -> bool {
  auto zero = RuntimeFixture::Create(PlanningCycleId{7}, {NodeId{0}});
  if(!zero) return false;
  const auto zero_result = zero->runtime.RunCycles(0);
  const auto zero_repeat = zero->runtime.RunCycles(1);
  if(zero_result || zero_result.error().code != ErrorCode::kInvalidArgument ||
     zero_repeat ||
     zero_repeat.error().code != ErrorCode::kFailedPrecondition ||
     zero->runtime.state() != ScenarioRuntimeState::kFailed ||
     zero->planner.build_count != 0 || !zero->estimator.observed_at.empty() ||
     zero->world.current_snapshot().version() != SnapshotVersion{0} ||
     zero->gateway.PlatformNow() != Result<SimTime>{SimTime::Zero()}) {
    return false;
  }

  constexpr auto kMaximumCycle =
      std::numeric_limits<PlanningCycleId::value_type>::max();
  auto overflow = RuntimeFixture::Create(
      PlanningCycleId{kMaximumCycle}, {NodeId{0}, NodeId{1}});
  if(!overflow) return false;
  const auto overflow_result = overflow->runtime.RunCycles(2);
  return !overflow_result &&
         overflow_result.error().code == ErrorCode::kOverflow &&
         overflow->runtime.state() == ScenarioRuntimeState::kFailed &&
         overflow->planner.build_count == 0 &&
         overflow->estimator.observed_at.empty() &&
         overflow->world.current_snapshot().version() == SnapshotVersion{0};
}

auto TestSuccessfulLifecycle() -> bool {
  auto fixture = RuntimeFixture::Create(PlanningCycleId{7}, {NodeId{0}});
  if(!fixture) return false;
  const auto run = fixture->runtime.RunCycles(1);
  const auto repeat = fixture->runtime.RunCycles(1);
  const auto now = fixture->gateway.PlatformNow();
  return run && !repeat &&
         repeat.error().code == ErrorCode::kFailedPrecondition &&
         fixture->runtime.state() == ScenarioRuntimeState::kCompleted &&
         fixture->planner.build_count == 1 &&
         fixture->planner.cycle_ids ==
             std::vector<PlanningCycleId>{PlanningCycleId{7}} &&
         fixture->planner.base_versions ==
             std::vector<SnapshotVersion>{SnapshotVersion{0}} &&
         fixture->planner.kernel_times ==
             std::vector<SimTime>{SimTime::Zero()} &&
         fixture->world.current_snapshot().version() == SnapshotVersion{1} &&
         fixture->world.current_snapshot().committed_at() == Seconds(10) &&
         fixture->world.last_committed_cycle_id() == PlanningCycleId{7} &&
         now && *now == SimTime::Zero();
}

auto TestMixedArrivalScenarioRuntime() -> bool {
  RecordingTraceSink trace;
  auto fixture = RuntimeFixture::Create(PlanningCycleId{20},
                                        {NodeId{0}},
                                        FeasibilityMode::kStableChain,
                                        std::nullopt,
                                        &trace);
  const auto packet = TestPacket();
  if(!fixture || !fixture->Enqueue(packet)) return false;
  fixture->channel.no_arrival_receivers = {NodeId{2}};
  const auto run = fixture->runtime.RunCycles(1);
  const auto next_transmission = fixture->ids.NextTransmissionId();
  const auto next_reception = fixture->ids.NextReceptionId();
  std::vector<NodeId> signal_receivers;
  std::vector<NodeId> no_arrival_receivers;
  for(const auto& event : trace.events) {
    const auto* channel =
        std::get_if<ChannelOutcomeTrace>(&event.payload());
    if(channel == nullptr) continue;
    if(std::holds_alternative<TraceSignalChannelOutcome>(channel->outcome)) {
      signal_receivers.push_back(channel->receiver_node_id);
    } else {
      no_arrival_receivers.push_back(channel->receiver_node_id);
    }
  }
  return run && fixture->runtime.state() == ScenarioRuntimeState::kCompleted &&
         fixture->tx_phy.audit.size() == 1U &&
         fixture->channel.receiver_audit ==
             std::vector<NodeId>{NodeId{1}, NodeId{2}, NodeId{3}} &&
         fixture->noise.count == 2U && fixture->rx_phy.count == 2U &&
         fixture->noise.receiver_audit ==
             std::vector<NodeId>{NodeId{1}, NodeId{3}} &&
         fixture->rx_phy.receiver_audit ==
             std::vector<NodeId>{NodeId{1}, NodeId{3}} &&
         fixture->QueueHasOnly(NodeId{1}, packet) &&
         fixture->deliveries.size() == 0U &&
         fixture->world.current_snapshot().version() == SnapshotVersion{1} &&
         fixture->world.last_committed_cycle_id() == PlanningCycleId{20} &&
         next_transmission && *next_transmission == TransmissionId{101} &&
         next_reception && *next_reception == ReceptionId{1'002} &&
         CountKind(trace, TraceKind::kTransmission) == 1 &&
         CountKind(trace, TraceKind::kChannelOutcome) == 3 &&
         CountKind(trace, TraceKind::kReception) == 2 &&
         CountKind(trace, TraceKind::kCycleCommit) == 1 &&
         trace.events.size() == 7 && HasCanonicalTraceOrder(trace) &&
         ReceptionDispositions(trace) ==
             std::vector<TraceReceptionDisposition>{
                 TraceReceptionDisposition::kRelayEnqueue,
                 TraceReceptionDisposition::kOverheard} &&
         signal_receivers ==
             std::vector<NodeId>{NodeId{1}, NodeId{3}} &&
         no_arrival_receivers == std::vector<NodeId>{NodeId{2}};
}

auto TestAllNoArrivalScenarioRuntime() -> bool {
  RecordingTraceSink trace;
  auto fixture = RuntimeFixture::Create(PlanningCycleId{30},
                                        {NodeId{0}},
                                        FeasibilityMode::kStableChain,
                                        std::nullopt,
                                        &trace);
  const auto packet = TestPacket();
  if(!fixture || !fixture->Enqueue(packet)) return false;
  fixture->channel.no_arrival_receivers =
      {NodeId{1}, NodeId{2}, NodeId{3}};
  const auto run = fixture->runtime.RunCycles(1);
  const auto next_transmission = fixture->ids.NextTransmissionId();
  const auto next_reception = fixture->ids.NextReceptionId();
  const auto all_outcomes_are_no_arrival = std::all_of(
      trace.events.begin(), trace.events.end(), [](const TraceEvent& event) {
        const auto* channel =
            std::get_if<ChannelOutcomeTrace>(&event.payload());
        return channel == nullptr ||
               std::holds_alternative<TraceNoArrivalChannelOutcome>(
                   channel->outcome);
      });
  return run && fixture->runtime.state() == ScenarioRuntimeState::kCompleted &&
         fixture->tx_phy.audit.size() == 1U &&
         fixture->channel.receiver_audit ==
             std::vector<NodeId>{NodeId{1}, NodeId{2}, NodeId{3}} &&
         fixture->noise.count == 0U && fixture->rx_phy.count == 0U &&
         fixture->noise.receiver_audit.empty() &&
         fixture->rx_phy.receiver_audit.empty() &&
         fixture->QueueHasOnly(std::nullopt, packet) &&
         fixture->deliveries.size() == 0U &&
         fixture->world.current_snapshot().version() == SnapshotVersion{1} &&
         fixture->world.last_committed_cycle_id() == PlanningCycleId{30} &&
         next_transmission && *next_transmission == TransmissionId{101} &&
         next_reception && *next_reception == ReceptionId{1'000} &&
         CountKind(trace, TraceKind::kTransmission) == 1 &&
         CountKind(trace, TraceKind::kChannelOutcome) == 3 &&
         CountKind(trace, TraceKind::kReception) == 0 &&
         CountKind(trace, TraceKind::kCycleCommit) == 1 &&
         trace.events.size() == 5 && HasCanonicalTraceOrder(trace) &&
         all_outcomes_are_no_arrival &&
         std::get<CycleCommitTrace>(trace.events.back().payload()) ==
             CycleCommitTrace{PlanningCycleId{30},
                              SnapshotVersion{0},
                              SnapshotVersion{1},
                              Seconds(10)};
}

auto TestProviderFailureEmitsNoSuccessfulTrace() -> bool {
  RecordingTraceSink trace;
  auto fixture = RuntimeFixture::Create(PlanningCycleId{40},
                                        {NodeId{0}},
                                        FeasibilityMode::kStableChain,
                                        std::nullopt,
                                        &trace);
  const auto packet = TestPacket();
  if(!fixture || !fixture->Enqueue(packet)) return false;
  fixture->channel.fail_on_receiver = NodeId{2};
  const auto run = fixture->runtime.RunCycles(1);
  const auto next_transmission = fixture->ids.NextTransmissionId();
  return !run && run.error().code == ErrorCode::kUnavailable &&
         fixture->runtime.state() == ScenarioRuntimeState::kFailed &&
         fixture->channel.receiver_audit ==
             std::vector<NodeId>{NodeId{1}, NodeId{2}} &&
         fixture->QueueHasOnly(NodeId{0}, packet) &&
         fixture->world.current_snapshot().version() == SnapshotVersion{0} &&
         next_transmission && *next_transmission == TransmissionId{101} &&
         trace.events.empty();
}

auto TestAllReceptionDispositions() -> bool {
  RecordingTraceSink trace;
  auto fixture = RuntimeFixture::Create(PlanningCycleId{50},
                                        {NodeId{0}},
                                        FeasibilityMode::kStableChain,
                                        std::nullopt,
                                        &trace);
  const auto packet = TestPacket();
  if(!fixture || !fixture->Enqueue(packet)) return false;
  fixture->rx_phy.not_decoded_receivers = {NodeId{1}};
  const auto run = fixture->runtime.RunCycles(1);
  auto dispositions = ReceptionDispositions(trace);
  std::sort(dispositions.begin(), dispositions.end());
  return run && dispositions ==
                    std::vector<TraceReceptionDisposition>{
                        TraceReceptionDisposition::kNotDecoded,
                        TraceReceptionDisposition::kOverheard,
                        TraceReceptionDisposition::kOverheard} &&
         fixture->QueueHasOnly(std::nullopt, packet) &&
         fixture->deliveries.size() == 0;
}

auto TestBroadcastCardinality() -> bool {
  RecordingTraceSink trace;
  auto fixture = RuntimeFixture::Create(PlanningCycleId{60},
                                        {NodeId{0}},
                                        FeasibilityMode::kStableChain,
                                        std::nullopt,
                                        &trace);
  const DigitalPacket packet{PacketId{20},
                             NodeId{0},
                             BroadcastDestination{},
                             {std::byte{0x01}}};
  if(!fixture || !fixture->Enqueue(packet)) return false;
  const auto run = fixture->runtime.RunCycles(1);
  if(!run || CountKind(trace, TraceKind::kTransmission) != 1 ||
     CountKind(trace, TraceKind::kChannelOutcome) != 3 ||
     CountKind(trace, TraceKind::kReception) != 3 ||
     fixture->deliveries.size() != 3) {
    return false;
  }
  const auto transmission = std::find_if(
      trace.events.begin(), trace.events.end(), [](const TraceEvent& event) {
        return event.kind() == TraceKind::kTransmission;
      });
  if(transmission == trace.events.end()) return false;
  const auto transmission_id =
      std::get<TransmissionTrace>(transmission->payload()).transmission_id;
  return std::holds_alternative<TraceBroadcastTransmissionTarget>(
             std::get<TransmissionTrace>(transmission->payload()).target) &&
         std::all_of(
             trace.events.begin(),
             trace.events.end(),
             [transmission_id](const TraceEvent& event) {
               if(const auto* channel =
                      std::get_if<ChannelOutcomeTrace>(&event.payload())) {
                 return channel->transmission_id == transmission_id;
               }
               if(const auto* reception =
                      std::get_if<ReceptionTrace>(&event.payload())) {
                 return reception->transmission_id == transmission_id;
               }
               return true;
             });
}

auto TestThreeCycleDeterminismAndCounts() -> bool {
  RecordingTraceSink first_trace;
  RecordingTraceSink second_trace;
  auto first = RuntimeFixture::Create(PlanningCycleId{70},
                                      {NodeId{0}, NodeId{1}, NodeId{2}},
                                      FeasibilityMode::kStableChain,
                                      std::nullopt,
                                      &first_trace);
  auto second = RuntimeFixture::Create(PlanningCycleId{70},
                                       {NodeId{0}, NodeId{1}, NodeId{2}},
                                       FeasibilityMode::kStableChain,
                                       std::nullopt,
                                       &second_trace);
  const auto packet = TestPacket();
  if(!first || !second || !first->Enqueue(packet) ||
     !second->Enqueue(packet)) {
    return false;
  }
  const auto first_run = first->runtime.RunCycles(3);
  const auto second_run = second->runtime.RunCycles(3);
  if(!first_run || !second_run || first_trace.events != second_trace.events ||
     !HasCanonicalTraceOrder(first_trace) ||
     CountKind(first_trace, TraceKind::kCycleCommit) != 3 ||
     CountKind(first_trace, TraceKind::kTransmission) != 3 ||
     CountKind(first_trace, TraceKind::kChannelOutcome) != 9 ||
     CountKind(first_trace, TraceKind::kReception) != 9 ||
     first->world.current_snapshot().version() != SnapshotVersion{3} ||
     first->world.current_snapshot().committed_at() != Seconds(30) ||
     first->deliveries.size() != 1 ||
     !first->QueueHasOnly(std::nullopt, packet)) {
    return false;
  }

  std::vector<NodeId> targets;
  for(const auto& event : first_trace.events) {
    const auto* transmission =
        std::get_if<TransmissionTrace>(&event.payload());
    if(transmission == nullptr) continue;
    const auto* target = std::get_if<TraceUnicastTransmissionTarget>(
        &transmission->target);
    if(target == nullptr || transmission->packet_id != packet.packet_id) {
      return false;
    }
    targets.push_back(target->node_id);
  }
  const auto dispositions = ReceptionDispositions(first_trace);
  return targets == std::vector<NodeId>{NodeId{1}, NodeId{2}, NodeId{3}} &&
         std::count(dispositions.begin(),
                    dispositions.end(),
                    TraceReceptionDisposition::kRelayEnqueue) == 2 &&
         std::count(dispositions.begin(),
                    dispositions.end(),
                    TraceReceptionDisposition::kLocalDelivery) == 1 &&
         std::count(dispositions.begin(),
                    dispositions.end(),
                    TraceReceptionDisposition::kOverheard) == 6;
}

auto TestFailingSinkIsNonCausal() -> bool {
  NullTraceSink null_sink;
  AlwaysFailTraceSink failing_sink;
  auto baseline = RuntimeFixture::Create(PlanningCycleId{80},
                                         {NodeId{0}},
                                         FeasibilityMode::kStableChain,
                                         std::nullopt,
                                         &null_sink);
  auto failing = RuntimeFixture::Create(PlanningCycleId{80},
                                        {NodeId{0}},
                                        FeasibilityMode::kStableChain,
                                        std::nullopt,
                                        &failing_sink);
  const auto packet = TestPacket();
  if(!baseline || !failing || !baseline->Enqueue(packet) ||
     !failing->Enqueue(packet)) {
    return false;
  }
  baseline->channel.no_arrival_receivers = {NodeId{2}};
  failing->channel.no_arrival_receivers = {NodeId{2}};
  const auto baseline_run = baseline->runtime.RunCycles(1);
  const auto failing_run = failing->runtime.RunCycles(1);
  const auto baseline_tx = baseline->ids.NextTransmissionId();
  const auto failing_tx = failing->ids.NextTransmissionId();
  const auto baseline_rx = baseline->ids.NextReceptionId();
  const auto failing_rx = failing->ids.NextReceptionId();
  const auto baseline_now = baseline->gateway.PlatformNow();
  const auto failing_now = failing->gateway.PlatformNow();
  const auto& baseline_snapshot = baseline->world.current_snapshot();
  const auto& failing_snapshot = failing->world.current_snapshot();
  return baseline_run && failing_run && failing_sink.attempt_count == 7 &&
         failing_sink.quality_reception_count == 2 &&
         baseline_snapshot.version() == failing_snapshot.version() &&
         baseline_snapshot.committed_at() ==
             failing_snapshot.committed_at() &&
         std::equal(baseline_snapshot.nodes().begin(),
                    baseline_snapshot.nodes().end(),
                    failing_snapshot.nodes().begin(),
                    failing_snapshot.nodes().end()) &&
         baseline->world.last_committed_cycle_id() ==
             failing->world.last_committed_cycle_id() &&
         baseline->QueueHasOnly(NodeId{1}, packet) &&
         failing->QueueHasOnly(NodeId{1}, packet) &&
         baseline->deliveries.size() == failing->deliveries.size() &&
         baseline_tx == failing_tx && baseline_rx == failing_rx &&
         baseline_now == failing_now &&
         baseline->planner.cycle_ids == failing->planner.cycle_ids &&
         baseline->runtime.state() == failing->runtime.state();
}

auto TestKernelAheadOfSnapshotFailsBeforePlanning() -> bool {
  auto fixture = RuntimeFixture::Create(PlanningCycleId{0}, {NodeId{0}});
  if(!fixture) return false;
  const auto scheduled = fixture->gateway.ScheduleAt(Seconds(1), [] {});
  if(!scheduled) return false;
  fixture->gateway.Run();
  const auto before = fixture->gateway.PlatformNow();
  const auto run = fixture->runtime.RunCycles(1);
  const auto after = fixture->gateway.PlatformNow();
  return before && *before == Seconds(1) && !run &&
         run.error().code == ErrorCode::kFailedPrecondition &&
         fixture->runtime.state() == ScenarioRuntimeState::kFailed &&
         fixture->planner.build_count == 0 &&
         fixture->estimator.observed_at.empty() && after &&
         *after == SimTime::Zero();
}

auto TestZeroDelayRemainsFatalWithoutTimeShift() -> bool {
  auto fixture = RuntimeFixture::Create(PlanningCycleId{0}, {NodeId{0}});
  const auto packet = TestPacket();
  if(!fixture || !fixture->Enqueue(packet)) return false;
  fixture->channel.propagation_delay = SimDuration::Zero();
  const auto run = fixture->runtime.RunCycles(1);
  const auto now = fixture->gateway.PlatformNow();
  return !run &&
         run.error().code == ErrorCode::kFailedPrecondition &&
         fixture->runtime.state() == ScenarioRuntimeState::kFailed &&
         fixture->planner.build_count == 1 &&
         fixture->tx_phy.audit.size() == 1 &&
         fixture->world.current_snapshot().version() == SnapshotVersion{0} &&
         fixture->world.current_snapshot().committed_at() == Seconds(0) &&
         fixture->QueueHasOnly(NodeId{0}, packet) &&
         fixture->deliveries.size() == 0 && now &&
         *now == SimTime::Zero();
}

auto TestFailureStopsAndPreservesSuccessfulPrefix() -> bool {
  auto fixture = RuntimeFixture::Create(
      PlanningCycleId{0},
      {NodeId{0}, NodeId{1}, NodeId{2}},
      FeasibilityMode::kHysteresisFailureCandidate,
      2);
  const auto packet = TestPacket();
  if(!fixture || !fixture->Enqueue(packet)) return false;
  const auto run = fixture->runtime.RunCycles(3);
  if(run || run.error().code != ErrorCode::kInternal ||
     fixture->runtime.state() != ScenarioRuntimeState::kFailed ||
     fixture->planner.build_count != 2 ||
     fixture->planner.connectivity_graphs.size() != 2 ||
     fixture->planner.kernel_times !=
         std::vector<SimTime>{Seconds(0), Seconds(10)} ||
     fixture->estimator.observed_at.size() != 24 ||
     fixture->tx_phy.audit.size() != 1 ||
     fixture->world.current_snapshot().version() != SnapshotVersion{1} ||
     fixture->world.current_snapshot().committed_at() != Seconds(10) ||
     fixture->world.last_committed_cycle_id() != PlanningCycleId{0} ||
     !fixture->QueueHasOnly(NodeId{1}, packet) ||
     fixture->deliveries.size() != 0) {
    return false;
  }

  const auto& cycle_zero = fixture->planner.connectivity_graphs[0];
  const auto& failed_candidate = fixture->planner.connectivity_graphs[1];
  const auto& retained = fixture->runtime.previous_connectivity();
  if(!cycle_zero.HasEdge(NodeId{0}, NodeId{1}) ||
     cycle_zero.HasEdge(NodeId{0}, NodeId{2}) ||
     !failed_candidate.HasEdge(NodeId{0}, NodeId{1}) ||
     !failed_candidate.HasEdge(NodeId{0}, NodeId{2}) || !retained ||
     *retained != cycle_zero || retained->HasEdge(NodeId{0}, NodeId{2})) {
    return false;
  }

  const auto repeat = fixture->runtime.RunCycles(1);
  const auto now = fixture->gateway.PlatformNow();
  return !repeat &&
         repeat.error().code == ErrorCode::kFailedPrecondition &&
         fixture->planner.build_count == 2 &&
         fixture->estimator.observed_at.size() == 24 &&
         fixture->tx_phy.audit.size() == 1 && now &&
         *now == SimTime::Zero();
}

}  // namespace

auto main() -> int {
  return TestArgumentPreflightAndTerminalState() &&
                 TestSuccessfulLifecycle() &&
                 TestMixedArrivalScenarioRuntime() &&
                 TestAllNoArrivalScenarioRuntime() &&
                 TestProviderFailureEmitsNoSuccessfulTrace() &&
                 TestAllReceptionDispositions() &&
                 TestBroadcastCardinality() &&
                 TestThreeCycleDeterminismAndCounts() &&
                 TestFailingSinkIsNonCausal() &&
                 TestKernelAheadOfSnapshotFailsBeforePlanning() &&
                 TestZeroDelayRemainsFatalWithoutTimeShift() &&
                 TestFailureStopsAndPreservesSuccessfulPrefix()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
