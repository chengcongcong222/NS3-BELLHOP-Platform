#pragma once

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "environment_asset_repository.hpp"
#include "offline_asset_pipeline.hpp"
#include "woss_cache_loader.hpp"

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

struct RawEnvironmentImportRequest final {
  std::filesystem::path woa_temperature_path;
  std::filesystem::path woa_salinity_path;
  double latitude_degrees;
  double longitude_degrees;
  double depth_limit_meters;
  double transect_bearing_degrees;
  double maximum_range_meters;
  std::size_t bathymetry_sample_count;
  std::string asset_id;
  std::string asset_name;
  std::string time_descriptor;
  std::string woa_dataset;
  std::string gebco_dataset;
  std::filesystem::path output_root;
  std::optional<std::filesystem::path> gebco_response_path;
  bool allow_gebco_fetch;
  bool overwrite_outputs;
};

struct RawEnvironmentImportResult final {
  std::filesystem::path asset_root;
  std::filesystem::path manifest_path;
};

class IRawEnvironmentImporter {
 public:
  virtual ~IRawEnvironmentImporter() = default;

  [[nodiscard]] virtual auto Import(
      const RawEnvironmentImportRequest& request) const
      -> contracts::Result<RawEnvironmentImportResult> = 0;
};

struct PythonRawEnvironmentImporterConfig final {
  std::filesystem::path python_executable;
  std::filesystem::path importer_script;
  std::chrono::milliseconds timeout;
};

namespace raw_detail {

template <typename T>
[[nodiscard]] inline auto Failure(contracts::ErrorCode code,
                                  std::string message)
    -> contracts::Result<T> {
  return std::unexpected(contracts::Error{code, std::move(message)});
}

[[nodiscard]] inline auto IsSafeAssetId(std::string_view asset_id) -> bool {
  if(asset_id.empty() || asset_id.size() > 128U) {
    return false;
  }
  const auto is_ascii_alphanumeric = [](char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9');
  };
  if(!is_ascii_alphanumeric(asset_id.front())) {
    return false;
  }
  return std::ranges::all_of(asset_id, [&](char character) {
    return is_ascii_alphanumeric(character) || character == '.' ||
           character == '_' || character == '-';
  });
}

[[nodiscard]] inline auto PathUtf8(const std::filesystem::path& path)
    -> std::string {
  const auto value = path.u8string();
  return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] inline auto ValidateImportRequest(
    const RawEnvironmentImportRequest& request) -> contracts::Status {
  const auto contains_nul = [](std::string_view value) {
    return value.find('\0') != std::string_view::npos;
  };
  if(request.woa_temperature_path.empty() ||
     request.woa_salinity_path.empty() || request.output_root.empty() ||
     !IsSafeAssetId(request.asset_id) || request.asset_name.empty() ||
     request.time_descriptor.empty() || request.woa_dataset.empty() ||
     request.gebco_dataset.empty() || contains_nul(request.asset_name) ||
     contains_nul(request.time_descriptor) ||
     contains_nul(request.woa_dataset) ||
     contains_nul(request.gebco_dataset)) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "Raw import requires WOA inputs, output root, safe asset metadata, "
        "and dataset identities"});
  }
  if(!std::isfinite(request.latitude_degrees) ||
     !std::isfinite(request.longitude_degrees) ||
     !std::isfinite(request.depth_limit_meters) ||
     !std::isfinite(request.transect_bearing_degrees) ||
     !std::isfinite(request.maximum_range_meters)) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "Raw import coordinates and physical limits must be finite"});
  }
  if(request.latitude_degrees < -90.0 ||
     request.latitude_degrees > 90.0 ||
     request.longitude_degrees < -180.0 ||
     request.longitude_degrees > 180.0 ||
     request.depth_limit_meters <= 0.0 ||
     request.maximum_range_meters <= 0.0 ||
     request.bathymetry_sample_count < 2U ||
     request.bathymetry_sample_count > 100U) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kOutOfRange,
        "Raw import coordinates, limits, or GEBCO sample count are outside "
        "supported bounds"});
  }
  if(request.gebco_response_path.has_value() ==
     request.allow_gebco_fetch) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "Raw import requires exactly one GEBCO source: a recorded response "
        "or explicit network permission"});
  }
  return {};
}

