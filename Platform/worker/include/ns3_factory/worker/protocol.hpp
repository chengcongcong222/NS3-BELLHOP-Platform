#pragma once

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
  application::RunId run_id;
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
