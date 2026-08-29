#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include <ns3_factory/application/result.hpp>
#include <ns3_factory/application/run_events.hpp>

namespace ns3_factory::worker {

enum class WorkerExitCode : int {
  kCompleted = 0,
  kProtocolFailure = 2,
  kExecutionFailure = 3,
};

enum class WorkerFailureCategory : std::uint8_t {
  kProtocol = 1,
  kComposition = 2,
  kSimulation = 3,
};

struct StartRunCommand final {
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

  auto operator==(const StartRunCommand&) const -> bool = default;
};

struct WorkerStarted final {
  application::RunId run_id;

  auto operator==(const WorkerStarted&) const -> bool = default;
};

struct WorkerRunEvent final {
  application::RunEventRecord record;

  auto operator==(const WorkerRunEvent&) const -> bool = default;
};

struct WorkerCompleted final {
  application::RunRecord run;
  application::RunResult result;

  auto operator==(const WorkerCompleted&) const -> bool = default;
};

struct WorkerFailed final {
  std::optional<application::RunId> run_id;
  WorkerFailureCategory category;
  contracts::Error error;
  std::optional<application::RunRecord> run;

  auto operator==(const WorkerFailed&) const -> bool = default;
};

using WorkerMessage =
    std::variant<WorkerStarted,
                 WorkerRunEvent,
                 WorkerCompleted,
                 WorkerFailed>;

class IWorkerMessageSink {
 public:
  virtual ~IWorkerMessageSink() = default;

  [[nodiscard]] virtual auto Emit(const WorkerMessage& message) noexcept
      -> contracts::Status = 0;
};

class NullWorkerMessageSink final : public IWorkerMessageSink {
 public:
  [[nodiscard]] auto Emit(const WorkerMessage&) noexcept
      -> contracts::Status override {
    return {};
  }
};

}  // namespace ns3_factory::worker