[[nodiscard]] inline auto FormatDouble(double value) -> std::string {
  char buffer[64]{};
  const auto [end, error] = std::to_chars(
      buffer,
      buffer + sizeof(buffer),
      value,
      std::chars_format::general,
      std::numeric_limits<double>::max_digits10);
  if(error != std::errc{}) {
    return {};
  }
  return {buffer, end};
}

#ifdef _WIN32

[[nodiscard]] inline auto Utf8ToWide(std::string_view value)
    -> contracts::Result<std::wstring> {
  if(value.empty()) {
    return std::wstring{};
  }
  if(value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return Failure<std::wstring>(contracts::ErrorCode::kOutOfRange,
                                 "Python process argument is too large");
  }
  const auto required = MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      value.data(),
      static_cast<int>(value.size()),
      nullptr,
      0);
  if(required <= 0) {
    return Failure<std::wstring>(contracts::ErrorCode::kInvalidArgument,
                                 "Python process argument is not valid UTF-8");
  }
  std::wstring result(static_cast<std::size_t>(required), L'\0');
  const auto converted = MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      value.data(),
      static_cast<int>(value.size()),
      result.data(),
      required);
  if(converted != required) {
    return Failure<std::wstring>(contracts::ErrorCode::kUnavailable,
                                 "Unable to convert Python process argument");
  }
  return result;
}

[[nodiscard]] inline auto QuoteWindowsArgument(std::wstring_view value)
    -> std::wstring {
  std::wstring result{L"\""};
  std::size_t backslashes = 0U;
  for(const auto character : value) {
    if(character == L'\\') {
      ++backslashes;
      continue;
    }
    if(character == L'\"') {
      result.append(backslashes * 2U + 1U, L'\\');
      result.push_back(L'\"');
    } else {
      result.append(backslashes, L'\\');
      result.push_back(character);
    }
    backslashes = 0U;
  }
  result.append(backslashes * 2U, L'\\');
  result.push_back(L'\"');
  return result;
}

[[nodiscard]] inline auto ExecutePython(
    const PythonRawEnvironmentImporterConfig& config,
    const std::vector<std::string>& arguments) -> contracts::Status {
  std::wstring command_line =
      QuoteWindowsArgument(config.python_executable.wstring());
  for(const auto& argument : arguments) {
    auto wide = Utf8ToWide(argument);
    if(!wide) {
      return std::unexpected(wide.error());
    }
    command_line.push_back(L' ');
    command_line += QuoteWindowsArgument(*wide);
  }
  std::vector<wchar_t> mutable_command{command_line.begin(),
                                       command_line.end()};
  mutable_command.push_back(L'\0');

  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info{};
  const auto created = CreateProcessW(config.python_executable.c_str(),
                                      mutable_command.data(),
                                      nullptr,
                                      nullptr,
                                      FALSE,
                                      CREATE_NO_WINDOW,
                                      nullptr,
                                      nullptr,
                                      &startup_info,
                                      &process_info);
  if(created == FALSE) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kUnavailable,
        "Unable to start raw environment importer; Windows error " +
            std::to_string(GetLastError())});
  }
  const auto timeout_count = config.timeout.count();
  constexpr auto kMaximumWait =
      static_cast<std::int64_t>(std::numeric_limits<DWORD>::max() - 1U);
  if(timeout_count > kMaximumWait) {
    TerminateProcess(process_info.hProcess, 1U);
    WaitForSingleObject(process_info.hProcess, INFINITE);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kOutOfRange,
        "Raw environment importer timeout exceeds Windows wait range"});
  }
  const auto wait_result = WaitForSingleObject(
      process_info.hProcess, static_cast<DWORD>(timeout_count));
  if(wait_result == WAIT_TIMEOUT) {
    TerminateProcess(process_info.hProcess, 1U);
    WaitForSingleObject(process_info.hProcess, INFINITE);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kUnavailable,
        "Raw environment importer timed out"});
  }
  if(wait_result != WAIT_OBJECT_0) {
    const auto error = GetLastError();
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kUnavailable,
        "Unable to wait for raw environment importer; Windows error " +
            std::to_string(error)});
  }
  DWORD exit_code = 0U;
  if(GetExitCodeProcess(process_info.hProcess, &exit_code) == FALSE) {
    const auto error = GetLastError();
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kUnavailable,
        "Unable to read raw environment importer exit code; Windows error " +
            std::to_string(error)});
  }
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  if(exit_code != 0U) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kUnavailable,
        "Raw environment importer exited with code " +
            std::to_string(exit_code)});
  }
  return {};
}

