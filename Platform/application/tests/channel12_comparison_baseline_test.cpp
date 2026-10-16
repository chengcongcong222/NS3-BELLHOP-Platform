#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "internal/acceptance_preset.hpp"
#include "internal/acceptance_run_executor.hpp"
#include "internal/reference_environment_asset.hpp"
#include "internal/target_packet_metrics.hpp"

namespace {

using namespace ns3_factory;
using namespace contracts;
using namespace application;
using namespace application::internal;
using namespace environment::internal;
using namespace phy::internal;

auto Check(bool condition, std::string_view label) -> bool {
  if(!condition) std::cerr << "FAILED: " << label << '\n';
  return condition;
}

// Delete only the unique directory created by this test invocation.
class TemporaryAssets final {
 public:
  TemporaryAssets() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for(int index = 0; index < 100; ++index) {
      auto candidate = std::filesystem::temp_directory_path() /
          ("channel12-metrics-" + std::to_string(stamp) + "-" +
           std::to_string(index));
      if(std::filesystem::create_directory(candidate)) {
        path = std::move(candidate);
        return;
      }
    }
  }

  ~TemporaryAssets() {
    if(path.empty()) return;
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }

  std::filesystem::path path;
};

struct RecordingTrace final : ITraceSink {
  auto Emit(const TraceEvent& event) noexcept -> Status override {
    events.push_back(event);
    return {};
  }
  std::vector<TraceEvent> events;
};

struct ScalarObserver final : ISampledScalarReceptionObserver {
  void Observe(const SampledScalarReceptionSummary& row) noexcept override {
    rows.push_back(row);
  }
  std::vector<SampledScalarReceptionSummary> rows;
};

struct WaveformObserver final : IWaveformReceptionObserver {
  void Observe(const WaveformReceptionSummary& row) noexcept override {
    rows.push_back(row);
  }
  std::vector<WaveformReceptionSummary> rows;
};

auto ScalarRows(const ScalarObserver& source)
    -> std::vector<TargetPacketObservation> {
  std::vector<TargetPacketObservation> rows;
  for(const auto& row : source.rows) {
    rows.push_back({row.transmission_id, row.receiver_node_id,
                    row.payload_bit_count, row.bit_error_count, row.crc_ok});
  }
  return rows;
}

auto WaveformRows(const WaveformObserver& source)
    -> std::vector<TargetPacketObservation> {
  std::vector<TargetPacketObservation> rows;
  for(const auto& row : source.rows) {
    rows.push_back({row.transmission_id, row.receiver_node_id,
                    row.payload_bit_count, row.bit_error_count, row.crc_ok});
  }
  return rows;
}

auto SameScalarRows(const ScalarObserver& first,
                    const ScalarObserver& second) -> bool {
  if(first.rows.size() != second.rows.size()) return false;
  for(std::size_t index = 0; index < first.rows.size(); ++index) {
    const auto& lhs = first.rows[index];
    const auto& rhs = second.rows[index];
    if(lhs.transmission_id != rhs.transmission_id ||
       lhs.receiver_node_id != rhs.receiver_node_id ||
       lhs.payload_bit_count != rhs.payload_bit_count ||
       lhs.bit_error_count != rhs.bit_error_count ||
       lhs.crc_ok != rhs.crc_ok ||
       lhs.modeled_bit_error_probability !=
           rhs.modeled_bit_error_probability) {
      return false;
    }
  }
  return true;
}

