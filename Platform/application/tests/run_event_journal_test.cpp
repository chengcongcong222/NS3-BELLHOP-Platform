#include <concepts>
#include <cstdlib>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <ns3_factory/application/run_service.hpp>

namespace {

using namespace ns3_factory::application;
using namespace ns3_factory::contracts;

template <typename Journal>
concept PublicJournalAppend = requires(Journal journal,
                                       const RunId& run_id,
                                       const TraceEvent& event) {
  journal.Append(run_id, event);
};

static_assert(!PublicJournalAppend<IRunEventJournal>);
static_assert(!std::constructible_from<RunEventSink,
                                       const RunId&,
                                       IRunEventJournal&>);

auto Node(std::uint64_t id, bool stationary = false)
    -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{!stationary,
                            true,
                            DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, -10.0},
                  Velocity3d{stationary ? 0.0 : 1.0, 0.0, 0.0}}};
}

auto MakeScenario() -> Result<ScenarioDefinition> {
  auto id = ScenarioId::Create("event-scenario");
  if(!id) return std::unexpected(id.error());
  return ScenarioDefinition::Create(
      std::move(*id),
      1,
      "Event Scenario",
      {Node(1), Node(9, true)},
      EnvironmentReference{"event-asset", 1},
      MobilityModel::kConstantVelocity,
      NodeId{9});
}

