#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ns3_factory/application/repositories.hpp>
#include <ns3_factory/application/run_service.hpp>
#include <ns3_factory/worker/adapter/process_controller.hpp>
#include <ns3_factory/worker/protocol.hpp>

#include "internal/acceptance_run_executor.hpp"
#include "internal/acoustic_field_asset.hpp"
#include "internal/environment_asset_package.hpp"
#include "internal/environment_asset_repository.hpp"
#include "internal/simulation_worker.hpp"

namespace {

using namespace ns3_factory;
using namespace application;
using namespace contracts;
using namespace environment::internal;
using namespace worker;
using namespace worker::adapter;
using namespace worker::internal;

class TemporaryRepositoryRoot final {
 public:
  TemporaryRepositoryRoot()
      : path_(std::filesystem::temp_directory_path() /
              "ns3_factory_simulation_worker_boundary_test") {
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

class RecordingWorkerSink final : public IWorkerMessageSink {
 public:
  auto Emit(const WorkerMessage& message) noexcept
      -> Status override {
    try {
      messages.push_back(message);
      return {};
    } catch(...) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable,
                "Worker message recording failed"});
    }
  }

  std::vector<WorkerMessage> messages;
};

class RecordingEventConsumer final : public IWorkerEventConsumer {
 public:
  auto OnRunEvent(const RunEventRecord& event) noexcept
      -> Status override {
    try {
      events.push_back(event);
      return {};
    } catch(...) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable,
                "Worker event consumer recording failed"});
    }
  }

  std::vector<RunEventRecord> events;
};

enum class SinkFailurePoint {
  kThirdRunEvent,
  kTerminal,
};

class FailingWorkerSink final : public IWorkerMessageSink {
 public:
  explicit FailingWorkerSink(SinkFailurePoint failure_point) noexcept
      : failure_point_(failure_point) {}

  auto Emit(const WorkerMessage& message) noexcept
      -> Status override {
    if(const auto* event = std::get_if<WorkerRunEvent>(&message)) {
      ++event_attempts_;
      if(failure_point_ == SinkFailurePoint::kThirdRunEvent &&
         event_attempts_ == 3U) {
        return BridgeFailure();
      }
      (void)event;
    }
    if(failure_point_ == SinkFailurePoint::kTerminal &&
       (std::holds_alternative<WorkerCompleted>(message) ||
        std::holds_alternative<WorkerFailed>(message))) {
      return BridgeFailure();
    }
    try {
      messages.push_back(message);
      return {};
    } catch(...) {
      return BridgeFailure();
    }
  }

  std::vector<WorkerMessage> messages;

 private:
  [[nodiscard]] static auto BridgeFailure() noexcept -> Status {
    return std::unexpected(
        Error{ErrorCode::kUnavailable,
              "Worker message bridge rejected output"});
  }

  SinkFailurePoint failure_point_;
  std::size_t event_attempts_{0};
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
                                    "simulation worker fixture",
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
      "simulation-worker-test",
      "deterministic process isolation fixture",
      "",
      "");
}

auto MakeRequest(std::string run_text,
                 std::string asset_text,
                 double noise_power = 45.0)
    -> Result<AcceptanceWorkerRequest> {
  auto run_id = RunId::Create(std::move(run_text));
  auto scenario_id = ScenarioId::Create("worker-test-scenario");
  auto experiment_id = ExperimentId::Create("worker-test-experiment");
  if(!run_id || !scenario_id || !experiment_id) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "Worker request fixture IDs are invalid"});
  }
  return AcceptanceWorkerRequest{
      std::move(*run_id),
      std::move(*scenario_id),
      std::move(*experiment_id),
      1,
      EnvironmentReference{std::move(asset_text), 1},
      AcceptanceProfile::kAcceptance4Node,
      2,
      RxQualityMode::kModeledBpskAwgn,
      noise_power,
      19};
}

struct CapturedWorkerOutput final {
  std::vector<RunEventRecord> events;
  std::optional<WorkerCompleted> completed;
  std::optional<WorkerFailed> failed;
};

