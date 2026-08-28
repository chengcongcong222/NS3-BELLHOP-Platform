#include <filesystem>
#include <iostream>
#include <string_view>
#include <utility>

#include <ns3_factory/worker/protocol.hpp>

#include "internal/environment_asset_repository.hpp"
#include "internal/simulation_worker.hpp"

namespace {

using namespace ns3_factory;

auto ProtocolFailure(std::string_view message) -> int {
  std::cerr << "platform_sim_worker protocol failure: " << message << '\n';
  return static_cast<int>(worker::WorkerExitCode::kProtocolFailure);
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if(argc != 5) {
    return ProtocolFailure(
        "expected <repository-root> <environment-asset-id> <run-id> "
        "<pass|verdict-fail>");
  }
  const auto mode = std::string_view{argv[4]};
  if(mode != "pass" && mode != "verdict-fail") {
    return ProtocolFailure("unsupported worker mode");
  }
  auto environments =
      environment::internal::EnvironmentAssetRepository::Open(
          std::filesystem::path{argv[1]});
  auto run_id = application::RunId::Create(argv[3]);
  auto scenario_id =
      application::ScenarioId::Create("worker-acceptance4-scenario");
  auto experiment_id =
      application::ExperimentId::Create("worker-acceptance4-experiment");
  if(!environments || !run_id || !scenario_id || !experiment_id) {
    return ProtocolFailure("worker input validation failed");
  }
  worker::internal::AcceptanceWorkerRequest request{
      std::move(*run_id),
      std::move(*scenario_id),
      std::move(*experiment_id),
      1,
      application::EnvironmentReference{argv[2], 1},
      application::AcceptanceProfile::kAcceptance4Node,
      2,
      application::RxQualityMode::kModeledBpskAwgn,
      mode == "verdict-fail" ? 180.0 : 45.0,
      0};
  worker::NullWorkerMessageSink output;
  worker::internal::SimulationWorker simulation_worker{*environments};
  const auto executed = simulation_worker.Run(request, output);
  if(!executed) {
    std::cerr << "platform_sim_worker execution failure: "
              << executed.error().message << '\n';
    return static_cast<int>(worker::WorkerExitCode::kExecutionFailure);
  }
  return static_cast<int>(worker::WorkerExitCode::kCompleted);
}