auto MakeExperiment(const ScenarioDefinition& scenario)
    -> Result<ExperimentDefinition> {
  auto id = ExperimentId::Create("event-experiment");
  if(!id) return std::unexpected(id.error());
  return ExperimentDefinition::Create(
      std::move(*id),
      1,
      "Event Experiment",
      ScenarioReference{scenario.scenario_id(), scenario.version()},
      RoutingConfiguration{RoutingMode::kDirectToFusionCenter},
      MacConfiguration{MacMode::kTdma,
                       SimDuration::FromNanoseconds(4'000'000'000),
                       SimDuration::FromNanoseconds(2'000'000'000)},
      PhyConfiguration{60, 25'000.0, 4'000.0, 110.0, 45.0,
                       RxQualityMode::kNone},
      FusionConfiguration{ApplicationWorkload::kAcceptanceBearingFusion,
                          AcceptanceProfile::kAcceptance4Node,
                          5,
                          SimDuration::FromNanoseconds(180'000'000'000),
                          200.0,
                          150.0,
                          1.0e-4},
      10,
      1,
      7);
}

auto MakeTraceEvents() -> Result<std::vector<TraceEvent>> {
  std::vector<TraceEvent> events;
  auto commit = TraceEvent::Create(
      SimTime::FromNanoseconds(20),
      CycleCommitTrace{PlanningCycleId{0},
                       SnapshotVersion{0},
                       SnapshotVersion{1},
                       SimTime::FromNanoseconds(20)});
  auto transmission = TraceEvent::Create(
      SimTime::Zero(),
      TransmissionTrace{TransmissionId{30},
                        PacketId{40},
                        NodeId{1},
                        TraceUnicastTransmissionTarget{NodeId{9}},
                        SimTime::Zero(),
                        SimTime::FromNanoseconds(10)});
  auto channel = TraceEvent::Create(
      SimTime::Zero(),
      ChannelOutcomeTrace{TransmissionId{30},
                          NodeId{9},
                          TraceNoArrivalChannelOutcome{}});
  auto reception = TraceEvent::Create(
      SimTime::FromNanoseconds(10),
      ReceptionTrace{ReceptionId{50},
                     TransmissionId{30},
                     PacketId{40},
                     NodeId{9},
                     TraceReceptionDisposition::kLocalDelivery,
                     std::nullopt});
  auto second_channel = TraceEvent::Create(
      SimTime::Zero(),
      ChannelOutcomeTrace{TransmissionId{10},
                          NodeId{9},
                          TraceNoArrivalChannelOutcome{}});
  if(!commit || !transmission || !channel || !reception ||
     !second_channel) {
    return std::unexpected(
        Error{ErrorCode::kInternal, "Trace test fixture is invalid"});
  }
  events.push_back(*commit);
  events.push_back(*transmission);
  events.push_back(*channel);
  events.push_back(*reception);
  events.push_back(*second_channel);
  return events;
}

class TraceRunExecutor final : public IRunExecutor {
 public:
  explicit TraceRunExecutor(std::vector<TraceEvent> events)
      : events_(std::move(events)) {}

  auto Execute(const RunId& run_id,
               const ScenarioDefinition& scenario,
               const ExperimentDefinition&,
               ITraceSink& trace_sink) const
      -> Result<RunResult> override {
    for(const auto& event : events_) {
      const auto ignored = trace_sink.Emit(event);
      (void)ignored;
    }
    if(failure_) return std::unexpected(*failure_);
    std::vector<NodeSummary> nodes;
    for(const auto& node : scenario.nodes()) {
      nodes.push_back(NodeSummary{node.node_id,
                                  node.motion.position,
                                  node.node_id ==
                                      scenario.fusion_center_node_id()});
    }
    return RunResult{
        run_id,
        RunProjectionSummary{SimTime::Zero(),
                             SimTime::FromNanoseconds(20),
                             SimDuration::FromNanoseconds(20),
                             SnapshotVersion{1},
                             1,
                             scenario.nodes().size(),
                             1,
                             0,
                             2,
                             1,
                             1},
        std::nullopt,
        {},
        std::move(nodes)};
  }

  void FailWith(Error error) { failure_ = std::move(error); }

 private:
  std::vector<TraceEvent> events_;
  std::optional<Error> failure_;
};

class AlwaysFailJournal final : public IRunEventJournal {
 public:
  auto ReadAfter(const RunId&,
                 RunEventSequence cursor,
                 std::size_t limit) const
      -> Result<std::vector<RunEventRecord>> override {
    if(limit == 0U || limit > kMaximumRunEventReadLimit) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument, "Invalid limit"});
    }
    if(cursor != RunEventSequence::BeforeFirst()) {
      return std::unexpected(
          Error{ErrorCode::kOutOfRange, "Invalid cursor"});
    }
    return std::vector<RunEventRecord>{};
  }

  auto GetLatestSequence(const RunId&) const
      -> Result<RunEventSequence> override {
    return RunEventSequence::BeforeFirst();
  }

 private:
  auto Append(const RunId&, const TraceEvent&) noexcept
      -> Result<RunEventRecord> override {
    return std::unexpected(
        Error{ErrorCode::kUnavailable, "Injected journal failure"});
  }
};

class IntermittentJournal final : public IRunEventJournal {
 public:
  auto ReadAfter(const RunId& run_id,
                 RunEventSequence cursor,
                 std::size_t limit) const
      -> Result<std::vector<RunEventRecord>> override {
    if(limit == 0U || limit > kMaximumRunEventReadLimit) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument, "Invalid limit"});
    }
    const auto latest = records_.empty()
                            ? RunEventSequence::BeforeFirst()
                            : records_.back().sequence;
    if(cursor > latest) {
      return std::unexpected(
          Error{ErrorCode::kOutOfRange, "Invalid cursor"});
    }
    std::vector<RunEventRecord> page;
    for(const auto& record : records_) {
      if(record.run_id == run_id && record.sequence > cursor &&
         page.size() < limit) {
        page.push_back(record);
      }
    }
    return page;
  }

  auto GetLatestSequence(const RunId&) const
      -> Result<RunEventSequence> override {
    return records_.empty() ? RunEventSequence::BeforeFirst()
                            : records_.back().sequence;
  }

 private:
  auto Append(const RunId& run_id, const TraceEvent& event) noexcept
      -> Result<RunEventRecord> override {
    ++append_attempts_;
    if(append_attempts_ == 2U) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable,
                "Injected intermittent journal failure"});
    }
    const auto latest = records_.empty()
                            ? RunEventSequence::BeforeFirst()
                            : records_.back().sequence;
    const auto next = RunEventSequence::TryNextAfter(latest);
    if(!next) return std::unexpected(next.error());
    try {
      records_.push_back(RunEventRecord{run_id, *next, event});
      return records_.back();
    } catch(...) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable, "Journal allocation failed"});
    }
  }

  std::size_t append_attempts_{0};
  std::vector<RunEventRecord> records_;
};

