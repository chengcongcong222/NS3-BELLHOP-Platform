#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/application/repositories.hpp>
#include <ns3_factory/application/run_service.hpp>

#include "internal/acceptance_preset.hpp"
#include "internal/acceptance_run_executor.hpp"
#include "internal/acoustic_field_asset.hpp"
#include "internal/environment_asset_package.hpp"
#include "internal/environment_asset_repository.hpp"

namespace {

using namespace ns3_factory;
using namespace application;
using namespace application::internal;
using namespace contracts;
using namespace environment::internal;

class TemporaryRepositoryRoot final {
 public:
  TemporaryRepositoryRoot()
      : path_(std::filesystem::temp_directory_path() /
              "ns3_factory_application_run_service_test") {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
    std::filesystem::create_directory(path_);
  }

  ~TemporaryRepositoryRoot() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] auto path() const noexcept
      -> const std::filesystem::path& {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

auto MakeAsset(bool path_aware = false) -> Result<AcousticFieldAsset> {
  auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!frame) return std::unexpected(frame.error());
  std::vector<AcousticFieldCell> cells;
  std::vector<PropagationPath> paths;
  if(path_aware) {
    const auto path = PropagationPath::Create(
        SimDuration::Zero(), std::pow(10.0, -70.0 / 20.0), 0.0);
    if(!path) return std::unexpected(path.error());
    paths.push_back(*path);
  }
  for(std::size_t index = 0; index < 8U; ++index) {
    cells.push_back(AcousticFieldSignalCell{
        70.0, SimDuration::FromNanoseconds(500'000'000), paths});
  }
  return AcousticFieldAsset::Create(1,
                                    "application acceptance fixture",
                                    *frame,
                                    {25'000.0},
                                    {0.0, 100.0},
                                    {0.0, 100.0},
                                    {0.0, 5'000.0},
                                    std::move(cells));
}

auto MakeProvenance() -> Result<EnvironmentAssetPackageProvenance> {
  return EnvironmentAssetPackageProvenance::Create(
      EnvironmentAssetProducerType::kManual,
      "application-test",
      "deterministic acceptance service fixture",
      "",
      "");
}

auto SameSimulationResult(const RunResult& lhs,
                          const RunResult& rhs) -> bool {
  return lhs.projection == rhs.projection &&
         lhs.acceptance_report == rhs.acceptance_report &&
         lhs.fusion_results == rhs.fusion_results && lhs.nodes == rhs.nodes;
}

auto SameTracePayloadOrder(const std::vector<RunEventRecord>& lhs,
                           const std::vector<RunEventRecord>& rhs) -> bool {
  if(lhs.size() != rhs.size()) return false;
  for(std::size_t index = 0; index < lhs.size(); ++index) {
    if(lhs[index].sequence != rhs[index].sequence ||
       lhs[index].trace_event != rhs[index].trace_event) {
      return false;
    }
  }
  return true;
}

auto CountKind(const std::vector<RunEventRecord>& events,
               TraceKind kind) -> std::size_t {
  return static_cast<std::size_t>(std::count_if(
      events.begin(), events.end(),
      [kind](const RunEventRecord& event) {
        return event.trace_event.kind() == kind;
      }));
}

auto HasCausalArrivalEvidence(
    const std::vector<RunEventRecord>& events) -> bool {
  for(const auto& arrival_event : events) {
    const auto* arrival =
        std::get_if<SignalArrivalTrace>(&arrival_event.trace_event.payload());
    if(arrival == nullptr) continue;
    const auto channel = std::find_if(
        events.begin(), events.end(), [&](const RunEventRecord& candidate) {
          const auto* outcome = std::get_if<ChannelOutcomeTrace>(
              &candidate.trace_event.payload());
          if(outcome == nullptr ||
             outcome->transmission_id != arrival->transmission_id ||
             outcome->receiver_node_id != arrival->receiver_node_id) {
            return false;
          }
          const auto* signal =
              std::get_if<TraceSignalChannelOutcome>(&outcome->outcome);
          if(signal == nullptr) return false;
          const auto expected = CheckedAdd(candidate.trace_event.occurred_at(),
                                           signal->first_arrival_delay);
          return expected && *expected == arrival_event.trace_event.occurred_at();
        });
    if(channel == events.end()) return false;
  }
  return true;
}

auto HasMovingTrajectoryEvidence(
    const std::vector<RunEventRecord>& events) -> bool {
  std::optional<NodePositionTrace> first;
  for(const auto& event : events) {
    const auto* position =
        std::get_if<NodePositionTrace>(&event.trace_event.payload());
    if(position == nullptr || position->node_id != NodeId{10}) continue;
    if(!first) first = *position;
    else if(position->x_meters != first->x_meters ||
            position->y_meters != first->y_meters ||
            position->z_meters != first->z_meters) {
      return true;
    }
  }
  return false;
}

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

auto TestAcceptanceRunServiceAndMissingEvidence() -> bool {
  TemporaryRepositoryRoot temporary;
  auto environments = EnvironmentAssetRepository::Open(temporary.path());
  auto asset = MakeAsset();
  auto provenance = MakeProvenance();
  auto asset_id = EnvironmentAssetId::Create("acceptance4-app-field-v1");
  if(!environments || !asset || !provenance || !asset_id ||
     !environments->Register(*asset_id, *asset, *provenance)) {
    return false;
  }

  auto scenario_id = ScenarioId::Create("acceptance4");
  auto experiment_id = ExperimentId::Create("acceptance-modeled");
  auto missing_experiment_id = ExperimentId::Create("acceptance-no-quality");
  if(!scenario_id || !experiment_id || !missing_experiment_id) return false;
  auto scenario = MakeAcceptanceScenarioPreset(
      AcceptanceProfile::kAcceptance4Node,
      *scenario_id,
      1,
      "Acceptance 4 Node",
      EnvironmentReference{asset_id->value(), 1});
  if(!scenario) return false;
  const auto scenario_reference =
      ScenarioReference{scenario->scenario_id(), scenario->version()};
  auto experiment = MakeAcceptanceExperimentPreset(
      AcceptanceProfile::kAcceptance4Node,
      *experiment_id,
      1,
      "Acceptance modeled BER",
      scenario_reference,
      2);
  auto missing_experiment = MakeAcceptanceExperimentPreset(
      AcceptanceProfile::kAcceptance4Node,
      *missing_experiment_id,
      1,
      "Acceptance no quality",
      scenario_reference,
      2,
      RxQualityMode::kNone);
  if(!experiment || !missing_experiment) return false;

  ScenarioRepository scenarios;
  ExperimentRepository experiments;
  RunRepository runs;
  if(!scenarios.Register(*scenario) ||
     !experiments.Register(*experiment) ||
     !experiments.Register(*missing_experiment)) {
    return false;
  }
  AcceptanceRunExecutor executor{*environments};
  InMemoryRunEventJournal events;
  RunService service{scenarios, experiments, runs, executor, events};
  auto run_id = RunId::Create("run-modeled");
  auto second_run_id = RunId::Create("run-modeled-repeat");
  auto direct_run_id = RunId::Create("run-direct-baseline");
  auto missing_run_id = RunId::Create("run-no-quality");
  auto failed_journal_run_id = RunId::Create("run-journal-failure");
  if(!run_id || !second_run_id || !direct_run_id || !missing_run_id ||
     !failed_journal_run_id) {
    return false;
  }
  const auto modeled_reference = ExperimentReference{
      experiment->experiment_id(), experiment->version()};
  if(!service.CreateRun(*run_id, modeled_reference) ||
     !service.CreateRun(*second_run_id, modeled_reference)) {
    return false;
  }
  const auto result = service.ExecuteRun(*run_id);
  const auto repeated = service.ExecuteRun(*second_run_id);
  NullTraceSink direct_trace;
  const auto direct =
      executor.Execute(*direct_run_id, *scenario, *experiment, direct_trace);
  const auto record = service.GetRun(*run_id);
  const auto stored = service.GetResult(*run_id);
  const auto first_events = service.ReadEvents(
      *run_id, RunEventSequence::BeforeFirst(),
      kMaximumRunEventReadLimit);
  const auto repeated_events = service.ReadEvents(
      *second_run_id, RunEventSequence::BeforeFirst(),
      kMaximumRunEventReadLimit);
  if(!result || !repeated || !direct || !record || !stored ||
     !first_events || !repeated_events || !result->acceptance_report) {
    return false;
  }
  const auto& acceptance = *result->acceptance_report;
  if(!result->visualization || !result->visualization->positions_complete ||
     result->visualization != repeated->visualization ||
     result->visualization != direct->visualization ||
     result->visualization->generated_feature_count != 6 ||
     result->visualization->fusion_sources.size() != 6 ||
     !result->visualization->waveform.empty()) return false;
  for(const auto& final_node : result->nodes) {
    bool found = false;
    for(const auto& sample : result->visualization->positions) {
      if(sample.node_id == final_node.node_id && sample.at == result->projection.simulation_ended_at) {
        if(sample.position != final_node.final_position) return false;
        found = true;
      }
    }
    if(!found) return false;
  }
  const auto causal_event_count =
      result->projection.transmission_count +
      result->projection.channel_signal_count +
      result->projection.channel_no_arrival_count +
      result->projection.reception_count + result->projection.cycle_count;
  const auto observability_event_count =
      CountKind(*first_events, TraceKind::kSignalArrival) +
      CountKind(*first_events, TraceKind::kNodePosition) +
      CountKind(*first_events, TraceKind::kFusion);
  if(record->lifecycle != RunLifecycle::kCompleted ||
     record->event_stream_complete != true ||
     record->experiment != modeled_reference ||
     record->scenario != scenario_reference ||
     record->environment != scenario->environment() ||
     result->projection.node_count != 4U ||
     result->projection.cycle_count != 2U ||
     result->projection.final_snapshot_version != SnapshotVersion{2} ||
     result->projection.simulation_ended_at !=
         SimTime::FromNanoseconds(24'000'000'000) ||
     result->fusion_results.size() != 1U ||
     result->fusion_results.front().observation_count != 6U ||
     acceptance.network_node_count != MetricStatus::kPass ||
     acceptance.communication_rate != MetricStatus::kPass ||
     acceptance.bit_error_rate != MetricStatus::kPass ||
     acceptance.feature_level_fusion != MetricStatus::kPass ||
     acceptance.bearing_point_count != MetricStatus::kPass ||
     acceptance.fusion_period != MetricStatus::kPass ||
     acceptance.overall != OverallStatus::kPass ||
     !acceptance.maximum_ber || *acceptance.maximum_ber >= 1.0e-4 ||
     first_events->size() != causal_event_count + observability_event_count ||
     CountKind(*first_events, TraceKind::kSignalArrival) !=
         result->projection.channel_signal_count ||
     CountKind(*first_events, TraceKind::kFusion) !=
         result->fusion_results.size() ||
     CountKind(*first_events, TraceKind::kNodePosition) <
         result->projection.node_count ||
     !HasCausalArrivalEvidence(*first_events) ||
     !HasMovingTrajectoryEvidence(*first_events) ||
     !SameTracePayloadOrder(*first_events, *repeated_events) ||
     !SameSimulationResult(*result, *repeated) ||
     !SameSimulationResult(*result, *direct) || *stored != *result) {
    return false;
  }

  RunRepository failed_journal_runs;
  AlwaysFailJournal failed_journal;
  RunService failed_journal_service{scenarios,
                                    experiments,
                                    failed_journal_runs,
                                    executor,
                                    failed_journal};
  if(!failed_journal_service.CreateRun(*failed_journal_run_id,
                                       modeled_reference)) {
    return false;
  }
  const auto failed_journal_result =
      failed_journal_service.ExecuteRun(*failed_journal_run_id);
  const auto failed_journal_record =
      failed_journal_service.GetRun(*failed_journal_run_id);
  const auto failed_journal_stored =
      failed_journal_service.GetResult(*failed_journal_run_id);
  if(!failed_journal_result || !failed_journal_record ||
     !failed_journal_stored ||
     !SameSimulationResult(*result, *failed_journal_result) ||
     failed_journal_record->lifecycle != RunLifecycle::kCompleted ||
     failed_journal_record->event_stream_complete != false ||
     *failed_journal_stored != *failed_journal_result) {
    return false;
  }

  const auto missing_reference = ExperimentReference{
      missing_experiment->experiment_id(), missing_experiment->version()};
  if(!service.CreateRun(*missing_run_id, missing_reference)) return false;
  const auto missing_result = service.ExecuteRun(*missing_run_id);
  return missing_result && missing_result->acceptance_report &&
         missing_result->acceptance_report->bit_error_rate ==
             MetricStatus::kNotEvaluated &&
         missing_result->acceptance_report->overall ==
             OverallStatus::kNotFullyEvaluated &&
         missing_result->acceptance_report->missing_ber_evidence_count == 6U;
}

auto TestMissingEnvironmentProducesFailedLifecycle() -> bool {
  TemporaryRepositoryRoot temporary;
  auto environments = EnvironmentAssetRepository::Open(temporary.path());
  auto scenario_id = ScenarioId::Create("missing-environment-scenario");
  auto experiment_id = ExperimentId::Create("missing-environment-experiment");
  auto run_id = RunId::Create("missing-environment-run");
  if(!environments || !scenario_id || !experiment_id || !run_id) {
    return false;
  }
  auto scenario = MakeAcceptanceScenarioPreset(
      AcceptanceProfile::kAcceptance4Node,
      *scenario_id,
      1,
      "Missing environment scenario",
      EnvironmentReference{"not-registered", 1});
  if(!scenario) return false;
  auto experiment = MakeAcceptanceExperimentPreset(
      AcceptanceProfile::kAcceptance4Node,
      *experiment_id,
      1,
      "Missing environment experiment",
      ScenarioReference{scenario->scenario_id(), scenario->version()},
      2);
  if(!experiment) return false;
  ScenarioRepository scenarios;
  ExperimentRepository experiments;
  RunRepository runs;
  if(!scenarios.Register(*scenario) || !experiments.Register(*experiment)) {
    return false;
  }
  AcceptanceRunExecutor executor{*environments};
  InMemoryRunEventJournal events;
  RunService service{scenarios, experiments, runs, executor, events};
  if(!service.CreateRun(
         *run_id,
         ExperimentReference{experiment->experiment_id(),
                             experiment->version()})) {
    return false;
  }
  const auto executed = service.ExecuteRun(*run_id);
  const auto record = service.GetRun(*run_id);
  return !executed && record &&
         executed.error().code == ErrorCode::kNotFound &&
         record->lifecycle == RunLifecycle::kFailed && record->failure &&
         record->event_stream_complete == true &&
         record->failure->code == ErrorCode::kNotFound &&
         record->failure->message.starts_with("Environment asset missing:");
}

auto TestCommunicationOnlyUsesActualPayloadAirtime() -> bool {
  TemporaryRepositoryRoot temporary;
  auto environments = EnvironmentAssetRepository::Open(temporary.path());
  // Explicit one-path test fixture supports both physical implementations.
  // Production Channel2 must not invent a path for scalar-only assets.
  auto asset = MakeAsset(true);
  auto provenance = MakeProvenance();
  auto asset_id = EnvironmentAssetId::Create("communication-airtime-field");
  auto scenario_id = ScenarioId::Create("communication-airtime-scenario");
  auto experiment_id = ExperimentId::Create("communication-airtime-experiment");
  auto run_id = RunId::Create("communication-airtime-run");
  if(!environments || !asset || !provenance || !asset_id || !scenario_id ||
     !experiment_id || !run_id ||
     !environments->Register(*asset_id, *asset, *provenance)) return false;
  const auto scenario = MakeAcceptanceScenarioPreset(
      AcceptanceProfile::kAcceptance4Node, *scenario_id, 1,
      "Communication airtime fixture", {asset_id->value(), 1});
  if(!scenario) return false;
  const auto preset = MakeAcceptanceExperimentPreset(
      AcceptanceProfile::kAcceptance4Node, *experiment_id, 1,
      "Communication airtime fixture",
      {scenario->scenario_id(), scenario->version()}, 1);
  if(!preset) return false;
  static_assert(assembly::internal::kCommunicationTrafficPayload.size() == 4U);
  const auto payload_bytes = assembly::internal::kCommunicationTrafficPayload.size();
  const auto guard = preset->mac().guard_interval;
  for(const bool waveform : {false, true}) {
    const auto airtime = phy::internal::ComputePayloadAirtime(
        payload_bytes + (waveform ? phy::internal::kWaveformCrcBytes : 0U),
        preset->phy().bit_rate_bits_per_second);
    if(!airtime) return false;
    const auto minimum_slot = CheckedAdd(*airtime, guard);
    if(!minimum_slot) return false;
    const auto make_experiment = [&](ApplicationWorkload workload,
                                     SimDuration slot) {
      auto fusion = preset->fusion();
      fusion.workload = workload;
      auto phy = preset->phy();
      // Mode B reports actual waveform evidence; the abstract BPSK/AWGN
      // observer is not a legal configuration for a waveform executor.
      if(waveform) phy.quality_mode = RxQualityMode::kNone;
      return ExperimentDefinition::Create(
          *experiment_id, 1, "Workload-specific airtime", preset->scenario(),
          preset->routing(), {MacMode::kTdma, slot, guard}, phy, fusion,
          preset->network_update_interval_cycles(), 1, preset->deterministic_seed());
    };
    std::optional<WaveformExecutionOptions> options;
    if(waveform) {
      options.emplace(WaveformExecutionOptions{
          {{phy::internal::ModulationScheme::kBpsk, 6000, 60,
            std::pow(10.0, preset->phy().source_level_db_re_1upa_at_1m / 20.0),
            25000, 30},
           4000, phy::internal::NoNoiseConfig{},
           {phy::internal::ChannelEqualizationMode::kDisabled}, true,
           phy::internal::WaveformFrameFormat::kPayloadWithCrc32, 1'000'000}});
    }
    RunExecutionExecutor executor{*environments, nullptr, options};
    NullTraceSink trace;
    const auto experiment = make_experiment(ApplicationWorkload::kCommunicationOnly,
                                            *minimum_slot);
    if(!experiment) return false;
    const auto result = executor.Execute(*run_id, *scenario, *experiment, trace);
    if(!result) {
      std::cerr << "COMM airtime fixture mode " << (waveform ? 'B' : 'A')
                << " failed: " << result.error().message << '\n';
      return false;
    }
    if(result->projection.transmission_count != 3U ||
       result->projection.local_delivery_count != 3U ||
       !result->fusion_results.empty() || !result->visualization) {
      std::cerr << "COMM airtime fixture mode " << (waveform ? 'B' : 'A')
                << " unexpected counts: transmissions="
                << result->projection.transmission_count << " deliveries="
                << result->projection.local_delivery_count << '\n';
      return false;
    }
    if(waveform && (result->visualization->waveform.empty() ||
       !std::ranges::all_of(result->visualization->waveform, [payload_bytes](const auto& row) {
         return row.payload_bit_count == payload_bytes * 8U &&
                row.bit_error_count == 0U && row.crc_ok;
       }))) {
      std::cerr << "COMM airtime fixture mode B has unexpected waveform evidence\n";
      return false;
    }
    // Guard is part of the configured slot. A one-nanosecond shortfall
    // must still fail; a feature packet must not inherit COMM's smaller size.
    const auto too_short = make_experiment(
        ApplicationWorkload::kCommunicationOnly,
        SimDuration::FromNanoseconds(minimum_slot->nanoseconds() - 1));
    const auto feature = make_experiment(ApplicationWorkload::kBearingFusion,
                                        *minimum_slot);
    if(!too_short || !feature) return false;
    for(const auto& invalid : {*too_short, *feature}) {
      const auto rejected = executor.Execute(*run_id, *scenario, invalid, trace);
      if(rejected || rejected.error().code != ErrorCode::kFailedPrecondition ||
         rejected.error().message !=
             "TDMA slot cannot contain maximum packet airtime and guard") {
        std::cerr << "COMM airtime rejection mismatch mode " << (waveform ? 'B' : 'A')
                  << ": " << (rejected ? "unexpected success" : rejected.error().message)
                  << '\n';
        return false;
      }
    }
  }
  return true;
}

auto TestMixedCellPropagationTail() -> bool {
  auto frame = EnvironmentCoordinateFrame::Create(0.0, VerticalAxisDirection::kPositiveUp);
  auto direct = PropagationPath::Create(SimDuration::Zero(), 1.0, 0.0);
  auto late = PropagationPath::Create(SimDuration::FromNanoseconds(10'000'000'000), 0.5, 0.0);
  if(!frame || !direct || !late) return false;
  auto asset = AcousticFieldAsset::Create(1, "mixed interpolation bound fixture", *frame,
      {25000}, {40}, {40}, {0, 1000}, {
        AcousticFieldSignalCell{0, SimDuration::Zero(), {*direct, *late}},
        AcousticFieldSignalCell{0, SimDuration::FromNanoseconds(10'000'000'000), {*direct}}});
  if(!asset) return false;
  const auto bound = MaximumAssetPropagationTail(*asset);
  // max(first+excess)=10 s is unsafe: interpolated first + nearest paths can exceed it.
  return bound && bound->nanoseconds() == 20'000'000'000;
}

auto TestGenericPlanningWithoutSink() -> bool {
  TemporaryRepositoryRoot temporary;
  auto environments = EnvironmentAssetRepository::Open(temporary.path());
  auto asset = MakeAsset(true);
  auto provenance = MakeProvenance();
  auto asset_id = EnvironmentAssetId::Create("planning-field-v1");
  if(!environments || !asset || !provenance || !asset_id ||
     !environments->Register(*asset_id, *asset, *provenance)) return false;
  const std::vector<NodeCommittedState> nodes{
      {NodeId{0}, {true, true, DuplexMode::kHalfDuplex}, {{0, 0, -40}, {0, 0, 0}}},
      {NodeId{1}, {true, true, DuplexMode::kHalfDuplex}, {{500, 0, -40}, {1, 0, 0}}},
      {NodeId{2}, {false, true, DuplexMode::kHalfDuplex}, {{1000, 100, -40}, {0, 0, 0}}}};
  auto scenario = ScenarioDefinition::Create(*ScenarioId::Create("planning-no-sink"), 1,
      "Explicit traffic", nodes, {asset_id->value(), 1}, MobilityModel::kTrajectoryDriven, std::nullopt);
  if(!scenario || scenario->fusion_center_node_id()) return false;
  const PlanningExecutionConfiguration planning{SimDuration::FromNanoseconds(6'000'000'000),
      SimDuration::Zero(), {{NodeId{0}, NodeId{1}, 1}, {NodeId{1}, NodeId{0}, 1},
                           {NodeId{0}, NodeId{2}, 2}}, std::nullopt};
  auto experiment = ExperimentDefinition::Create(*ExperimentId::Create("planning-traffic"), 1,
      "Automatic slots", {scenario->scenario_id(), 1}, {RoutingMode::kDirect},
      {MacMode::kAdaptiveTdma, SimDuration::Zero(), SimDuration::Zero()},
      {60, 25000, 4000, 110, 45, RxQualityMode::kModeledBpskAwgn},
      {ApplicationWorkload::kCommunicationOnly, AcceptanceProfile::kAcceptance4Node,
       5, SimDuration::FromNanoseconds(180'000'000'000), 200, 150, 1e-4}, 1, 2, 42, planning);
  if(!experiment) return false;
  auto snapshot = WorldSnapshot::Create(SnapshotVersion{0}, SimTime::Zero(), nodes);
  auto trajectories = trajectory::BindGenerated(*snapshot, SimTime::FromNanoseconds(12'000'000'000));
  if(!trajectories) return false;
  NullTraceSink trace;
  RunExecutionExecutor executor{*environments, *trajectories};
  const auto run = *RunId::Create("planning-traffic-run");
  auto first = executor.Execute(run, *scenario, *experiment, trace);
  auto second = executor.Execute(run, *scenario, *experiment, trace);
  if(!first || !second) {
    std::cerr << "Generic planning failed: " << (!first ? first.error().message : second.error().message) << '\n';
    return false;
  }
  if(!SameSimulationResult(*first, *second) || first->projection.cycle_count != 2 ||
     first->projection.transmission_count != 8 || first->projection.local_delivery_count != 8 ||
     first->projection.simulation_ended_at.nanoseconds() != 12'000'000'000 ||
     first->acceptance_report || !first->fusion_results.empty() ||
     std::ranges::any_of(first->nodes, [](const auto& n) { return n.is_fusion_center; })) return false;
  // A user-authored planning run may not generate missing trajectories.
  RunExecutionExecutor missing{*environments};
  if(missing.Execute(run, *scenario, *experiment, trace)) return false;
  auto invalid_planning = planning;
  invalid_planning.flows.front().destination_node_id = NodeId{999};
  auto invalid = ExperimentDefinition::Create(experiment->experiment_id(), 1, experiment->name(),
      experiment->scenario(), experiment->routing(), experiment->mac(), experiment->phy(), experiment->fusion(),
      1, 2, 42, invalid_planning);
  return invalid && !executor.Execute(run, *scenario, *invalid, trace);
}

}  // namespace

auto main() -> int {
  return TestAcceptanceRunServiceAndMissingEvidence() &&
                 TestMissingEnvironmentProducesFailedLifecycle() &&
                 TestCommunicationOnlyUsesActualPayloadAirtime() &&
                 TestMixedCellPropagationTail() && TestGenericPlanningWithoutSink()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
