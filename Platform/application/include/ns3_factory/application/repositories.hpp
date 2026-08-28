#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <ns3_factory/application/result.hpp>

namespace ns3_factory::application {

class RunService;

class ScenarioRepository final {
 public:
  [[nodiscard]] auto Register(ScenarioDefinition definition)
      -> contracts::Status {
    const auto position = std::lower_bound(
        definitions_.begin(),
        definitions_.end(),
        definition,
        [](const ScenarioDefinition& lhs, const ScenarioDefinition& rhs) {
          if(lhs.scenario_id() != rhs.scenario_id()) {
            return lhs.scenario_id() < rhs.scenario_id();
          }
          return lhs.version() < rhs.version();
        });
    if(position != definitions_.end() &&
       position->scenario_id() == definition.scenario_id() &&
       position->version() == definition.version()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kAlreadyExists,
                           "Scenario ID/version is already registered"});
    }
    definitions_.insert(position, std::move(definition));
    return {};
  }

  [[nodiscard]] auto Find(const ScenarioReference& reference) const
      -> contracts::Result<ScenarioDefinition> {
    const auto position = std::lower_bound(
        definitions_.begin(),
        definitions_.end(),
        reference,
        [](const ScenarioDefinition& lhs, const ScenarioReference& rhs) {
          if(lhs.scenario_id() != rhs.scenario_id) {
            return lhs.scenario_id() < rhs.scenario_id;
          }
          return lhs.version() < rhs.scenario_version;
        });
    if(position == definitions_.end() ||
       position->scenario_id() != reference.scenario_id ||
       position->version() != reference.scenario_version) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kNotFound,
                           "Scenario ID/version was not found"});
    }
    return *position;
  }

 private:
  std::vector<ScenarioDefinition> definitions_;
};

class ExperimentRepository final {
 public:
  [[nodiscard]] auto Register(ExperimentDefinition definition)
      -> contracts::Status {
    const auto position = std::lower_bound(
        definitions_.begin(),
        definitions_.end(),
        definition,
        [](const ExperimentDefinition& lhs,
           const ExperimentDefinition& rhs) {
          if(lhs.experiment_id() != rhs.experiment_id()) {
            return lhs.experiment_id() < rhs.experiment_id();
          }
          return lhs.version() < rhs.version();
        });
    if(position != definitions_.end() &&
       position->experiment_id() == definition.experiment_id() &&
       position->version() == definition.version()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kAlreadyExists,
                           "Experiment ID/version is already registered"});
    }
    definitions_.insert(position, std::move(definition));
    return {};
  }

  [[nodiscard]] auto Find(const ExperimentReference& reference) const
      -> contracts::Result<ExperimentDefinition> {
    const auto position = std::lower_bound(
        definitions_.begin(),
        definitions_.end(),
        reference,
        [](const ExperimentDefinition& lhs,
           const ExperimentReference& rhs) {
          if(lhs.experiment_id() != rhs.experiment_id) {
            return lhs.experiment_id() < rhs.experiment_id;
          }
          return lhs.version() < rhs.experiment_version;
        });
    if(position == definitions_.end() ||
       position->experiment_id() != reference.experiment_id ||
       position->version() != reference.experiment_version) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kNotFound,
                           "Experiment ID/version was not found"});
    }
    return *position;
  }

 private:
  std::vector<ExperimentDefinition> definitions_;
};