#else

[[nodiscard]] inline auto ExecutePython(
    const PythonRawEnvironmentImporterConfig& config,
    const std::vector<std::string>& arguments) -> contracts::Status {
  const auto process_id = fork();
  if(process_id < 0) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kUnavailable,
        "Unable to fork raw environment importer"});
  }
  if(process_id == 0) {
    std::vector<std::string> process_arguments;
    process_arguments.reserve(arguments.size() + 1U);
    process_arguments.push_back(PathUtf8(config.python_executable));
    process_arguments.insert(process_arguments.end(),
                             arguments.begin(),
                             arguments.end());
    std::vector<char*> argument_pointers;
    argument_pointers.reserve(process_arguments.size() + 1U);
    for(auto& argument : process_arguments) {
      argument_pointers.push_back(argument.data());
    }
    argument_pointers.push_back(nullptr);
    execv(config.python_executable.c_str(), argument_pointers.data());
    _exit(127);
  }

  const auto started_at = std::chrono::steady_clock::now();
  for(;;) {
    int status = 0;
    const auto wait_result = waitpid(process_id, &status, WNOHANG);
    if(wait_result == process_id) {
      if(WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return {};
      }
      const auto exit_description =
          WIFEXITED(status)
              ? " with code " + std::to_string(WEXITSTATUS(status))
              : std::string{" without a normal exit code"};
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kUnavailable,
          "Raw environment importer terminated" + exit_description});
    }
    if(wait_result < 0 && errno != EINTR) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kUnavailable,
          "Unable to wait for raw environment importer"});
    }
    if(std::chrono::steady_clock::now() - started_at >= config.timeout) {
      kill(process_id, SIGKILL);
      waitpid(process_id, nullptr, 0);
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kUnavailable,
          "Raw environment importer timed out"});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
}

#endif

}  // namespace raw_detail

class PythonRawEnvironmentImporter final : public IRawEnvironmentImporter {
 public:
  [[nodiscard]] static auto Create(PythonRawEnvironmentImporterConfig config)
      -> contracts::Result<PythonRawEnvironmentImporter> {
    if(config.python_executable.empty() || config.importer_script.empty() ||
       config.timeout.count() <= 0) {
      return raw_detail::Failure<PythonRawEnvironmentImporter>(
          contracts::ErrorCode::kInvalidArgument,
          "Python raw importer requires executable, script, and positive "
          "timeout");
    }
    std::error_code error;
    auto executable =
        std::filesystem::weakly_canonical(config.python_executable, error);
    if(error || !std::filesystem::is_regular_file(executable, error) || error) {
      return raw_detail::Failure<PythonRawEnvironmentImporter>(
          contracts::ErrorCode::kNotFound,
          "Python executable does not exist: " +
              config.python_executable.string());
    }
#ifndef _WIN32
    if(access(executable.c_str(), X_OK) != 0) {
      return raw_detail::Failure<PythonRawEnvironmentImporter>(
          contracts::ErrorCode::kFailedPrecondition,
          "Python interpreter is not executable: " + executable.string());
    }
#endif
    error.clear();
    auto script =
        std::filesystem::weakly_canonical(config.importer_script, error);
    if(error || !std::filesystem::is_regular_file(script, error) || error) {
      return raw_detail::Failure<PythonRawEnvironmentImporter>(
          contracts::ErrorCode::kNotFound,
          "Raw environment importer script does not exist: " +
              config.importer_script.string());
    }
    config.python_executable = std::move(executable);
    config.importer_script = std::move(script);
    return PythonRawEnvironmentImporter{std::move(config)};
  }

