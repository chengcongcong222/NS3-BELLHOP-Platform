#include <concepts>
#include <cstdlib>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <ns3_factory/application/domain.hpp>
#include <ns3_factory/application/repositories.hpp>
#include <ns3_factory/application/run_service.hpp>

namespace {

using namespace ns3_factory::application;
using namespace ns3_factory::contracts;

static_assert(!std::same_as<ScenarioId, ExperimentId>);
static_assert(!std::same_as<ScenarioId, RunId>);
static_assert(!std::same_as<ExperimentId, RunId>);
static_assert(!std::constructible_from<ScenarioId, std::string>);
static_assert(!std::constructible_from<ExperimentId, std::string>);
static_assert(!std::constructible_from<RunId, std::string>);

template <typename Repository>
concept PublicRunReplace = requires(Repository repository,
                                    RunRecord record) {
  repository.Replace(std::move(record));
};

template <typename Repository>
concept PublicRunComplete = requires(Repository repository,
                                     RunRecord record,
                                     RunResult result) {
  repository.Complete(std::move(record), std::move(result));
};

static_assert(!PublicRunReplace<RunRepository>);
static_assert(!PublicRunComplete<RunRepository>);

template <typename T>
concept ExposesRuntimeInternal = requires(T value) {
  value.world_snapshot;
} || requires(T value) {
  value.cycle_working_state;
} || requires(T value) {
  value.protocol_cycle_plan;
} || requires(T value) {
  value.transmission_record_store;
} || requires(T value) {
  value.scenario_runtime;
};

static_assert(!ExposesRuntimeInternal<ScenarioDefinition>);
static_assert(!ExposesRuntimeInternal<ExperimentDefinition>);
static_assert(!ExposesRuntimeInternal<RunRecord>);
static_assert(!ExposesRuntimeInternal<RunResult>);

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

auto MakeScenario(std::string id = "scenario-1",
                  std::string asset_id = "asset-1",
                  ResourceVersion version = 1)
    -> Result<ScenarioDefinition> {
  auto scenario_id = ScenarioId::Create(std::move(id));
  if(!scenario_id) return std::unexpected(scenario_id.error());
  return ScenarioDefinition::Create(
      std::move(*scenario_id),
      version,
      "Scenario",
      {Node(1), Node(2), Node(9, true)},
      EnvironmentReference{std::move(asset_id), 1},
      MobilityModel::kConstantVelocity,
      NodeId{9});
}

auto MakeExperiment(const ScenarioDefinition& scenario,
                    std::string id = "experiment-1",
                    ResourceVersion version = 1,
                    std::size_t simulation_cycle_count = 2)
    -> Result<ExperimentDefinition> {
  auto experiment_id = ExperimentId::Create(std::move(id));
  if(!experiment_id) return std::unexpected(experiment_id.error());
  return ExperimentDefinition::Create(
      std::move(*experiment_id),
      version,
      "Experiment",
      ScenarioReference{scenario.scenario_id(), scenario.version()},
      RoutingConfiguration{RoutingMode::kDirectToFusionCenter},
      MacConfiguration{MacMode::kTdma,
                       SimDuration::FromNanoseconds(4'000'000'000),
                       SimDuration::FromNanoseconds(2'000'000'000)},
      PhyConfiguration{60, 25'000.0, 4'000.0, 110.0, 45.0,
                       RxQualityMode::kModeledBpskAwgn},
      FusionConfiguration{ApplicationWorkload::kAcceptanceBearingFusion,
                          AcceptanceProfile::kAcceptance4Node,
                          5,
                          SimDuration::FromNanoseconds(180'000'000'000),
                          200.0,
                          150.0,
                          1.0e-4},
      10,
      simulation_cycle_count,
      42);
}

class FakeRunExecutor final : public IRunExecutor {
 public:
  auto Execute(const RunId& run_id,
               const ScenarioDefinition& scenario,
               const ExperimentDefinition& experiment) const
      -> Result<RunResult> override {
    ++execute_count;
    if(failure) return std::unexpected(*failure);
    const auto ended_at = SimTime::FromNanoseconds(
        static_cast<std::int64_t>(experiment.simulation_cycle_count()) *
        10'000'000'000);
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
                             ended_at,
                             SimDuration::FromNanoseconds(
                                 ended_at.nanoseconds()),
                             SnapshotVersion{
                                 experiment.simulation_cycle_count()},
                             experiment.simulation_cycle_count(),
                             scenario.nodes().size(),
                             2,
                             4,
                             0,
                             4,
                             2},
        std::nullopt,
        {},
        std::move(nodes)};
  }

  mutable std::size_t execute_count{0};
  std::optional<Error> failure;
};

auto TestIdAndDefinitionValidation() -> bool {
  const auto valid = ScenarioId::Create("scenario.valid-1");
  const auto empty = ScenarioId::Create("");
  const auto path = ScenarioId::Create("folder/scenario");
  const auto leading = ScenarioId::Create("_scenario");
  auto scenario = MakeScenario();
  const auto invalid_asset = MakeScenario("scenario-asset", "../asset");
  if(!scenario) return false;
  auto duplicate_nodes = std::vector<NodeCommittedState>{Node(1), Node(1)};
  const auto invalid_scenario = ScenarioDefinition::Create(
      scenario->scenario_id(),
      2,
      "Invalid",
      std::move(duplicate_nodes),
      EnvironmentReference{"asset-1", 1},
      MobilityModel::kConstantVelocity,
      NodeId{1});
  return valid && !empty && !path && !leading && !invalid_asset &&
         !invalid_scenario &&
         invalid_scenario.error().code == ErrorCode::kAlreadyExists &&
         scenario->nodes().size() == 3 &&
         scenario->nodes().front().node_id == NodeId{1};
}

auto TestRepositoriesAndLifecycle() -> bool {
  auto scenario = MakeScenario();
  if(!scenario) return false;
  auto experiment = MakeExperiment(*scenario);
  if(!experiment) return false;
  ScenarioRepository scenarios;
  ExperimentRepository experiments;
  RunRepository runs;
  if(!scenarios.Register(*scenario) || scenarios.Register(*scenario) ||
     !experiments.Register(*experiment) ||
     experiments.Register(*experiment)) {
    return false;
  }
  auto missing_scenario_id = ScenarioId::Create("missing");
  auto missing_experiment_id = ExperimentId::Create("missing");
  if(!missing_scenario_id || !missing_experiment_id ||
     scenarios.Find(ScenarioReference{*missing_scenario_id, 1}) ||
     experiments.Find(ExperimentReference{*missing_experiment_id, 1})) {
    return false;
  }
  FakeRunExecutor executor;
  RunService service{scenarios, experiments, runs, executor};
  auto run_id = RunId::Create("run-1");
  auto missing_experiment_run_id = RunId::Create("run-missing-experiment");
  if(!run_id || !missing_experiment_run_id) return false;
  const auto missing_experiment_run = service.CreateRun(
      *missing_experiment_run_id,
      ExperimentReference{*missing_experiment_id, 1});
  if(missing_experiment_run ||
     missing_experiment_run.error().code != ErrorCode::kNotFound ||
     !missing_experiment_run.error().message.starts_with("Experiment")) {
    return false;
  }
  const auto reference = ExperimentReference{
      experiment->experiment_id(), experiment->version()};
  const auto created = service.CreateRun(*run_id, reference);
  const auto duplicate = service.CreateRun(*run_id, reference);
  const auto before_result = service.GetResult(*run_id);
  const auto result = service.ExecuteRun(*run_id);
  const auto record = service.GetRun(*run_id);
  const auto stored_result = service.GetResult(*run_id);
  const auto repeated = service.ExecuteRun(*run_id);
  return created && !duplicate && !before_result && result && record &&
         stored_result && !repeated && executor.execute_count == 1 &&
         duplicate.error().code == ErrorCode::kAlreadyExists &&
         before_result.error().code == ErrorCode::kFailedPrecondition &&
         repeated.error().code == ErrorCode::kFailedPrecondition &&
         record->lifecycle == RunLifecycle::kCompleted &&
         record->simulation_started_at == SimTime::Zero() &&
         record->simulation_ended_at ==
             SimTime::FromNanoseconds(20'000'000'000) &&
         record->final_snapshot_version == SnapshotVersion{2} &&
         !record->failure && *stored_result == *result;
}

auto TestMissingScenarioIsDistinguished() -> bool {
  auto scenario = MakeScenario();
  if(!scenario) return false;
  auto missing_scenario_id = ScenarioId::Create("missing-scenario");
  auto experiment_id = ExperimentId::Create("orphan-experiment");
  auto run_id = RunId::Create("run-missing-scenario");
  if(!missing_scenario_id || !experiment_id || !run_id) return false;
  auto experiment = ExperimentDefinition::Create(
      std::move(*experiment_id),
      1,
      "Orphan Experiment",
      ScenarioReference{*missing_scenario_id, 1},
      RoutingConfiguration{RoutingMode::kDirectToFusionCenter},
      MacConfiguration{MacMode::kTdma,
                       SimDuration::FromNanoseconds(4'000'000'000),
                       SimDuration::FromNanoseconds(2'000'000'000)},
      PhyConfiguration{60, 25'000.0, 4'000.0, 110.0, 45.0,
                       RxQualityMode::kModeledBpskAwgn},
      FusionConfiguration{ApplicationWorkload::kAcceptanceBearingFusion,
                          AcceptanceProfile::kAcceptance4Node,
                          5,
                          SimDuration::FromNanoseconds(180'000'000'000),
                          200.0,
                          150.0,
                          1.0e-4},
      10,
      2,
      42);
  if(!experiment) return false;
  ScenarioRepository scenarios;
  ExperimentRepository experiments;
  RunRepository runs;
  if(!experiments.Register(*experiment)) return false;
  FakeRunExecutor executor;
  RunService service{scenarios, experiments, runs, executor};
  const auto created = service.CreateRun(
      *run_id,
      ExperimentReference{experiment->experiment_id(),
                          experiment->version()});
  return !created && created.error().code == ErrorCode::kNotFound &&
         created.error().message.starts_with("Scenario") &&
         !service.GetRun(*run_id);
}

auto TestRunIdDoesNotAffectDeterminism() -> bool {
  auto scenario = MakeScenario();
  if(!scenario) return false;
  auto experiment = MakeExperiment(*scenario);
  if(!experiment) return false;
  ScenarioRepository scenarios;
  ExperimentRepository experiments;
  RunRepository runs;
  if(!scenarios.Register(*scenario) || !experiments.Register(*experiment)) {
    return false;
  }
  FakeRunExecutor executor;
  RunService service{scenarios, experiments, runs, executor};
  auto first_id = RunId::Create("run-a");
  auto second_id = RunId::Create("run-b");
  if(!first_id || !second_id) return false;
  const auto reference = ExperimentReference{
      experiment->experiment_id(), experiment->version()};
  if(!service.CreateRun(*first_id, reference) ||
     !service.CreateRun(*second_id, reference)) {
    return false;
  }
  const auto first = service.ExecuteRun(*first_id);
  const auto second = service.ExecuteRun(*second_id);
  return first && second && first->run_id != second->run_id &&
         first->projection == second->projection &&
         first->acceptance_report == second->acceptance_report &&
         first->fusion_results == second->fusion_results &&
         first->nodes == second->nodes;
}

auto TestRunCapturesExactDefinitionVersions() -> bool {
  auto scenario_v1 = MakeScenario();
  auto scenario_v2 = MakeScenario("scenario-1", "asset-1", 2);
  if(!scenario_v1 || !scenario_v2) return false;
  auto experiment_v1 = MakeExperiment(*scenario_v1);
  auto experiment_v2 = MakeExperiment(*scenario_v2, "experiment-1", 2, 3);
  if(!experiment_v1 || !experiment_v2) return false;
  ScenarioRepository scenarios;
  ExperimentRepository experiments;
  RunRepository runs;
  if(!scenarios.Register(*scenario_v1) ||
     !experiments.Register(*experiment_v1)) {
    return false;
  }
  FakeRunExecutor executor;
  RunService service{scenarios, experiments, runs, executor};
  auto run_id = RunId::Create("run-captured-v1");
  if(!run_id) return false;
  const auto v1_reference = ExperimentReference{
      experiment_v1->experiment_id(), experiment_v1->version()};
  const auto created = service.CreateRun(*run_id, v1_reference);
  if(!created || created->experiment.experiment_version != 1U ||
     created->scenario.scenario_version != 1U ||
     created->environment != scenario_v1->environment()) {
    return false;
  }
  if(!scenarios.Register(*scenario_v2) ||
     !experiments.Register(*experiment_v2)) {
    return false;
  }
  const auto result = service.ExecuteRun(*run_id);
  const auto record = service.GetRun(*run_id);
  return result && record &&
         result->projection.cycle_count == 2U &&
         result->projection.simulation_ended_at ==
             SimTime::FromNanoseconds(20'000'000'000) &&
         record->experiment == v1_reference &&
         record->scenario.scenario_version == 1U &&
         record->environment == scenario_v1->environment();
}

auto TestFailedRunIsTerminalAndAuditable() -> bool {
  auto scenario = MakeScenario();
  if(!scenario) return false;
  auto experiment = MakeExperiment(*scenario);
  if(!experiment) return false;
  ScenarioRepository scenarios;
  ExperimentRepository experiments;
  RunRepository runs;
  if(!scenarios.Register(*scenario) || !experiments.Register(*experiment)) {
    return false;
  }
  FakeRunExecutor executor;
  executor.failure =
      Error{ErrorCode::kNotFound, "Environment asset missing"};
  RunService service{scenarios, experiments, runs, executor};
  auto run_id = RunId::Create("run-failed");
  if(!run_id) return false;
  const auto reference = ExperimentReference{
      experiment->experiment_id(), experiment->version()};
  if(!service.CreateRun(*run_id, reference)) return false;
  const auto executed = service.ExecuteRun(*run_id);
  const auto record = service.GetRun(*run_id);
  const auto repeated = service.ExecuteRun(*run_id);
  const auto result = service.GetResult(*run_id);
  return !executed && record && !repeated && !result &&
         executed.error().code == ErrorCode::kNotFound &&
         record->lifecycle == RunLifecycle::kFailed && record->failure &&
         record->failure->code == ErrorCode::kNotFound &&
         record->failure->message == "Environment asset missing" &&
         repeated.error().code == ErrorCode::kFailedPrecondition;
}

}  // namespace

auto main() -> int {
  return TestIdAndDefinitionValidation() &&
                 TestRepositoriesAndLifecycle() &&
                 TestMissingScenarioIsDistinguished() &&
                 TestRunIdDoesNotAffectDeterminism() &&
                 TestRunCapturesExactDefinitionVersions() &&
                 TestFailedRunIsTerminalAndAuditable()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
