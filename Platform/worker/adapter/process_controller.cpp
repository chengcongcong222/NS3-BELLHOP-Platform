#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ns3_factory/worker/adapter/process_controller.hpp>
#include <ns3_factory/worker/codec/json_codec.hpp>

namespace ns3_factory::worker::adapter {
namespace {

[[nodiscard]] auto ProcessError(std::string message) -> contracts::Error {
  return contracts::Error{contracts::ErrorCode::kUnavailable,
                          std::move(message)};
}

auto Close(int descriptor) noexcept -> void {
  if(descriptor >= 0) (void)::close(descriptor);
}

auto Wait(pid_t child) -> contracts::Result<int> {
  int status{};
  while(::waitpid(child, &status, 0) < 0) {
    if(errno != EINTR) {
      return std::unexpected(ProcessError("waitpid failed"));
    }
  }
  if(!WIFEXITED(status)) {
    return std::unexpected(
        ProcessError("worker terminated by signal before normal exit"));
  }
  return WEXITSTATUS(status);
}

auto WriteAll(int descriptor, std::string_view text) -> contracts::Status {
  struct sigaction ignore{};
  struct sigaction previous{};
  ignore.sa_handler = SIG_IGN;
  (void)::sigemptyset(&ignore.sa_mask);
  (void)::sigaction(SIGPIPE, &ignore, &previous);
  std::size_t offset{};
  while(offset < text.size()) {
    const auto written =
        ::write(descriptor, text.data() + offset, text.size() - offset);
    if(written < 0) {
      if(errno == EINTR) continue;
      (void)::sigaction(SIGPIPE, &previous, nullptr);
      return std::unexpected(ProcessError("worker stdin pipe write failed"));
    }
    offset += static_cast<std::size_t>(written);
  }
  (void)::sigaction(SIGPIPE, &previous, nullptr);
  return {};
}

auto ReadLine(int descriptor,
              std::string& line,
              bool& eof) -> contracts::Status {
  line.clear();
  eof = false;
  while(true) {
    char character{};
    const auto count = ::read(descriptor, &character, 1U);
    if(count == 0) {
      eof = true;
      if(line.empty()) return {};
      return std::unexpected(
          ProcessError("worker stdout ended in a partial JSON frame"));
    }
    if(count < 0) {
      if(errno == EINTR) continue;
      return std::unexpected(ProcessError("worker stdout pipe read failed"));
    }
    if(character == '\n') return {};
    if(character == '\r') continue;
    if(line.size() == codec::kMaximumOutputMessageBytes) {
      return std::unexpected(
          ProcessError("worker stdout frame exceeds maximum bytes"));
    }
    line.push_back(character);
  }
}

}  // namespace

auto WorkerProcessController::Run(const StartRunCommand& command,
                                  IWorkerEventConsumer& events)
    -> contracts::Result<WorkerProcessResult> {
  if(state_ != WorkerProcessState::kNotStarted) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "WorkerProcessController is one-use"});
  }
  if(executable_.empty() || environment_repository_root_.empty()) {
    state_ = WorkerProcessState::kFailed;
    return std::unexpected(ProcessError(
        "worker executable and environment repository must be explicit"));
  }
  auto encoded = codec::EncodeStartRunCommand(command);
  if(!encoded) {
    state_ = WorkerProcessState::kFailed;
    return std::unexpected(encoded.error());
  }
  encoded->push_back('\n');

  int input_pipe[2]{-1, -1};
  int output_pipe[2]{-1, -1};
  if(::pipe(input_pipe) != 0 || ::pipe(output_pipe) != 0) {
    Close(input_pipe[0]); Close(input_pipe[1]);
    Close(output_pipe[0]); Close(output_pipe[1]);
    state_ = WorkerProcessState::kFailed;
    return std::unexpected(ProcessError("cannot create worker pipes"));
  }
  state_ = WorkerProcessState::kStarting;
  const auto child = ::fork();
  if(child < 0) {
    Close(input_pipe[0]); Close(input_pipe[1]);
    Close(output_pipe[0]); Close(output_pipe[1]);
    state_ = WorkerProcessState::kFailed;
    return std::unexpected(ProcessError("cannot fork worker process"));
  }
  if(child == 0) {
    (void)::dup2(input_pipe[0], STDIN_FILENO);
    (void)::dup2(output_pipe[1], STDOUT_FILENO);
    Close(input_pipe[0]); Close(input_pipe[1]);
    Close(output_pipe[0]); Close(output_pipe[1]);
    const auto executable = executable_.string();
    const auto root = environment_repository_root_.string();
    ::execl(executable.c_str(), executable.c_str(), root.c_str(),
            static_cast<char*>(nullptr));
    ::_exit(127);
  }
  Close(input_pipe[0]);
  Close(output_pipe[1]);
  const auto written = WriteAll(input_pipe[1], *encoded);
  Close(input_pipe[1]);

  std::optional<contracts::Error> stream_error;
  if(!written) stream_error = written.error();
  bool saw_started = false;
  std::uint64_t expected_sequence = 1U;
  std::optional<WorkerCompleted> completed;
  std::optional<WorkerFailed> failed;
  while(!stream_error) {
    std::string line;
    bool eof{};
    const auto read = ReadLine(output_pipe[0], line, eof);
    if(!read) { stream_error = read.error(); break; }
    if(eof) break;
    if(completed || failed) {
      stream_error = ProcessError("worker emitted data after terminal message");
      break;
    }
    auto decoded = codec::DecodeWorkerMessage(line);
    if(!decoded) { stream_error = decoded.error(); break; }
    if(const auto* started = std::get_if<WorkerStarted>(&*decoded)) {
      if(saw_started || started->run_id != command.run_id) {
        stream_error = ProcessError("WorkerStarted is duplicate or mismatched");
        break;
      }
      saw_started = true;
      state_ = WorkerProcessState::kRunning;
    } else if(const auto* event = std::get_if<WorkerRunEvent>(&*decoded)) {
      if(!saw_started || event->record.run_id != command.run_id ||
         event->record.sequence.value() != expected_sequence++) {
        stream_error = ProcessError("worker event order or identity is invalid");
        break;
      }
      const auto consumed = events.OnRunEvent(event->record);
      if(!consumed) stream_error = consumed.error();
    } else if(const auto* terminal = std::get_if<WorkerCompleted>(&*decoded)) {
      if(!saw_started || terminal->run.run_id != command.run_id ||
         terminal->result.run_id != command.run_id) {
        stream_error = ProcessError("WorkerCompleted identity is invalid");
        break;
      }
      completed = *terminal;
    } else {
      const auto& failure = std::get<WorkerFailed>(*decoded);
      if((failure.category != WorkerFailureCategory::kProtocol &&
          !saw_started) ||
         (failure.run_id && *failure.run_id != command.run_id)) {
        stream_error = ProcessError("WorkerFailed identity is invalid");
        break;
      }
      failed = failure;
    }
  }
  Close(output_pipe[0]);
  auto exit = Wait(child);
  if(!exit) stream_error = exit.error();
  if(stream_error) {
    state_ = WorkerProcessState::kFailed;
    return std::unexpected(std::move(*stream_error));
  }
  if(!completed && !failed) {
    state_ = WorkerProcessState::kFailed;
    return std::unexpected(
        ProcessError("worker reached EOF before a terminal message"));
  }
  if((completed && *exit != 0) || (failed && *exit == 0)) {
    state_ = WorkerProcessState::kFailed;
    return std::unexpected(
        ProcessError("worker terminal message and exit status disagree"));
  }
  state_ = completed ? WorkerProcessState::kCompleted
                     : WorkerProcessState::kFailed;
  return WorkerProcessResult{*exit, std::move(completed), std::move(failed)};
}

}  // namespace ns3_factory::worker::adapter