  [[nodiscard]] auto Import(
      const RawEnvironmentImportRequest& request) const
      -> contracts::Result<RawEnvironmentImportResult> override {
    const auto request_status = raw_detail::ValidateImportRequest(request);
    if(!request_status) {
      return std::unexpected(request_status.error());
    }
    std::error_code error;
    const auto temperature = std::filesystem::weakly_canonical(
        request.woa_temperature_path, error);
    if(error || !std::filesystem::is_regular_file(temperature, error) ||
       error) {
      return raw_detail::Failure<RawEnvironmentImportResult>(
          contracts::ErrorCode::kNotFound,
          "WOA temperature source does not exist");
    }
    error.clear();
    const auto salinity = std::filesystem::weakly_canonical(
        request.woa_salinity_path, error);
    if(error || !std::filesystem::is_regular_file(salinity, error) || error) {
      return raw_detail::Failure<RawEnvironmentImportResult>(
          contracts::ErrorCode::kNotFound,
          "WOA salinity source does not exist");
    }
    std::optional<std::filesystem::path> gebco_response;
    if(request.gebco_response_path) {
      error.clear();
      auto resolved = std::filesystem::weakly_canonical(
          *request.gebco_response_path, error);
      if(error || !std::filesystem::is_regular_file(resolved, error) ||
         error) {
        return raw_detail::Failure<RawEnvironmentImportResult>(
            contracts::ErrorCode::kNotFound,
            "Recorded GEBCO response does not exist");
      }
      gebco_response = std::move(resolved);
    }
    error.clear();
    const auto output_root =
        std::filesystem::weakly_canonical(request.output_root, error);
    if(error) {
      return raw_detail::Failure<RawEnvironmentImportResult>(
          contracts::ErrorCode::kInvalidArgument,
          "Unable to resolve raw environment output root");
    }

    std::vector<std::string> arguments{
        raw_detail::PathUtf8(config_.importer_script),
        "--woa-temperature",
        raw_detail::PathUtf8(temperature),
        "--woa-salinity",
        raw_detail::PathUtf8(salinity),
        "--latitude",
        raw_detail::FormatDouble(request.latitude_degrees),
        "--longitude",
        raw_detail::FormatDouble(request.longitude_degrees),
        "--depth-limit-m",
        raw_detail::FormatDouble(request.depth_limit_meters),
        "--bearing-deg",
        raw_detail::FormatDouble(request.transect_bearing_degrees),
        "--range-max-m",
        raw_detail::FormatDouble(request.maximum_range_meters),
        "--sample-count",
        std::to_string(request.bathymetry_sample_count),
        "--asset-id",
        request.asset_id,
        "--asset-name",
        request.asset_name,
        "--time-label",
        request.time_descriptor,
        "--woa-dataset",
        request.woa_dataset,
        "--gebco-dataset",
        request.gebco_dataset,
        "--output-root",
        raw_detail::PathUtf8(output_root)};
    if(gebco_response) {
      arguments.push_back("--gebco-response-file");
      arguments.push_back(raw_detail::PathUtf8(*gebco_response));
    } else {
      arguments.push_back("--fetch-gebco");
    }
    if(request.overwrite_outputs) {
      arguments.push_back("--overwrite");
    }
    const auto execution = raw_detail::ExecutePython(config_, arguments);
    if(!execution) {
      return std::unexpected(execution.error());
    }

    const auto expected_manifest =
        output_root / "data" / "woss_sources" /
        (request.asset_id + ".json");
    error.clear();
    const auto manifest =
        std::filesystem::weakly_canonical(expected_manifest, error);
    if(error || !std::filesystem::is_regular_file(manifest, error) || error) {
      return raw_detail::Failure<RawEnvironmentImportResult>(
          contracts::ErrorCode::kNotFound,
          "Raw environment importer did not produce the expected manifest");
    }
    return RawEnvironmentImportResult{output_root, manifest};
  }

  [[nodiscard]] auto config() const noexcept
      -> const PythonRawEnvironmentImporterConfig& {
    return config_;
  }

 private:
  explicit PythonRawEnvironmentImporter(
      PythonRawEnvironmentImporterConfig config)
      : config_(std::move(config)) {}

  PythonRawEnvironmentImporterConfig config_;
};

struct RawEnvironmentAssetBuildRequest final {
  RawEnvironmentImportRequest raw_import;
  std::uint32_t asset_version;
  EnvironmentAssetGenerationRequest generation;
};