auto CaptureOutput(const RecordingWorkerSink& sink,
                   const RunId& expected_run_id)
    -> Result<CapturedWorkerOutput> {
  if(sink.messages.empty() ||
     !std::holds_alternative<WorkerStarted>(sink.messages.front()) ||
     std::get<WorkerStarted>(sink.messages.front()).run_id !=
         expected_run_id) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "WorkerStarted is missing or out of order"});
  }
  CapturedWorkerOutput output;
  for(std::size_t index = 1; index < sink.messages.size(); ++index) {
    const auto& message = sink.messages[index];
    if(const auto* event = std::get_if<WorkerRunEvent>(&message)) {
      if(output.completed || output.failed) {
        return std::unexpected(
            Error{ErrorCode::kFailedPrecondition,
                  "Worker event followed a terminal message"});
      }
      output.events.push_back(event->record);
    } else if(const auto* completed =
                  std::get_if<WorkerCompleted>(&message)) {
      if(output.completed || output.failed ||
         index + 1U != sink.messages.size()) {
        return std::unexpected(
            Error{ErrorCode::kFailedPrecondition,
                  "WorkerCompleted is not the unique terminal message"});
      }
      output.completed = *completed;
    } else if(const auto* failed = std::get_if<WorkerFailed>(&message)) {
      if(output.completed || output.failed ||
         index + 1U != sink.messages.size()) {
        return std::unexpected(
            Error{ErrorCode::kFailedPrecondition,
                  "WorkerFailed is not the unique terminal message"});
      }
      output.failed = *failed;
    } else {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "WorkerStarted appeared more than once"});
    }
  }
  return output;
}

auto SameSimulationResult(const RunResult& lhs,
                          const RunResult& rhs) -> bool {
  return lhs.projection == rhs.projection &&
         lhs.acceptance_report == rhs.acceptance_report &&
         lhs.fusion_results == rhs.fusion_results && lhs.nodes == rhs.nodes;
}

