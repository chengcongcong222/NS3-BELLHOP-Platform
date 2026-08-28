#pragma once

#include <functional>
#include <utility>

#include <ns3_factory/application/repositories.hpp>

namespace ns3_factory::application {

class RunService final {
 public:
  RunService(const ScenarioRepository& scenarios,
             const ExperimentRepository& experiments,
             RunRepository& runs,
             const IRunExecutor& executor,
             IRunEventJournal& events) noexcept
      : scenarios_(scenarios),
        experiments_(experiments),
        runs_(runs),
        executor_(executor),
        events_(events) {}

  [[nodiscard]] auto CreateRun(RunId run_id,
                               ExperimentReference experiment_reference)
      -> contracts::Result<RunRecord> {
    const auto experiment = experiments_.get().Find(experiment_reference);
    if(!experiment) return std::unexpected(experiment.error());
    const auto scenario = scenarios_.get().Find(experiment->scenario());
    if(!scenario) return std::unexpected(scenario.error());
    RunRecord record{std::move(run_id),
                     experiment_reference,
                     experiment->scenario(),
                     scenario->environment(),
                     RunLifecycle::kCreated,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt,
                     std::nullopt};
    const auto inserted = runs_.get().Insert(record);
    if(!inserted) return std::unexpected(inserted.error());
    return record;
  }

  [[nodiscard]] auto ExecuteRun(const RunId& run_id)
      -> contracts::Result<RunResult> {
    auto record = runs_.get().Find(run_id);
    if(!record) return std::unexpected(record.error());
    if(record->lifecycle != RunLifecycle::kCreated) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Run lifecycle is terminal or already executing"});
    }
    const auto experiment = experiments_.get().Find(record->experiment);
    if(!experiment) return Fail(*record, experiment.error());
    const auto scenario = scenarios_.get().Find(record->scenario);
    if(!scenario) return Fail(*record, scenario.error());

    record->lifecycle = RunLifecycle::kRunning;
    record->simulation_started_at = contracts::SimTime::Zero();
    const auto running = runs_.get().Replace(*record);
    if(!running) return std::unexpected(running.error());

    RunEventSink event_sink{run_id, events_.get()};
    auto result =
        executor_.get().Execute(run_id, *scenario, *experiment, event_sink);
    if(!result) {
      return Fail(*record,
                  result.error(),
                  event_sink.event_stream_complete());
    }
    if(result->run_id != run_id ||
       result->projection.simulation_started_at !=
           *record->simulation_started_at) {
      return Fail(
          *record,
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Run executor returned mismatched provenance"},
          event_sink.event_stream_complete());
    }
    record->lifecycle = RunLifecycle::kCompleted;
    record->simulation_ended_at = result->projection.simulation_ended_at;
    record->final_snapshot_version =
        result->projection.final_snapshot_version;
    record->failure.reset();
    record->event_stream_complete = event_sink.event_stream_complete();
    const auto completed = runs_.get().Complete(*record, *result);
    if(!completed) return std::unexpected(completed.error());
    return result;
  }

  [[nodiscard]] auto GetRun(const RunId& run_id) const
      -> contracts::Result<RunRecord> {
    return runs_.get().Find(run_id);
  }

  [[nodiscard]] auto GetResult(const RunId& run_id) const
      -> contracts::Result<RunResult> {
    return runs_.get().GetResult(run_id);
  }

  [[nodiscard]] auto ReadEvents(const RunId& run_id,
                                RunEventSequence cursor,
                                std::size_t limit) const
      -> contracts::Result<std::vector<RunEventRecord>> {
    const auto record = runs_.get().Find(run_id);
    if(!record) return std::unexpected(record.error());
    return events_.get().ReadAfter(run_id, cursor, limit);
  }

  [[nodiscard]] auto GetLatestEventSequence(const RunId& run_id) const
      -> contracts::Result<RunEventSequence> {
    const auto record = runs_.get().Find(run_id);
    if(!record) return std::unexpected(record.error());
    return events_.get().GetLatestSequence(run_id);
  }

 private:
  [[nodiscard]] auto Fail(
      RunRecord record,
      contracts::Error error,
      std::optional<bool> event_stream_complete = std::nullopt)
      -> contracts::Result<RunResult> {
    record.lifecycle = RunLifecycle::kFailed;
    record.simulation_ended_at.reset();
    record.final_snapshot_version.reset();
    record.failure = RunFailureSummary{error.code, error.message};
    record.event_stream_complete = event_stream_complete;
    const auto replaced = runs_.get().Replace(std::move(record));
    if(!replaced) return std::unexpected(replaced.error());
    return std::unexpected(std::move(error));
  }

  std::reference_wrapper<const ScenarioRepository> scenarios_;
  std::reference_wrapper<const ExperimentRepository> experiments_;
  std::reference_wrapper<RunRepository> runs_;
  std::reference_wrapper<const IRunExecutor> executor_;
  std::reference_wrapper<IRunEventJournal> events_;
};

}  // namespace ns3_factory::application
