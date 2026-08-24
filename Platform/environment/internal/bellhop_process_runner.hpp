#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "bellhop_environment_builder.hpp"

namespace ns3_factory::environment {

struct BellhopProcessRunnerConfig final {
  std::filesystem::path executable_path;
  std::filesystem::path workspace_root;
  std::chrono::milliseconds timeout;
  std::uintmax_t maximum_arrivals_bytes;
};

// Concrete offline runner. It invokes Bellhop directly without a command
// shell, creates one isolated directory per case, refuses to overwrite an
// existing case, and validates the generated ARR before returning it.
class BellhopProcessRunner final : public IBellhopRunner {
 public:
  [[nodiscard]] static auto Create(BellhopProcessRunnerConfig config)
      -> contracts::Result<BellhopProcessRunner>;

  [[nodiscard]] auto Run(std::string_view case_name,
                         const BellhopInputFiles& input_files) const
      -> contracts::Result<BellhopRunResult> override;

  [[nodiscard]] auto config() const noexcept
      -> const BellhopProcessRunnerConfig& {
    return config_;
  }

 private:
  explicit BellhopProcessRunner(BellhopProcessRunnerConfig config)
      : config_(std::move(config)) {}

  BellhopProcessRunnerConfig config_;
};

}  // namespace ns3_factory::environment

#include "bellhop_process_runner_impl.hpp"