auto SameEventPayloadOrder(const std::vector<RunEventRecord>& lhs,
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

auto ExecuteDirect(
    const EnvironmentAssetRepository& environments,
    const AcceptanceWorkerRequest& request,
    RunId direct_run_id)
    -> Result<std::pair<RunResult, std::vector<RunEventRecord>>> {
  auto definitions = MakeAcceptanceWorkerDefinitions(request);
  if(!definitions) return std::unexpected(definitions.error());
  ScenarioRepository scenarios;
  ExperimentRepository experiments;
  RunRepository runs;
  InMemoryRunEventJournal events;
  if(const auto registered = scenarios.Register(definitions->scenario);
     !registered) {
    return std::unexpected(registered.error());
  }
  if(const auto registered = experiments.Register(definitions->experiment);
     !registered) {
    return std::unexpected(registered.error());
  }
  application::internal::AcceptanceRunExecutor executor{environments};
  RunService service{scenarios, experiments, runs, executor, events};
  const auto reference = ExperimentReference{
      definitions->experiment.experiment_id(),
      definitions->experiment.version()};
  if(const auto created = service.CreateRun(direct_run_id, reference);
     !created) {
    return std::unexpected(created.error());
  }
  auto result = service.ExecuteRun(direct_run_id);
  if(!result) return std::unexpected(result.error());
  auto replay = service.ReadEvents(
      direct_run_id,
      RunEventSequence::BeforeFirst(),
      kMaximumRunEventReadLimit);
  if(!replay) return std::unexpected(replay.error());
  return std::pair{std::move(*result), std::move(*replay)};
}

auto RunChild(const std::filesystem::path& repository_root,
              std::string_view asset_id,
              std::string_view run_id,
              std::string_view mode) -> int {
  const auto executable = std::string{PLATFORM_SIM_WORKER_PATH};
  const auto root = repository_root.string();
  const auto asset = std::string{asset_id};
  const auto run = std::string{run_id};
  const auto worker_mode = std::string{mode};
  const auto child = ::fork();
  if(child < 0) return -1;
  if(child == 0) {
    ::execl(executable.c_str(),
            executable.c_str(),
            root.c_str(),
            asset.c_str(),
            run.c_str(),
            worker_mode.c_str(),
            static_cast<char*>(nullptr));
    ::_exit(127);
  }
  int status = 0;
  while(::waitpid(child, &status, 0) < 0) {
    if(errno != EINTR) return -1;
  }
  if(!WIFEXITED(status)) return -1;
  return WEXITSTATUS(status);
}

auto TestTypedBridgeAndDirectEquivalence(
    const EnvironmentAssetRepository& environments,
    std::string_view asset_id) -> bool {
  auto request = MakeRequest("worker-run-a", std::string{asset_id});
  auto second_request = MakeRequest("worker-run-b", std::string{asset_id});
  auto direct_id = RunId::Create("worker-direct-baseline");
  if(!request || !second_request || !direct_id) return false;
  SimulationWorker first_worker{environments};
  RecordingWorkerSink first_sink;
  const auto first_status = first_worker.Run(*request, first_sink);
  const auto reuse_status = first_worker.Run(*request, first_sink);
  auto first = CaptureOutput(first_sink, request->run_id);
  auto direct = ExecuteDirect(environments, *request, std::move(*direct_id));
  if(!first_status || reuse_status || !first || !direct ||
     !first->completed || first->failed) {
    return false;
  }
  if(!SameSimulationResult(first->completed->result, direct->first) ||
     !SameEventPayloadOrder(first->events, direct->second) ||
     first->completed->run.lifecycle != RunLifecycle::kCompleted ||
     first->completed->result.projection.simulation_started_at !=
         SimTime::Zero()) {
    return false;
  }
  for(std::size_t index = 0; index < first->events.size(); ++index) {
    if(first->events[index].run_id != request->run_id ||
       first->events[index].sequence.value() != index + 1U) {
      return false;
    }
  }

  SimulationWorker second_worker{environments};
  RecordingWorkerSink second_sink;
  const auto second_status = second_worker.Run(*second_request, second_sink);
  auto second = CaptureOutput(second_sink, second_request->run_id);
  return second_status && second && second->completed && !second->failed &&
         SameSimulationResult(first->completed->result,
                              second->completed->result) &&
         SameEventPayloadOrder(first->events, second->events) &&
         second->completed->result.projection.simulation_started_at ==
             SimTime::Zero();
}

auto TestVerdictFailureIsWorkerSuccess(
    const EnvironmentAssetRepository& environments,
    std::string_view asset_id) -> bool {
  auto request =
      MakeRequest("worker-verdict-fail", std::string{asset_id}, 180.0);
  if(!request) return false;
  SimulationWorker simulation_worker{environments};
  RecordingWorkerSink sink;
  const auto status = simulation_worker.Run(*request, sink);
  const auto output = CaptureOutput(sink, request->run_id);
  return status && output && output->completed && !output->failed &&
         output->completed->result.acceptance_report &&
         output->completed->result.acceptance_report->overall ==
             OverallStatus::kFail;
}

auto TestSimulationFailureIsIsolated(
    const EnvironmentAssetRepository& environments) -> bool {
  auto request = MakeRequest("worker-simulation-fail", "missing-asset");
  if(!request) return false;
  SimulationWorker simulation_worker{environments};
  RecordingWorkerSink sink;
  const auto status = simulation_worker.Run(*request, sink);
  const auto output = CaptureOutput(sink, request->run_id);
  return !status && output && !output->completed && output->failed &&
         output->failed->error.code == ErrorCode::kNotFound &&
         output->failed->run &&
         output->failed->run->lifecycle == RunLifecycle::kFailed;
}

auto TestBridgeFailureIsNoncausal(
    const EnvironmentAssetRepository& environments,
    std::string_view asset_id) -> bool {
  auto request = MakeRequest("worker-bridge-fail", std::string{asset_id});
  auto direct_id = RunId::Create("worker-bridge-direct-baseline");
  if(!request || !direct_id) return false;
  auto direct = ExecuteDirect(environments, *request, std::move(*direct_id));
  if(!direct || direct->second.size() < 3U) return false;

  SimulationWorker event_failure_worker{environments};
  FailingWorkerSink event_failure_sink{SinkFailurePoint::kThirdRunEvent};
  const auto event_failure_status =
      event_failure_worker.Run(*request, event_failure_sink);
  const auto& event_snapshot = event_failure_worker.last_execution();
  if(event_failure_status ||
     event_failure_status.error().code != ErrorCode::kUnavailable ||
     !event_snapshot || !event_snapshot->result ||
     event_snapshot->run.lifecycle != RunLifecycle::kCompleted ||
     !SameSimulationResult(*event_snapshot->result, direct->first) ||
     !SameEventPayloadOrder(event_snapshot->events, direct->second) ||
     event_failure_sink.messages.size() != 3U ||
     !std::holds_alternative<WorkerStarted>(
         event_failure_sink.messages.front())) {
    return false;
  }
  for(const auto& message : event_failure_sink.messages) {
    if(std::holds_alternative<WorkerCompleted>(message) ||
       std::holds_alternative<WorkerFailed>(message)) {
      return false;
    }
  }

  auto terminal_request =
      MakeRequest("worker-terminal-bridge-fail", std::string{asset_id});
  if(!terminal_request) return false;
  SimulationWorker terminal_failure_worker{environments};
  FailingWorkerSink terminal_failure_sink{SinkFailurePoint::kTerminal};
  const auto terminal_failure_status =
      terminal_failure_worker.Run(*terminal_request,
                                  terminal_failure_sink);
  const auto& terminal_snapshot = terminal_failure_worker.last_execution();
  if(terminal_failure_status ||
     terminal_failure_status.error().code != ErrorCode::kUnavailable ||
     !terminal_snapshot || !terminal_snapshot->result ||
     terminal_snapshot->run.lifecycle != RunLifecycle::kCompleted ||
     !SameSimulationResult(*terminal_snapshot->result, direct->first) ||
     !SameEventPayloadOrder(terminal_snapshot->events, direct->second)) {
    return false;
  }
  for(const auto& message : terminal_failure_sink.messages) {
    if(std::holds_alternative<WorkerCompleted>(message) ||
       std::holds_alternative<WorkerFailed>(message)) {
      return false;
    }
  }
  return true;
}

auto TestIndependentWorkerProcesses(
    const std::filesystem::path& repository_root,
    std::string_view asset_id) -> bool {
  const auto first =
      RunChild(repository_root, asset_id, "process-run-a", "pass");
  const auto second =
      RunChild(repository_root, asset_id, "process-run-b", "pass");
  const auto failed = RunChild(repository_root,
                               "missing-asset",
                               "process-run-failed",
                               "pass");
  const auto after_failure =
      RunChild(repository_root, asset_id, "process-run-c", "pass");
  const auto verdict_fail = RunChild(repository_root,
                                     asset_id,
                                     "process-verdict-fail",
                                     "verdict-fail");
  const auto protocol_failure = RunChild(repository_root,
                                         asset_id,
                                         "process-protocol-fail",
                                         "unsupported");
  return first == static_cast<int>(WorkerExitCode::kCompleted) &&
         second == static_cast<int>(WorkerExitCode::kCompleted) &&
         failed == static_cast<int>(WorkerExitCode::kExecutionFailure) &&
         after_failure == static_cast<int>(WorkerExitCode::kCompleted) &&
         verdict_fail == static_cast<int>(WorkerExitCode::kCompleted) &&
         protocol_failure ==
             static_cast<int>(WorkerExitCode::kProtocolFailure);
}

auto MakeWireCommand(const AcceptanceWorkerRequest& request)
    -> StartRunCommand {
  return StartRunCommand{request.run_id,
                         request.scenario_id,
                         request.experiment_id,
                         request.definition_version,
                         request.environment,
                         request.profile,
                         request.simulation_cycle_count,
                         request.quality_mode,
                         request.equivalent_noise_power_db_re_1upa2,
                         request.deterministic_seed};
}

auto TestWireProcessController(
    const std::filesystem::path& repository_root,
    const EnvironmentAssetRepository& environments,
    std::string_view asset_id) -> bool {
  auto request = MakeRequest("wire-process-run", std::string{asset_id});
  auto direct_id = RunId::Create("wire-process-direct");
  if(!request || !direct_id) return false;
  auto direct = ExecuteDirect(environments, *request, std::move(*direct_id));
  if(!direct) return false;

  WorkerProcessController controller{PLATFORM_SIM_WORKER_PATH,
                                     repository_root};
  RecordingEventConsumer events;
  auto result = controller.Run(MakeWireCommand(*request), events);
  if(!result) std::cerr << "wire success: " << result.error().message << '\n';
  if(!result || controller.state() != WorkerProcessState::kCompleted ||
     result->exit_code != 0 || !result->completed || result->failed ||
     result->completed->run.lifecycle != RunLifecycle::kCompleted ||
     !result->completed->run.event_stream_complete.value_or(false) ||
     !SameSimulationResult(result->completed->result, direct->first) ||
     !SameEventPayloadOrder(events.events, direct->second) ||
     events.events.empty() || events.events.front().sequence.value() != 1U) {
    return false;
  }

  auto verdict_request =
      MakeRequest("wire-verdict-fail", std::string{asset_id}, 180.0);
  if(!verdict_request) return false;
  WorkerProcessController verdict_controller{PLATFORM_SIM_WORKER_PATH,
                                             repository_root};
  RecordingEventConsumer verdict_events;
  auto verdict = verdict_controller.Run(MakeWireCommand(*verdict_request),
                                        verdict_events);
  if(!verdict) std::cerr << "wire verdict: " << verdict.error().message << '\n';
  if(!verdict || verdict_controller.state() !=
                       WorkerProcessState::kCompleted ||
     !verdict->completed || verdict->exit_code != 0 ||
     !verdict->completed->result.acceptance_report ||
     verdict->completed->result.acceptance_report->overall !=
         OverallStatus::kFail) {
    return false;
  }

  auto failed_request = MakeRequest("wire-simulation-fail",
                                    "missing-asset");
  if(!failed_request) return false;
  WorkerProcessController failed_controller{PLATFORM_SIM_WORKER_PATH,
                                            repository_root};
  RecordingEventConsumer failed_events;
  auto failed = failed_controller.Run(MakeWireCommand(*failed_request),
                                      failed_events);
  if(!failed) std::cerr << "wire expected failure: " << failed.error().message << '\n';
  if(!failed || failed_controller.state() != WorkerProcessState::kFailed ||
     failed->exit_code == 0 || failed->completed || !failed->failed ||
     failed->failed->category != WorkerFailureCategory::kSimulation) {
    return false;
  }

  WorkerProcessController crash_controller{
      PLATFORM_WORKER_CRASH_FIXTURE_PATH, repository_root};
  RecordingEventConsumer crash_events;
  const auto crashed =
      crash_controller.Run(MakeWireCommand(*request), crash_events);
  WorkerProcessController eof_controller{
      PLATFORM_WORKER_EOF_FIXTURE_PATH, repository_root};
  RecordingEventConsumer eof_events;
  const auto premature_eof =
      eof_controller.Run(MakeWireCommand(*request), eof_events);
  WorkerProcessController failed_zero_controller{
      PLATFORM_WORKER_FAILED_EXIT_ZERO_FIXTURE_PATH, repository_root};
  RecordingEventConsumer failed_zero_events;
  const auto failed_zero = failed_zero_controller.Run(
      MakeWireCommand(*request), failed_zero_events);
  WorkerProcessController completed_nonzero_controller{
      PLATFORM_WORKER_COMPLETED_NONZERO_FIXTURE_PATH, repository_root};
  RecordingEventConsumer completed_nonzero_events;
  const auto completed_nonzero = completed_nonzero_controller.Run(
      MakeWireCommand(*request), completed_nonzero_events);
  if(crashed || crash_controller.state() != WorkerProcessState::kFailed ||
     premature_eof ||
     eof_controller.state() != WorkerProcessState::kFailed ||
     failed_zero ||
     failed_zero_controller.state() != WorkerProcessState::kFailed ||
     completed_nonzero ||
     completed_nonzero_controller.state() != WorkerProcessState::kFailed) {
    return false;
  }

  auto second_request = MakeRequest("wire-process-run-two",
                                    std::string{asset_id});
  if(!second_request) return false;
  WorkerProcessController second_controller{PLATFORM_SIM_WORKER_PATH,
                                            repository_root};
  RecordingEventConsumer second_events;
  auto second = second_controller.Run(MakeWireCommand(*second_request),
                                      second_events);
  return second && second->completed && !second_events.events.empty() &&
         second_events.events.front().sequence.value() == 1U &&
         second->completed->result.projection.simulation_started_at ==
             SimTime::Zero() &&
         SameSimulationResult(result->completed->result,
                              second->completed->result);
}

}  // namespace