// One offline transaction: import raw sources, load normalized source assets,
// run Bellhop, construct the complete immutable asset, and finally publish it
// to the repository. Runtime channel queries cannot reach this class.
class OfflineRawEnvironmentAssetBuilder final {
 public:
  OfflineRawEnvironmentAssetBuilder(
      const IRawEnvironmentImporter& raw_importer,
      const IBellhopEnvironmentBuilder& environment_builder,
      const IBellhopRunner& bellhop_runner,
      IEnvironmentAssetRepository& repository)
      : raw_importer_(raw_importer),
        bellhop_pipeline_(environment_builder, bellhop_runner),
        repository_(repository) {}

  [[nodiscard]] auto BuildAndStore(RawEnvironmentAssetBuildRequest request)
      const
      -> contracts::Result<
          std::shared_ptr<const internal::AcousticEnvironmentAsset>> {
    const auto import_status =
        raw_detail::ValidateImportRequest(request.raw_import);
    if(!import_status) {
      return std::unexpected(import_status.error());
    }
    if(request.asset_version == 0U) {
      return raw_detail::Failure<
          std::shared_ptr<const internal::AcousticEnvironmentAsset>>(
          contracts::ErrorCode::kInvalidArgument,
          "Raw environment asset build requires an id and non-zero version");
    }
    const EnvironmentAssetKey key{request.raw_import.asset_id,
                                  request.asset_version};
    if(repository_.get().Find(key)) {
      return raw_detail::Failure<
          std::shared_ptr<const internal::AcousticEnvironmentAsset>>(
          contracts::ErrorCode::kAlreadyExists,
          "Environment asset id/version already exists before raw import");
    }

    auto imported = raw_importer_.get().Import(request.raw_import);
    if(!imported) {
      return std::unexpected(imported.error());
    }
    auto cached = WossCacheLoader::Load(imported->manifest_path,
                                        imported->asset_root);
    if(!cached) {
      return std::unexpected(cached.error());
    }
    const auto imported_woa = cached->manifest.datasets.find("woa");
    const auto imported_gebco = cached->manifest.datasets.find("gebco");
    if(cached->manifest.source_id != request.raw_import.asset_id ||
       cached->manifest.time_descriptor !=
           request.raw_import.time_descriptor ||
       cached->source_query.latitude_degrees() !=
           request.raw_import.latitude_degrees ||
       cached->source_query.longitude_degrees() !=
           request.raw_import.longitude_degrees ||
       imported_woa == cached->manifest.datasets.end() ||
       imported_woa->second != request.raw_import.woa_dataset ||
       imported_gebco == cached->manifest.datasets.end() ||
       imported_gebco->second != request.raw_import.gebco_dataset) {
      return raw_detail::Failure<
          std::shared_ptr<const internal::AcousticEnvironmentAsset>>(
          contracts::ErrorCode::kFailedPrecondition,
          "Imported manifest identity, time, location, or dataset versions "
          "do not match the raw build request");
    }

    request.generation.asset_provenance = cached->provenance.Describe();
    request.generation.geographic_region = cached->geographic_region;
    request.generation.source_query = cached->source_query;
    auto asset = bellhop_pipeline_.GenerateEnvironmentAsset(
        request.generation,
        {cached->manifest.source_id,
         request.asset_version,
         cached->manifest.time_descriptor,
         {cached->provenance.source_format,
          cached->provenance.source_uri,
          cached->provenance.content_digest,
          cached->provenance.generated_by}},
        std::move(cached->sound_speed_profile),
        std::move(cached->bathymetry_profile));
    if(!asset) {
      return std::unexpected(asset.error());
    }
    auto published =
        std::make_shared<const internal::AcousticEnvironmentAsset>(
            std::move(*asset));
    const auto stored = repository_.get().Store(published);
    if(!stored) {
      return std::unexpected(stored.error());
    }
    return published;
  }

 private:
  std::reference_wrapper<const IRawEnvironmentImporter> raw_importer_;
  OfflineBellhopAssetPipeline bellhop_pipeline_;
  std::reference_wrapper<IEnvironmentAssetRepository> repository_;
};

}  // namespace ns3_factory::environment