class ExhaustedJournal final : public IRunEventJournal {
 public:
  auto ReadAfter(const RunId&,
                 RunEventSequence,
                 std::size_t limit) const
      -> Result<std::vector<RunEventRecord>> override {
    if(limit == 0U || limit > kMaximumRunEventReadLimit) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument, "Invalid limit"});
    }
    return std::vector<RunEventRecord>{};
  }

  auto GetLatestSequence(const RunId&) const
      -> Result<RunEventSequence> override {
    return RunEventSequence{
        std::numeric_limits<RunEventSequence::value_type>::max()};
  }

 private:
  auto Append(const RunId&, const TraceEvent&) noexcept
      -> Result<RunEventRecord> override {
    const auto next = RunEventSequence::TryNextAfter(
        RunEventSequence{
            std::numeric_limits<RunEventSequence::value_type>::max()});
    return std::unexpected(next.error());
  }
};

struct ServiceFixture final {
  ScenarioRepository scenarios;
  ExperimentRepository experiments;
  RunRepository runs;
  ScenarioDefinition scenario;
  ExperimentDefinition experiment;

  static auto Create() -> Result<ServiceFixture> {
    auto scenario = MakeScenario();
    if(!scenario) return std::unexpected(scenario.error());
    auto experiment = MakeExperiment(*scenario);
    if(!experiment) return std::unexpected(experiment.error());
    ServiceFixture fixture{{}, {}, {}, *scenario, *experiment};
    if(const auto registered = fixture.scenarios.Register(*scenario);
       !registered) {
      return std::unexpected(registered.error());
    }
    if(const auto registered = fixture.experiments.Register(*experiment);
       !registered) {
      return std::unexpected(registered.error());
    }
    return fixture;
  }

  [[nodiscard]] auto reference() const -> ExperimentReference {
    return ExperimentReference{experiment.experiment_id(),
                               experiment.version()};
  }
};

auto PayloadsEqual(const std::vector<RunEventRecord>& records,
                   const std::vector<TraceEvent>& expected) -> bool {
  if(records.size() != expected.size()) return false;
  for(std::size_t index = 0; index < records.size(); ++index) {
    if(records[index].trace_event != expected[index]) return false;
  }
  return true;
}

