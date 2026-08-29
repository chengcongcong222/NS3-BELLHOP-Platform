#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include <ns3_factory/worker/codec/json_codec.hpp>
#include <ns3_factory/worker/protocol.hpp>

#include "adapter/json_line_message_sink.hpp"
#include "internal/environment_asset_repository.hpp"
#include "internal/simulation_worker.hpp"

namespace {

using namespace ns3_factory;

auto ProtocolFailure(std::string_view message) -> int {
  std::cerr << "platform_sim_worker protocol failure: " << message << '\n';
  return static_cast<int>(worker::WorkerExitCode::kProtocolFailure);
}

auto ReadBoundedCommandLine() -> contracts::Result<std::string> {
  std::string line;
  line.reserve(4096U);
  char character{};
  while(std::cin.get(character)) {
    if(character == '\n') return line;
    if(character == '\r') continue;
    if(line.size() == worker::codec::kMaximumInputLineBytes) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "StartRunCommand exceeds maximum line bytes"});
    }
    line.push_back(character);
  }
  if(!line.empty()) return line;
  return std::unexpected(
      contracts::Error{contracts::ErrorCode::kInvalidArgument,
                       "StartRunCommand input ended before one frame"});
}

auto EmitProtocolFailure(const contracts::Error& error) -> int {
  worker::adapter::JsonLineWorkerMessageSink output{std::cout};
  (void)output.Emit(worker::WorkerFailed{
      std::nullopt,
      worker::WorkerFailureCategory::kProtocol,
      error,
      std::nullopt});
  return ProtocolFailure(error.message);
}

auto RunWireWorker(const std::filesystem::path& repository_root) -> int {
  auto line = ReadBoundedCommandLine();
  if(!line) return EmitProtocolFailure(line.error());
  auto command = worker::codec::DecodeStartRunCommand(*line);
  if(!command) return EmitProtocolFailure(command.error());
  auto asset_id = environment::internal::EnvironmentAssetId::Create(
      command->environment.asset_id);
  if(!asset_id) return EmitProtocolFailure(asset_id.error());
  auto environments =
      environment::internal::EnvironmentAssetRepository::Open(
          repository_root);
  if(!environments) {
    worker::adapter::JsonLineWorkerMessageSink output{std::cout};
    const auto emitted = output.Emit(worker::WorkerFailed{
        command->run_id,
        worker::WorkerFailureCategory::kComposition,
        environments.error(),
        std::nullopt});
    if(!emitted) return ProtocolFailure(emitted.error().message);
    return static_cast<int>(worker::WorkerExitCode::kExecutionFailure);
  }
  worker::internal::AcceptanceWorkerRequest request{
      command->run_id,
      command->scenario_id,
      command->experiment_id,
      command->definition_version,
      command->environment,
      command->profile,
      command->simulation_cycle_count,
      command->quality_mode,
      command->equivalent_noise_power_db_re_1upa2,
      command->deterministic_seed};
  worker::adapter::JsonLineWorkerMessageSink output{std::cout};
  worker::internal::SimulationWorker simulation_worker{*environments};
  const auto executed = simulation_worker.Run(request, output);
  if(!executed) {
    std::cerr << "platform_sim_worker execution/bridge failure: "
              << executed.error().message << '\n';
    return static_cast<int>(worker::WorkerExitCode::kExecutionFailure);
  }
  return static_cast<int>(worker::WorkerExitCode::kCompleted);
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if(argc == 2) {
    return RunWireWorker(std::filesystem::path{argv[1]});
  }
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