class RunRepository final {
 public:
  [[nodiscard]] auto Insert(RunRecord record) -> contracts::Status {
    if(record.lifecycle != RunLifecycle::kCreated ||
       record.simulation_started_at || record.simulation_ended_at ||
       record.final_snapshot_version || record.failure ||
       record.event_stream_complete) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "A new RunRecord must be in the Created state"});
    }
    const auto position = FindPosition(record.run_id);
    if(position != entries_.end() && position->record.run_id == record.run_id) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kAlreadyExists,
                           "RunId is already registered"});
    }
    entries_.insert(position, Entry{std::move(record), std::nullopt});
    return {};
  }

  [[nodiscard]] auto Find(const RunId& run_id) const
      -> contracts::Result<RunRecord> {
    const auto position = FindPosition(run_id);
    if(position == entries_.end() || position->record.run_id != run_id) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kNotFound,
                           "RunId was not found"});
    }
    return position->record;
  }

 private:
  [[nodiscard]] auto Replace(RunRecord record) -> contracts::Status {
    const auto position = FindPosition(record.run_id);
    if(position == entries_.end() || position->record.run_id != record.run_id) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kNotFound,
                           "RunId was not found"});
    }
    const auto same_inputs =
        position->record.experiment == record.experiment &&
        position->record.scenario == record.scenario &&
        position->record.environment == record.environment;
    const auto begins_running =
        position->record.lifecycle == RunLifecycle::kCreated &&
        record.lifecycle == RunLifecycle::kRunning &&
        record.simulation_started_at && !record.simulation_ended_at &&
        !record.final_snapshot_version && !record.failure &&
        !record.event_stream_complete;
    const auto becomes_failed =
        position->record.lifecycle == RunLifecycle::kRunning &&
        record.lifecycle == RunLifecycle::kFailed &&
        record.simulation_started_at && !record.simulation_ended_at &&
        !record.final_snapshot_version && record.failure &&
        record.event_stream_complete;
    if(!same_inputs || position->result ||
       (!begins_running && !becomes_failed)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Run lifecycle transition is invalid"});
    }
    position->record = std::move(record);
    return {};
  }

  [[nodiscard]] auto Complete(RunRecord record, RunResult result)
      -> contracts::Status {
    const auto position = FindPosition(record.run_id);
    if(position == entries_.end() || position->record.run_id != record.run_id) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kNotFound,
                           "RunId was not found"});
    }
    if(position->record.lifecycle != RunLifecycle::kRunning ||
       record.lifecycle != RunLifecycle::kCompleted ||
       result.run_id != record.run_id || position->result ||
       position->record.experiment != record.experiment ||
       position->record.scenario != record.scenario ||
       position->record.environment != record.environment ||
       !record.simulation_started_at || !record.simulation_ended_at ||
       !record.final_snapshot_version || record.failure ||
       !record.event_stream_complete ||
       *record.simulation_started_at !=
           result.projection.simulation_started_at ||
       *record.simulation_ended_at !=
           result.projection.simulation_ended_at ||
       *record.final_snapshot_version !=
           result.projection.final_snapshot_version) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Run cannot publish a result in its current state"});
    }
    position->record = std::move(record);
    position->result = std::move(result);
    return {};
  }

 public:
  [[nodiscard]] auto GetResult(const RunId& run_id) const
      -> contracts::Result<RunResult> {
    const auto position = FindPosition(run_id);
    if(position == entries_.end() || position->record.run_id != run_id) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kNotFound,
                           "RunId was not found"});
    }
    if(!position->result) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Run result is not available"});
    }
    return *position->result;
  }

 private:
  friend class RunService;

  struct Entry final {
    RunRecord record;
    std::optional<RunResult> result;
  };

  [[nodiscard]] auto FindPosition(const RunId& run_id)
      -> std::vector<Entry>::iterator {
    return std::lower_bound(
        entries_.begin(),
        entries_.end(),
        run_id,
        [](const Entry& lhs, const RunId& rhs) {
          return lhs.record.run_id < rhs;
        });
  }

  [[nodiscard]] auto FindPosition(const RunId& run_id) const
      -> std::vector<Entry>::const_iterator {
    return std::lower_bound(
        entries_.begin(),
        entries_.end(),
        run_id,
        [](const Entry& lhs, const RunId& rhs) {
          return lhs.record.run_id < rhs;
        });
  }

  std::vector<Entry> entries_;
};

}  // namespace ns3_factory::application
