#pragma once

#include "bellhop_process_runner.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ns3_factory::environment {
namespace {

template <typename T>
[[nodiscard]] auto Failure(contracts::ErrorCode code, std::string message)
    -> contracts::Result<T> {
  return std::unexpected(
      contracts::Error{code, std::move(message)});
}

[[nodiscard]] auto IsSafeCaseName(std::string_view case_name) -> bool {
  return !case_name.empty() &&
         std::all_of(case_name.begin(), case_name.end(), [](char character) {
           const auto value = static_cast<unsigned char>(character);
           return std::isalnum(value) != 0 || character == '_' ||
                  character == '-';
         });
}

[[nodiscard]] auto WriteInputFile(const std::filesystem::path& path,
                                  std::string_view content)
    -> contracts::Status {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  if(!output) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kUnavailable,
        "Unable to create Bellhop input file: " + path.string()});
  }
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  output.close();
  if(!output) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kUnavailable,
        "Unable to write Bellhop input file: " + path.string()});
  }
  return {};
}

#ifdef _WIN32

[[nodiscard]] auto ExecuteProcess(
    const BellhopProcessRunnerConfig& config,
    const std::filesystem::path& working_directory,
    std::string_view case_name) -> contracts::Result<int> {
  std::wstring case_name_wide{case_name.begin(), case_name.end()};
  std::wstring command_line = L"\"" + config.executable_path.wstring() +
                              L"\" \"" + case_name_wide + L"\"";
  std::vector<wchar_t> mutable_command{command_line.begin(),
                                       command_line.end()};
  mutable_command.push_back(L'\0');

  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info{};
  const auto created = CreateProcessW(config.executable_path.c_str(),
                                      mutable_command.data(),
                                      nullptr,
                                      nullptr,
                                      FALSE,
                                      CREATE_NO_WINDOW,
                                      nullptr,
                                      working_directory.c_str(),
                                      &startup_info,
                                      &process_info);
  if(created == FALSE) {
    return Failure<int>(
        contracts::ErrorCode::kUnavailable,
        "Unable to start Bellhop process; Windows error " +
            std::to_string(GetLastError()));
  }

  const auto timeout_count = config.timeout.count();
  const auto maximum_wait =
      static_cast<std::int64_t>(std::numeric_limits<DWORD>::max() - 1U);
  if(timeout_count > maximum_wait) {
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return Failure<int>(contracts::ErrorCode::kOutOfRange,
                        "Bellhop timeout exceeds Windows wait range");
  }
  const auto wait_result = WaitForSingleObject(
      process_info.hProcess, static_cast<DWORD>(timeout_count));
  if(wait_result == WAIT_TIMEOUT) {
    TerminateProcess(process_info.hProcess, 1U);
    WaitForSingleObject(process_info.hProcess, INFINITE);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return Failure<int>(contracts::ErrorCode::kUnavailable,
                        "Bellhop process timed out");
  }
  if(wait_result != WAIT_OBJECT_0) {
    const auto error = GetLastError();
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return Failure<int>(
        contracts::ErrorCode::kUnavailable,
        "Unable to wait for Bellhop process; Windows error " +
            std::to_string(error));
  }

  DWORD exit_code = 0;
  if(GetExitCodeProcess(process_info.hProcess, &exit_code) == FALSE) {
    const auto error = GetLastError();
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return Failure<int>(
        contracts::ErrorCode::kUnavailable,
        "Unable to read Bellhop exit code; Windows error " +
            std::to_string(error));
  }
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  if(exit_code > static_cast<DWORD>(std::numeric_limits<int>::max())) {
    return Failure<int>(contracts::ErrorCode::kOutOfRange,
                        "Bellhop exit code exceeds platform integer range");
  }
  return static_cast<int>(exit_code);
}

#else

