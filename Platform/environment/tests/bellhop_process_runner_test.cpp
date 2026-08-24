#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "internal/bellhop_process_runner.hpp"

using namespace ns3_factory;

namespace {

auto Check(bool condition) -> void {
  if(!condition) {
    std::abort();
  }
}

class TemporaryDirectory final {
 public:
  TemporaryDirectory() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    path_ = std::filesystem::temp_directory_path() /
            ("platform-bellhop-runner-test-" + std::to_string(suffix));
    Check(std::filesystem::create_directory(path_));
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  auto operator=(const TemporaryDirectory&) -> TemporaryDirectory& = delete;

  [[nodiscard]] auto path() const noexcept
      -> const std::filesystem::path& {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace

int main(int argc, char** argv) {
  Check(argc == 2);
  TemporaryDirectory workspace;
  auto runner = environment::BellhopProcessRunner::Create(
      {std::filesystem::path{argv[1]},
       workspace.path(),
       std::chrono::seconds{2},
       1024U * 1024U});
  Check(runner.has_value());

  const environment::BellhopInputFiles input{
      "fixture env", "fixture bty", "fixture ati"};
  const auto result = runner->Run("valid-case", input);
  Check(result.has_value());
  Check(result->arrivals_ascii.starts_with("'2D'\n12000\n"));
  Check(!result->runner_identity.empty());

  const auto duplicate = runner->Run("valid-case", input);
  Check(!duplicate.has_value());
  Check(duplicate.error().code == contracts::ErrorCode::kAlreadyExists);
  const auto unsafe = runner->Run("../escape", input);
  Check(!unsafe.has_value());
  Check(unsafe.error().code == contracts::ErrorCode::kInvalidArgument);

  auto timeout_runner = environment::BellhopProcessRunner::Create(
      {std::filesystem::path{argv[1]},
       workspace.path(),
       std::chrono::milliseconds{50},
       1024U * 1024U});
  Check(timeout_runner.has_value());
  const auto timed_out = timeout_runner->Run("timeout-case", input);
  Check(!timed_out.has_value());
  Check(timed_out.error().code == contracts::ErrorCode::kUnavailable);

  return EXIT_SUCCESS;
}