auto TestRealRunsShareTargetPacketBasis() -> bool {
  TemporaryAssets temporary;
  if(!Check(!temporary.path.empty(), "Temporary repository")) return false;
  auto repository = EnvironmentAssetRepository::Open(temporary.path);
  auto asset = BuildReferenceShallowWaterV1(
      PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT);
  auto provenance = ReferenceShallowWaterV1Provenance();
  auto asset_id = EnvironmentAssetId::Create(
      kReferenceShallowWaterV1AssetId);
  if(!Check(repository && asset && provenance && asset_id,
            "Reference Bellhop asset")) {
    return false;
  }
  if(!Check(static_cast<bool>(repository->Register(*asset_id, *asset, *provenance)),
            "Register reference environment")) {
    return false;
  }

  auto scenario_id = ScenarioId::Create("channel12-metrics");
  auto experiment_id = ExperimentId::Create("channel12-metrics");
  auto run_id = RunId::Create("channel12-metrics");
  if(!Check(scenario_id && experiment_id && run_id, "Run identities"))
    return false;
  auto scenario = MakeAcceptanceScenarioPreset(
      AcceptanceProfile::kAcceptance4Node, *scenario_id, 1,
      "Identical target packets", EnvironmentReference{asset_id->value(), 1});
  if(!Check(static_cast<bool>(scenario), "Acceptance scenario")) return false;
  auto preset = MakeAcceptanceExperimentPreset(
      AcceptanceProfile::kAcceptance4Node, *experiment_id, 1,
      "Channel 1 and 2 metric basis",
      ScenarioReference{scenario->scenario_id(), scenario->version()}, 2,
      RxQualityMode::kNone, 45.0, 42U);
  if(!Check(static_cast<bool>(preset), "Acceptance experiment")) return false;
  auto experiment = ExperimentDefinition::Create(
      preset->experiment_id(), 1, "Channel 1 and 2 metric basis",
      preset->scenario(), preset->routing(),
      MacConfiguration{MacMode::kTdma,
                       SimDuration::FromNanoseconds(6'000'000'000),
                       SimDuration::FromNanoseconds(2'000'000'000)},
      preset->phy(), preset->fusion(),
      preset->network_update_interval_cycles(),
      preset->simulation_cycle_count(), 42U);
  if(!Check(static_cast<bool>(experiment), "Framed airtime compatible TDMA")) return false;

  ScalarObserver scalar_observer;
  ScalarObserver scalar_repeat_observer;
  WaveformObserver waveform_observer;
  RecordingTrace scalar_trace, repeated_trace, waveform_trace;
  AcceptanceRunExecutor scalar{
      *repository, ScalarSamplingExecutionOptions{42U, &scalar_observer}};
  AcceptanceRunExecutor scalar_repeat{
      *repository, ScalarSamplingExecutionOptions{
                       42U, &scalar_repeat_observer}};
  WaveformPhyConfig waveform_config{
      ModulationConfig{
          ModulationScheme::kOsdm, 6'000.0, 6'000.0,
          std::pow(10.0,
                   experiment->phy().source_level_db_re_1upa_at_1m / 20.0),
          25'000.0, 30.0,
          OsdmConfig{1024U, 16U, 640U, OsdmConstellation::kQpsk,
                     true, 0.05, 1280U}},
      4'000.0, NoNoiseConfig{},
      {ChannelEqualizationMode::kTrainingSequenceMmse}, true,
      WaveformFrameFormat::kPayloadWithCrc32, 1'000'000};
  AcceptanceRunExecutor waveform{
      *repository,
      WaveformExecutionOptions{waveform_config, &waveform_observer}};

  const auto scalar_result =
      scalar.Execute(*run_id, *scenario, *experiment, scalar_trace);
  const auto repeat_result =
      scalar_repeat.Execute(*run_id, *scenario, *experiment, repeated_trace);
  const auto waveform_result =
      waveform.Execute(*run_id, *scenario, *experiment, waveform_trace);
  if(!Check(scalar_result && repeat_result && waveform_result,
            "Execute both PHY compositions")) {
    if(!scalar_result) std::cerr << scalar_result.error().message << '\n';
    if(!repeat_result) std::cerr << repeat_result.error().message << '\n';
    if(!waveform_result) std::cerr << waveform_result.error().message << '\n';
    return false;
  }
  const auto scalar_metrics = BuildTargetPacketMetrics(
      scalar_trace.events, ScalarRows(scalar_observer), 120U);
  const auto repeated_metrics = BuildTargetPacketMetrics(
      repeated_trace.events, ScalarRows(scalar_repeat_observer), 120U);
  const auto waveform_metrics = BuildTargetPacketMetrics(
      waveform_trace.events, WaveformRows(waveform_observer), 120U);
  if(!Check(scalar_metrics && repeated_metrics && waveform_metrics,
            "Build target-only metrics for both paths")) {
    if(!scalar_metrics) std::cerr << scalar_metrics.error().message << '\n';
    if(!repeated_metrics)
      std::cerr << repeated_metrics.error().message << '\n';
    if(!waveform_metrics)
      std::cerr << waveform_metrics.error().message << '\n';
    return false;
  }
  return Check(scalar_metrics->target_attempt_count == 6U &&
                   waveform_metrics->target_attempt_count == 6U,
               "Both paths sent six target feature packets") &&
         Check(ValidateSameTargetAttempts(*scalar_metrics,
                                          *waveform_metrics) &&
                   ValidateSameTargetAttempts(*scalar_metrics,
                                              *repeated_metrics),
               "Same target packet identities and bit denominators") &&
         Check(SameScalarRows(scalar_observer, scalar_repeat_observer) &&
                   scalar_metrics->payload_bit_error_count ==
                       repeated_metrics->payload_bit_error_count &&
                   scalar_metrics->packet_error_count ==
                       repeated_metrics->packet_error_count,
               "Explicit seed reproduces sampled scalar outcomes") &&
         Check(scalar_metrics->evaluated_target_receptions +
                   scalar_metrics->no_arrival_count == 6U &&
                   waveform_metrics->evaluated_target_receptions +
                       waveform_metrics->no_arrival_count == 6U &&
                   scalar_metrics->packet_error_rate &&
                   waveform_metrics->packet_error_rate,
               "No-arrival is covered by PER on both paths");
}

}  // namespace

int main() {
  return TestRealRunsShareTargetPacketBasis() ? EXIT_SUCCESS : EXIT_FAILURE;
}