auto TestPaginationReplayAndIndependentRuns() -> bool {
  auto events = MakeTraceEvents();
  auto fixture = ServiceFixture::Create();
  auto first_id = RunId::Create("event-run-a");
  auto second_id = RunId::Create("event-run-b");
  if(!events || !fixture || !first_id || !second_id) return false;
  TraceRunExecutor executor{*events};
  InMemoryRunEventJournal journal;
  RunService service{fixture->scenarios,
                     fixture->experiments,
                     fixture->runs,
                     executor,
                     journal};
  if(!service.CreateRun(*first_id, fixture->reference()) ||
     !service.CreateRun(*second_id, fixture->reference()) ||
     !service.ExecuteRun(*first_id) || !service.ExecuteRun(*second_id)) {
    return false;
  }
  const auto first_record = service.GetRun(*first_id);
  const auto latest_first = service.GetLatestEventSequence(*first_id);
  const auto latest_second = service.GetLatestEventSequence(*second_id);
  const auto first_page = service.ReadEvents(
      *first_id, RunEventSequence::BeforeFirst(), 2);
  const auto repeated_first_page = service.ReadEvents(
      *first_id, RunEventSequence::BeforeFirst(), 2);
  const auto second_page =
      service.ReadEvents(*first_id, RunEventSequence{2}, 2);
  const auto third_page =
      service.ReadEvents(*first_id, RunEventSequence{4}, 2);
  const auto all = service.ReadEvents(
      *first_id, RunEventSequence::BeforeFirst(),
      kMaximumRunEventReadLimit);
  const auto second_run = service.ReadEvents(
      *second_id, RunEventSequence::BeforeFirst(),
      kMaximumRunEventReadLimit);
  const auto at_latest =
      service.ReadEvents(*first_id, RunEventSequence{5}, 1);
  const auto future =
      service.ReadEvents(*first_id, RunEventSequence{6}, 1);
  const auto zero_limit = service.ReadEvents(
      *first_id, RunEventSequence::BeforeFirst(), 0);
  const auto excessive_limit = service.ReadEvents(
      *first_id, RunEventSequence::BeforeFirst(),
      kMaximumRunEventReadLimit + 1U);
  if(!first_record || !latest_first || !latest_second || !first_page ||
     !repeated_first_page || !second_page || !third_page || !all ||
     !second_run || !at_latest || future || zero_limit ||
     excessive_limit) {
    return false;
  }
  std::vector<RunEventRecord> replay;
  replay.insert(replay.end(), first_page->begin(), first_page->end());
  replay.insert(replay.end(), second_page->begin(), second_page->end());
  replay.insert(replay.end(), third_page->begin(), third_page->end());
  if(replay != *all || *first_page != *repeated_first_page ||
     !PayloadsEqual(*all, *events) || !PayloadsEqual(*second_run, *events) ||
     latest_first->value() != 5U || latest_second->value() != 5U ||
     !at_latest->empty() ||
     future.error().code != ErrorCode::kOutOfRange ||
     zero_limit.error().code != ErrorCode::kInvalidArgument ||
     excessive_limit.error().code != ErrorCode::kInvalidArgument ||
     first_record->lifecycle != RunLifecycle::kCompleted ||
     first_record->event_stream_complete != true) {
    return false;
  }
  for(std::size_t index = 0; index < all->size(); ++index) {
    if((*all)[index].run_id != *first_id ||
       (*all)[index].sequence.value() != index + 1U ||
       (*second_run)[index].run_id != *second_id ||
       (*second_run)[index].sequence.value() != index + 1U) {
      return false;
    }
  }
  return true;
}

auto TestFailedRunRetainsReplay() -> bool {
  auto events = MakeTraceEvents();
  auto fixture = ServiceFixture::Create();
  auto run_id = RunId::Create("event-run-failed");
  if(!events || !fixture || !run_id) return false;
  TraceRunExecutor executor{*events};
  executor.FailWith(Error{ErrorCode::kInternal, "Injected simulation failure"});
  InMemoryRunEventJournal journal;
  RunService service{fixture->scenarios,
                     fixture->experiments,
                     fixture->runs,
                     executor,
                     journal};
  if(!service.CreateRun(*run_id, fixture->reference())) return false;
  const auto executed = service.ExecuteRun(*run_id);
  const auto record = service.GetRun(*run_id);
  const auto replay = service.ReadEvents(
      *run_id, RunEventSequence::BeforeFirst(),
      kMaximumRunEventReadLimit);
  const auto result = service.GetResult(*run_id);
  const auto repeated = service.ExecuteRun(*run_id);
  return !executed && record && replay && !result && !repeated &&
         PayloadsEqual(*replay, *events) &&
         record->lifecycle == RunLifecycle::kFailed &&
         record->event_stream_complete == true && record->failure &&
         record->failure->code == ErrorCode::kInternal;
}