auto main() -> int {
  TemporaryRepositoryRoot temporary;
  auto environments = EnvironmentAssetRepository::Open(temporary.path());
  auto asset = MakeAsset();
  auto provenance = MakeProvenance();
  auto asset_id = EnvironmentAssetId::Create("worker-field-v1");
  if(!environments || !asset || !provenance || !asset_id ||
     !environments->Register(*asset_id, *asset, *provenance)) {
    return EXIT_FAILURE;
  }
  const auto typed = TestTypedBridgeAndDirectEquivalence(
      *environments, asset_id->value());
  const auto verdict = TestVerdictFailureIsWorkerSuccess(
      *environments, asset_id->value());
  const auto isolated = TestSimulationFailureIsIsolated(*environments);
  const auto bridge = TestBridgeFailureIsNoncausal(
      *environments, asset_id->value());
  const auto wire = TestWireProcessController(
      temporary.path(), *environments, asset_id->value());
  const auto processes = TestIndependentWorkerProcesses(
      temporary.path(), asset_id->value());
  if(!typed) std::cerr << "typed bridge failed\n";
  if(!verdict) std::cerr << "verdict failed\n";
  if(!isolated) std::cerr << "isolation failed\n";
  if(!bridge) std::cerr << "bridge failure gate failed\n";
  if(!wire) std::cerr << "wire controller failed\n";
  if(!processes) std::cerr << "process smoke failed\n";
  return typed && verdict && isolated && bridge && wire && processes
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
