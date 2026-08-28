#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
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

auto MakeAsset() -> Result<AcousticFieldAsset> {
  auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!frame) return std::unexpected(frame.error());
  std::vector<AcousticFieldCell> cells;
  for(std::size_t index = 0; index < 8U; ++index) {
    cells.push_back(AcousticFieldSignalCell{
        70.0, SimDuration::FromNanoseconds(500'000'000), {}});
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
  const auto expected_event_count =
      result->projection.transmission_count +
      result->projection.channel_signal_count +
      result->projection.channel_no_arrival_count +
      result->projection.reception_count + result->projection.cycle_count;
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
     first_events->size() != expected_event_count ||
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

}  // namespace

auto main() -> int {
  return TestAcceptanceRunServiceAndMissingEvidence() &&
                 TestMissingEnvironmentProducesFailedLifecycle()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