auto TestJournalFailureIsNonCausal() -> bool {
  auto events = MakeTraceEvents();
  auto baseline_fixture = ServiceFixture::Create();
  auto failing_fixture = ServiceFixture::Create();
  auto baseline_id = RunId::Create("event-run-baseline");
  auto failing_id = RunId::Create("event-run-journal-failure");
  if(!events || !baseline_fixture || !failing_fixture || !baseline_id ||
     !failing_id) {
    return false;
  }
  TraceRunExecutor executor{*events};
  InMemoryRunEventJournal baseline_journal;
  AlwaysFailJournal failing_journal;
  RunService baseline_service{baseline_fixture->scenarios,
                              baseline_fixture->experiments,
                              baseline_fixture->runs,
                              executor,
                              baseline_journal};
  RunService failing_service{failing_fixture->scenarios,
                             failing_fixture->experiments,
                             failing_fixture->runs,
                             executor,
                             failing_journal};
  if(!baseline_service.CreateRun(*baseline_id,
                                 baseline_fixture->reference()) ||
     !failing_service.CreateRun(*failing_id,
                                failing_fixture->reference())) {
    return false;
  }
  const auto baseline = baseline_service.ExecuteRun(*baseline_id);
  const auto failed_journal_result =
      failing_service.ExecuteRun(*failing_id);
  const auto record = failing_service.GetRun(*failing_id);
  const auto stored = failing_service.GetResult(*failing_id);
  const auto events_after_failure = failing_service.ReadEvents(
      *failing_id, RunEventSequence::BeforeFirst(), 1);
  return baseline && failed_journal_result && record && stored &&
         events_after_failure && events_after_failure->empty() &&
         baseline->projection == failed_journal_result->projection &&
         baseline->acceptance_report ==
             failed_journal_result->acceptance_report &&
         baseline->fusion_results == failed_journal_result->fusion_results &&
         baseline->nodes == failed_journal_result->nodes &&
         record->lifecycle == RunLifecycle::kCompleted &&
         record->event_stream_complete == false &&
         *stored == *failed_journal_result;
}

auto TestIntermittentFailureDoesNotConsumeSequence() -> bool {
  auto events = MakeTraceEvents();
  auto fixture = ServiceFixture::Create();
  auto run_id = RunId::Create("event-run-intermittent");
  if(!events || !fixture || !run_id) return false;
  events->erase(events->begin() + 3, events->end());
  TraceRunExecutor executor{*events};
  IntermittentJournal journal;
  RunService service{fixture->scenarios,
                     fixture->experiments,
                     fixture->runs,
                     executor,
                     journal};
  if(!service.CreateRun(*run_id, fixture->reference())) return false;
  const auto result = service.ExecuteRun(*run_id);
  const auto record = service.GetRun(*run_id);
  const auto replay = service.ReadEvents(
      *run_id, RunEventSequence::BeforeFirst(),
      kMaximumRunEventReadLimit);
  const auto latest = service.GetLatestEventSequence(*run_id);
  return result && record && replay && latest && replay->size() == 2U &&
         (*replay)[0].sequence == RunEventSequence{1} &&
         (*replay)[1].sequence == RunEventSequence{2} &&
         (*replay)[0].trace_event == (*events)[0] &&
         (*replay)[1].trace_event == (*events)[2] &&
         latest->value() == 2U &&
         record->lifecycle == RunLifecycle::kCompleted &&
         record->event_stream_complete == false;
}

auto TestSequenceOverflowIsNonCausal() -> bool {
  const auto checked = RunEventSequence::TryNextAfter(
      RunEventSequence{
          std::numeric_limits<RunEventSequence::value_type>::max()});
  auto events = MakeTraceEvents();
  auto fixture = ServiceFixture::Create();
  auto run_id = RunId::Create("event-run-overflow");
  if(checked || checked.error().code != ErrorCode::kOverflow || !events ||
     !fixture || !run_id) {
    return false;
  }
  events->erase(events->begin() + 1, events->end());
  TraceRunExecutor executor{*events};
  ExhaustedJournal journal;
  RunService service{fixture->scenarios,
                     fixture->experiments,
                     fixture->runs,
                     executor,
                     journal};
  if(!service.CreateRun(*run_id, fixture->reference())) return false;
  const auto result = service.ExecuteRun(*run_id);
  const auto record = service.GetRun(*run_id);
  const auto stored = service.GetResult(*run_id);
  const auto latest = service.GetLatestEventSequence(*run_id);
  return result && record && stored && latest &&
         latest->value() ==
             std::numeric_limits<RunEventSequence::value_type>::max() &&
         record->lifecycle == RunLifecycle::kCompleted &&
         record->event_stream_complete == false && *stored == *result;
}

}  // namespace

auto main() -> int {
  return TestPaginationReplayAndIndependentRuns() &&
                 TestFailedRunRetainsReplay() &&
                 TestJournalFailureIsNonCausal() &&
                 TestIntermittentFailureDoesNotConsumeSequence() &&
                 TestSequenceOverflowIsNonCausal()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
