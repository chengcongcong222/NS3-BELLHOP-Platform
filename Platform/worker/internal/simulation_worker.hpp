#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/application/repositories.hpp>
#include <ns3_factory/application/run_service.hpp>
#include <ns3_factory/worker/protocol.hpp>

#include "internal/acceptance_preset.hpp"
#include "internal/acceptance_run_executor.hpp"
#include "internal/environment_asset_repository.hpp"

namespace ns3_factory::worker::internal {

struct AcceptanceWorkerRequest final {
  application::RunId run_id;
  application::ScenarioId scenario_id;
  application::ExperimentId experiment_id;
  application::ResourceVersion definition_version;
  application::EnvironmentReference environment;
  application::AcceptanceProfile profile;
  std::size_t simulation_cycle_count;
  application::RxQualityMode quality_mode;
  double equivalent_noise_power_db_re_1upa2;
  std::uint64_t deterministic_seed;
};

struct AcceptanceWorkerDefinitions final {
  application::ScenarioDefinition scenario;
  application::ExperimentDefinition experiment;
};

struct WorkerExecutionSnapshot final {
  application::RunRecord run;
  std::optional<application::RunResult> result;
  std::vector<application::RunEventRecord> events;
};

[[nodiscard]] inline auto MakeAcceptanceWorkerDefinitions(
    const AcceptanceWorkerRequest& request)
    -> contracts::Result<AcceptanceWorkerDefinitions> {
  auto scenario = application::internal::MakeAcceptanceScenarioPreset(
      request.profile,
      request.scenario_id,
      request.definition_version,
      "Worker Acceptance Scenario",
      request.environment);
  if(!scenario) return std::unexpected(scenario.error());
  auto experiment = application::internal::MakeAcceptanceExperimentPreset(
      request.profile,
      request.experiment_id,
      request.definition_version,
      "Worker Acceptance Experiment",
      application::ScenarioReference{scenario->scenario_id(),
                                     scenario->version()},
      request.simulation_cycle_count,
      request.quality_mode,
      request.equivalent_noise_power_db_re_1upa2,
      request.deterministic_seed);
  if(!experiment) return std::unexpected(experiment.error());
  return AcceptanceWorkerDefinitions{std::move(*scenario),
                                     std::move(*experiment)};
}

class SimulationWorker final {
 public:
  explicit SimulationWorker(
      const environment::internal::EnvironmentAssetRepository& environments)
      noexcept
      : environments_(environments) {}

  [[nodiscard]] auto last_execution() const noexcept
      -> const std::optional<WorkerExecutionSnapshot>& {
    return last_execution_;
  }

  [[nodiscard]] auto Run(const AcceptanceWorkerRequest& request,
                         IWorkerMessageSink& output)
      -> contracts::Status {
    if(used_) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "A SimulationWorker executes exactly one Run"});
    }
    used_ = true;
    if(const auto emitted = output.Emit(WorkerStarted{request.run_id});
       !emitted) {
      return std::unexpected(emitted.error());
    }
    auto definitions = MakeAcceptanceWorkerDefinitions(request);
    if(!definitions) {
      return EmitFailure(request.run_id,
                         definitions.error(),
                         std::nullopt,
                         output);
    }

    application::ScenarioRepository scenarios;
    application::ExperimentRepository experiments;
    application::RunRepository runs;
    application::InMemoryRunEventJournal events;
    if(const auto registered =
           scenarios.Register(definitions->scenario);
       !registered) {
      return EmitFailure(request.run_id,
                         registered.error(),
                         std::nullopt,
                         output);
    }
    if(const auto registered =
           experiments.Register(definitions->experiment);
       !registered) {
      return EmitFailure(request.run_id,
                         registered.error(),
                         std::nullopt,
                         output);
    }
    application::internal::AcceptanceRunExecutor executor{
        environments_.get()};
    application::RunService service{
        scenarios, experiments, runs, executor, events};
    const auto reference = application::ExperimentReference{
        definitions->experiment.experiment_id(),
        definitions->experiment.version()};
    const auto created = service.CreateRun(request.run_id, reference);
    if(!created) {
      return EmitFailure(request.run_id,
                         created.error(),
                         std::nullopt,
                         output);
    }

    const auto executed = service.ExecuteRun(request.run_id);
    const auto record = service.GetRun(request.run_id);
    if(!record) {
      return EmitFailure(request.run_id,
                         record.error(),
                         std::nullopt,
                         output);
    }
    auto collected_events = CollectEvents(service, request.run_id);
    if(!collected_events) {
      return EmitFailure(request.run_id,
                         collected_events.error(),
                         *record,
                         output);
    }
    if(!executed) {
      last_execution_.emplace(
          WorkerExecutionSnapshot{*record,
                                  std::nullopt,
                                  *collected_events});
      if(const auto emitted = EmitEvents(*collected_events, output);
         !emitted) {
        return emitted;
      }
      return EmitFailure(request.run_id,
                         executed.error(),
                         *record,
                         output);
    }
    const auto result = service.GetResult(request.run_id);
    if(!result) {
      last_execution_.emplace(
          WorkerExecutionSnapshot{*record,
                                  std::nullopt,
                                  *collected_events});
      if(const auto emitted = EmitEvents(*collected_events, output);
         !emitted) {
        return emitted;
      }
      return EmitFailure(request.run_id,
                         result.error(),
                         *record,
                         output);
    }
    last_execution_.emplace(
        WorkerExecutionSnapshot{*record, *result, *collected_events});
    if(const auto emitted = EmitEvents(*collected_events, output);
       !emitted) {
      return emitted;
    }
    const auto emitted = output.Emit(WorkerCompleted{*record, *result});
    if(!emitted) return std::unexpected(emitted.error());
    return {};
  }

 private:
  [[nodiscard]] static auto CollectEvents(
      const application::RunService& service,
      const application::RunId& run_id)
      -> contracts::Result<std::vector<application::RunEventRecord>> {
    std::vector<application::RunEventRecord> events;
    auto cursor = application::RunEventSequence::BeforeFirst();
    while(true) {
      const auto page = service.ReadEvents(
          run_id, cursor, application::kMaximumRunEventReadLimit);
      if(!page) return std::unexpected(page.error());
      if(page->empty()) return events;
      for(const auto& record : *page) {
        events.push_back(record);
        cursor = record.sequence;
      }
    }
  }

  [[nodiscard]] static auto EmitEvents(
      const std::vector<application::RunEventRecord>& events,
      IWorkerMessageSink& output) -> contracts::Status {
    for(const auto& record : events) {
      const auto emitted = output.Emit(WorkerRunEvent{record});
      if(!emitted) return std::unexpected(emitted.error());
    }
    return {};
  }

  [[nodiscard]] static auto EmitFailure(
      const application::RunId& run_id,
      contracts::Error error,
      std::optional<application::RunRecord> run,
      IWorkerMessageSink& output) -> contracts::Status {
    const auto emitted = output.Emit(
        WorkerFailed{run_id, error, std::move(run)});
    if(!emitted) return std::unexpected(emitted.error());
    return std::unexpected(std::move(error));
  }

  std::reference_wrapper<
      const environment::internal::EnvironmentAssetRepository>
      environments_;
  std::optional<WorkerExecutionSnapshot> last_execution_;
  bool used_{false};
};

}  // namespace ns3_factory::worker::internal