[[nodiscard]] auto ExecuteProcess(
    const BellhopProcessRunnerConfig& config,
    const std::filesystem::path& working_directory,
    std::string_view case_name) -> contracts::Result<int> {
  const auto process_id = fork();
  if(process_id < 0) {
    return Failure<int>(contracts::ErrorCode::kUnavailable,
                        "Unable to fork Bellhop process");
  }
  if(process_id == 0) {
    if(chdir(working_directory.c_str()) != 0) {
      _exit(126);
    }
    const std::string case_name_string{case_name};
    const auto executable_name = config.executable_path.filename().string();
    execl(config.executable_path.c_str(),
          executable_name.c_str(),
          case_name_string.c_str(),
          static_cast<char*>(nullptr));
    _exit(127);
  }

  const auto started_at = std::chrono::steady_clock::now();
  for(;;) {
    int status = 0;
    const auto wait_result = waitpid(process_id, &status, WNOHANG);
    if(wait_result == process_id) {
      if(WIFEXITED(status)) {
        return WEXITSTATUS(status);
      }
      return Failure<int>(contracts::ErrorCode::kUnavailable,
                          "Bellhop process terminated without an exit code");
    }
    if(wait_result < 0 && errno != EINTR) {
      return Failure<int>(contracts::ErrorCode::kUnavailable,
                          "Unable to wait for Bellhop process");
    }
    if(std::chrono::steady_clock::now() - started_at >= config.timeout) {
      kill(process_id, SIGKILL);
      waitpid(process_id, nullptr, 0);
      return Failure<int>(contracts::ErrorCode::kUnavailable,
                          "Bellhop process timed out");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
}

#endif

}  // namespace

inline auto BellhopProcessRunner::Create(BellhopProcessRunnerConfig config)
    -> contracts::Result<BellhopProcessRunner> {
  if(config.executable_path.empty() || config.workspace_root.empty() ||
     config.timeout.count() <= 0 || config.maximum_arrivals_bytes == 0) {
    return Failure<BellhopProcessRunner>(
        contracts::ErrorCode::kInvalidArgument,
        "Bellhop runner requires executable, workspace, positive timeout, and ARR size limit");
  }

  std::error_code error;
  auto executable =
      std::filesystem::weakly_canonical(config.executable_path, error);
  if(error || !std::filesystem::is_regular_file(executable, error) || error) {
    return Failure<BellhopProcessRunner>(
        contracts::ErrorCode::kNotFound,
        "Bellhop executable does not exist: " +
            config.executable_path.string());
  }
#ifndef _WIN32
  if(access(executable.c_str(), X_OK) != 0) {
    return Failure<BellhopProcessRunner>(
        contracts::ErrorCode::kFailedPrecondition,
        "Bellhop executable is not executable: " + executable.string());
  }
#endif

  error.clear();
  auto workspace =
      std::filesystem::weakly_canonical(config.workspace_root, error);
  if(error || !std::filesystem::is_directory(workspace, error) || error) {
    return Failure<BellhopProcessRunner>(
        contracts::ErrorCode::kNotFound,
        "Bellhop workspace root does not exist: " +
            config.workspace_root.string());
  }
  config.executable_path = std::move(executable);
  config.workspace_root = std::move(workspace);
  return BellhopProcessRunner{std::move(config)};
}

inline auto BellhopProcessRunner::Run(std::string_view case_name,
                               const BellhopInputFiles& input_files) const
    -> contracts::Result<BellhopRunResult> {
  if(!IsSafeCaseName(case_name)) {
    return Failure<BellhopRunResult>(
        contracts::ErrorCode::kInvalidArgument,
        "Bellhop case name may only contain ASCII letters, digits, underscore, and hyphen");
  }
  if(input_files.environment_ascii.empty() ||
     input_files.bathymetry_ascii.empty() ||
     input_files.surface_ascii.empty()) {
    return Failure<BellhopRunResult>(
        contracts::ErrorCode::kInvalidArgument,
        "Bellhop runner requires ENV, BTY, and ATI input content");
  }

  const std::string case_name_string{case_name};
  const auto case_directory = config_.workspace_root / case_name_string;
  std::error_code error;
  if(!std::filesystem::create_directory(case_directory, error)) {
    return Failure<BellhopRunResult>(
        error ? contracts::ErrorCode::kUnavailable
              : contracts::ErrorCode::kAlreadyExists,
        error ? "Unable to create Bellhop case directory: " + error.message()
              : "Bellhop case directory already exists");
  }

  const auto write_environment = WriteInputFile(
      case_directory / (case_name_string + ".env"),
      input_files.environment_ascii);
  if(!write_environment) {
    return std::unexpected(write_environment.error());
  }
  const auto write_bathymetry = WriteInputFile(
      case_directory / (case_name_string + ".bty"),
      input_files.bathymetry_ascii);
  if(!write_bathymetry) {
    return std::unexpected(write_bathymetry.error());
  }
  const auto write_surface = WriteInputFile(
      case_directory / (case_name_string + ".ati"),
      input_files.surface_ascii);
  if(!write_surface) {
    return std::unexpected(write_surface.error());
  }

  const auto exit_code = ExecuteProcess(config_, case_directory, case_name);
  if(!exit_code) {
    return std::unexpected(exit_code.error());
  }
  if(*exit_code != 0) {
    return Failure<BellhopRunResult>(
        contracts::ErrorCode::kUnavailable,
        "Bellhop process exited with code " + std::to_string(*exit_code));
  }

  const auto arrivals_path =
      case_directory / (case_name_string + ".arr");
  error.clear();
  const auto arrivals_size = std::filesystem::file_size(arrivals_path, error);
  if(error) {
    return Failure<BellhopRunResult>(
        contracts::ErrorCode::kNotFound,
        "Bellhop did not produce a readable ARR file");
  }
  if(arrivals_size == 0 || arrivals_size > config_.maximum_arrivals_bytes ||
     arrivals_size > std::numeric_limits<std::size_t>::max()) {
    return Failure<BellhopRunResult>(
        contracts::ErrorCode::kOutOfRange,
        "Bellhop ARR file is empty or exceeds the configured size limit");
  }

  std::ifstream arrivals_file{arrivals_path, std::ios::binary};
  if(!arrivals_file) {
    return Failure<BellhopRunResult>(
        contracts::ErrorCode::kUnavailable,
        "Unable to open Bellhop ARR file");
  }
  std::string arrivals_ascii(static_cast<std::size_t>(arrivals_size), '\0');
  arrivals_file.read(arrivals_ascii.data(),
                     static_cast<std::streamsize>(arrivals_ascii.size()));
  if(!arrivals_file) {
    return Failure<BellhopRunResult>(
        contracts::ErrorCode::kUnavailable,
        "Unable to read complete Bellhop ARR file");
  }
  return BellhopRunResult{
      std::move(arrivals_ascii),
      "bellhop-process:" + config_.executable_path.string()};
}

}  // namespace ns3_factory::environment
