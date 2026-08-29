#pragma once

#include <filesystem>
#include <functional>
#include <optional>

#include <ns3_factory/worker/protocol.hpp>

namespace ns3_factory::worker::adapter {

enum class WorkerProcessState : std::uint8_t {
  kNotStarted = 1,
  kStarting = 2,
  kRunning = 3,
  kCompleted = 4,
  kFailed = 5,
};

class IWorkerEventConsumer {
 public:
  virtual ~IWorkerEventConsumer() = default;
  [[nodiscard]] virtual auto OnRunEvent(
      const application::RunEventRecord& event) noexcept
      -> contracts::Status = 0;
};

class NullWorkerEventConsumer final : public IWorkerEventConsumer {
 public:
  [[nodiscard]] auto OnRunEvent(
      const application::RunEventRecord&) noexcept
      -> contracts::Status override {
    return {};
  }
};

struct WorkerProcessResult final {
  int exit_code;
  std::optional<WorkerCompleted> completed;
  std::optional<WorkerFailed> failed;
};

class WorkerProcessController final {
 public:
  WorkerProcessController(std::filesystem::path executable,
                          std::filesystem::path environment_repository_root)
      : executable_(std::move(executable)),
        environment_repository_root_(
            std::move(environment_repository_root)) {}

  [[nodiscard]] auto state() const noexcept -> WorkerProcessState {
    return state_;
  }

  [[nodiscard]] auto Run(const StartRunCommand& command,
                         IWorkerEventConsumer& events)
      -> contracts::Result<WorkerProcessResult>;

 private:
  std::filesystem::path executable_;
  std::filesystem::path environment_repository_root_;
  WorkerProcessState state_{WorkerProcessState::kNotStarted};
};

}  // namespace ns3_factory::worker::adapter
