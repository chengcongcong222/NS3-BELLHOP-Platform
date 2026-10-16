#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/acceptance_preset.hpp"
#include "internal/acceptance_run_executor.hpp"
#include "internal/paired_packet_comparison.hpp"
#include "internal/reference_environment_asset.hpp"
#include "internal/target_packet_metrics.hpp"

namespace {

using namespace ns3_factory;
using namespace contracts;
using namespace application;
using namespace application::internal;
using namespace environment::internal;
using namespace phy::internal;

struct Options final {
  std::size_t repetitions = 100U;
  std::size_t cycles = 4U;
  std::size_t bootstrap_resamples = 2'000U;
  std::uint64_t first_seed = 42U;
  double noise_psd_db = 0.0;
  std::filesystem::path asset_root =
      PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT;
};

auto ParseUnsigned(const std::string& value) -> std::uint64_t {
  if(value.empty() ||
     value.find_first_not_of("0123456789") != std::string::npos) {
    throw std::runtime_error("Expected an unsigned decimal integer");
  }
  std::size_t used = 0U;
  const auto parsed = std::stoull(value, &used);
  if(used != value.size()) {
    throw std::runtime_error("Invalid unsigned decimal integer");
  }
  return parsed;
}

auto Parse(int argc, char** argv) -> Options {
  Options options;
  for(int index = 1; index < argc; ++index) {
    const std::string_view flag{argv[index]};
    if(index + 1 >= argc) {
      throw std::runtime_error("Missing value after option");
    }
    const std::string value{argv[++index]};
    if(flag == "--repetitions") {
      options.repetitions = ParseUnsigned(value);
    } else if(flag == "--cycles") {
      options.cycles = ParseUnsigned(value);
    } else if(flag == "--bootstrap") {
      options.bootstrap_resamples = ParseUnsigned(value);
    } else if(flag == "--seed") {
      options.first_seed = ParseUnsigned(value);
    } else if(flag == "--noise-psd-db") {
      std::size_t used = 0U;
      options.noise_psd_db = std::stod(value, &used);
      if(used != value.size()) {
        throw std::runtime_error("Invalid noise PSD value");
      }
    } else if(flag == "--asset-root") {
      options.asset_root = value;
    } else {
      throw std::runtime_error("Unknown option: " + std::string{flag});
    }
  }
  if(options.repetitions < 2U || options.repetitions > 1'000U ||
     options.cycles < 2U || options.cycles > 100U ||
     options.bootstrap_resamples < 100U ||
     options.bootstrap_resamples > 100'000U ||
     !std::isfinite(options.noise_psd_db) ||
     options.first_seed >
         std::numeric_limits<std::uint64_t>::max() -
             (options.repetitions - 1U)) {
    throw std::runtime_error("Invalid comparison configuration");
  }
  return options;
}

template<class T>
auto Require(Result<T> result) -> T {
  if(!result) throw std::runtime_error(result.error().message);
  return std::move(*result);
}

auto Require(Status result) -> void {
  if(!result) throw std::runtime_error(result.error().message);
}

// The repository is an output artifact of this invocation only; reference
// Bellhop source files remain untouched.
class TemporaryAssets final {
 public:
  TemporaryAssets() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for(int attempt = 0; attempt < 100; ++attempt) {
      auto candidate = std::filesystem::temp_directory_path() /
          ("channel12-comparison-" + std::to_string(stamp) + "-" +
           std::to_string(attempt));
      if(std::filesystem::create_directory(candidate)) {
        path_ = std::move(candidate);
        return;
      }
    }
    throw std::runtime_error("Cannot create unique temporary asset directory");
  }

  ~TemporaryAssets() {
    std::error_code ignored;
    if(!path_.empty()) std::filesystem::remove_all(path_, ignored);
  }

  TemporaryAssets(const TemporaryAssets&) = delete;
  auto operator=(const TemporaryAssets&) -> TemporaryAssets& = delete;

  [[nodiscard]] auto path() const -> const std::filesystem::path& {
    return path_;
  }

 private:
  std::filesystem::path path_;
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

template<class Row>
auto ToMetricRows(const std::vector<Row>& source)
    -> std::vector<TargetPacketObservation> {
  std::vector<TargetPacketObservation> rows;
  rows.reserve(source.size());
  for(const auto& row : source) {
    rows.push_back({row.transmission_id, row.receiver_node_id,
                    row.payload_bit_count, row.bit_error_count, row.crc_ok});
  }
  return rows;
}

auto RunPair(const EnvironmentAssetRepository& repository,
             const ScenarioDefinition& scenario,
             const Options& options,
             std::uint64_t seed) -> PairedPacketRun {
  const auto seed_text = std::to_string(seed);
  auto preset = Require(MakeAcceptanceExperimentPreset(
      AcceptanceProfile::kAcceptance4Node,
      Require(ExperimentId::Create("channel12-comparison-" + seed_text)),
      1, "Paired OSDM AWGN experiment",
      ScenarioReference{scenario.scenario_id(), scenario.version()},
      options.cycles, RxQualityMode::kNone, 0.0, seed));
  const auto bandwidth = preset.phy().occupied_bandwidth_hz;
  const auto equivalent_noise_db =
      options.noise_psd_db + 10.0 * std::log10(bandwidth);
  if(!std::isfinite(equivalent_noise_db)) {
    throw std::runtime_error("Integrated noise power is not finite");
  }
  auto phy = preset.phy();
  phy.equivalent_noise_power_db_re_1upa2 = equivalent_noise_db;
  auto experiment = Require(ExperimentDefinition::Create(
      preset.experiment_id(), preset.version(), preset.name(),
      preset.scenario(), preset.routing(),
      MacConfiguration{MacMode::kTdma,
                       SimDuration::FromNanoseconds(6'000'000'000),
                       SimDuration::FromNanoseconds(2'000'000'000)},
      phy, preset.fusion(), preset.network_update_interval_cycles(),
      preset.simulation_cycle_count(), seed));

  ScalarObserver scalar_observer;
  WaveformObserver waveform_observer;
  RecordingTrace scalar_trace, waveform_trace;
  AcceptanceRunExecutor scalar{
      repository, ScalarSamplingExecutionOptions{seed, &scalar_observer}};
  const auto scalar_run_id =
      Require(RunId::Create("channel12-" + seed_text + "-scalar"));
  const auto waveform_run_id =
      Require(RunId::Create("channel12-" + seed_text + "-waveform"));
  const auto scalar_result =
      Require(scalar.Execute(scalar_run_id, scenario, experiment, scalar_trace));

  WaveformPhyConfig waveform_config{
      ModulationConfig{
          ModulationScheme::kOsdm, 6'000.0, 6'000.0,
          std::pow(10.0,
                   experiment.phy().source_level_db_re_1upa_at_1m / 20.0),
          experiment.phy().center_frequency_hz, 30.0,
          OsdmConfig{1024U, 16U, 640U, OsdmConstellation::kQpsk,
                     true, 0.05, 1280U}},
      experiment.phy().occupied_bandwidth_hz,
      AwgnNoiseConfig{options.noise_psd_db, seed},
      {ChannelEqualizationMode::kTrainingSequenceMmse}, true,
      WaveformFrameFormat::kPayloadWithCrc32, 1'000'000};
  AcceptanceRunExecutor waveform{
      repository, WaveformExecutionOptions{
                      waveform_config, &waveform_observer}};
  const auto waveform_result = Require(waveform.Execute(
      waveform_run_id, scenario, experiment, waveform_trace));
  if(scalar_result.projection.transmission_count !=
     waveform_result.projection.transmission_count) {
    throw std::runtime_error("Paired runs sent different packet counts");
  }
  constexpr auto payload_bits =
      assembly::internal::kDetectionFeatureV1PayloadBytes * 8U;
  auto scalar_metrics = Require(BuildTargetPacketMetrics(
      scalar_trace.events, ToMetricRows(scalar_observer.rows), payload_bits));
  auto waveform_metrics = Require(BuildTargetPacketMetrics(
      waveform_trace.events, ToMetricRows(waveform_observer.rows),
      payload_bits));
  Require(ValidateSameTargetAttempts(scalar_metrics, waveform_metrics));
  const auto expected_attempts =
      options.cycles * (scenario.nodes().size() - 1U);
  if(scalar_metrics.target_attempt_count != expected_attempts) {
    throw std::runtime_error(
        "Reference scenario did not send all planned target packets");
  }
  return PairedPacketRun{seed, std::move(scalar_metrics),
                         std::move(waveform_metrics)};
}

auto PrintOptional(std::optional<double> value) -> void {
  if(value) std::cout << *value;
  else std::cout << "N/A";
}

auto PrintInterval(std::optional<PercentileInterval> value) -> void {
  if(value) std::cout << '[' << value->lower << ',' << value->upper << ']';
  else std::cout << "N/A";
}

auto PrintTotals(std::string_view label, const PacketMetricTotals& totals)
    -> void {
  std::cout << label << ": attempts=" << totals.target_attempt_count
            << " no_arrival=" << totals.no_arrival_count
            << " packet_errors=" << totals.packet_error_count
            << " observed_payload_bits=" << totals.observed_payload_bit_count
            << " payload_bit_errors=" << totals.payload_bit_error_count
            << " BER=";
  PrintOptional(totals.BitErrorRate());
  std::cout << " PER=";
  PrintOptional(totals.PacketErrorRate());
  std::cout << '\n';
}

auto Run(const Options& options) -> void {
  TemporaryAssets temporary;
  auto repository = Require(EnvironmentAssetRepository::Open(temporary.path()));
  auto asset = Require(BuildReferenceShallowWaterV1(options.asset_root));
  auto provenance = Require(ReferenceShallowWaterV1Provenance());
  auto asset_id = Require(EnvironmentAssetId::Create(
      kReferenceShallowWaterV1AssetId));
  Require(repository.Register(asset_id, asset, provenance));
  auto scenario = Require(MakeAcceptanceScenarioPreset(
      AcceptanceProfile::kAcceptance4Node,
      Require(ScenarioId::Create("channel12-comparison")), 1,
      "Channel 1/2 paired comparison",
      EnvironmentReference{asset_id.value(), 1}));
  std::vector<PairedPacketRun> runs;
  runs.reserve(options.repetitions);
  for(std::size_t index = 0U; index < options.repetitions; ++index) {
    runs.push_back(RunPair(repository, scenario, options,
                           options.first_seed + index));
  }
  const auto summary = Require(ComparePairedPacketRuns(
      runs, options.bootstrap_resamples,
      options.first_seed ^ 0x5eed19d1d8f7a21bULL));
  const auto example = Require(MakeAcceptanceExperimentPreset(
      AcceptanceProfile::kAcceptance4Node,
      Require(ExperimentId::Create("channel12-summary")), 1,
      "Noise mapping", ScenarioReference{
          scenario.scenario_id(), scenario.version()}, options.cycles,
      RxQualityMode::kNone));
  const auto bandwidth = example.phy().occupied_bandwidth_hz;
  const auto equivalent_noise_db =
      options.noise_psd_db + 10.0 * std::log10(bandwidth);

  std::cout << std::setprecision(10);
  std::cout << "Channel 1/2 paired scalar-vs-OSDM-AWGN comparison (experiment only)\n"
            << "source=Bellhop-derived reference .arr; NOT field-measured\n"
            << "channel2_equalization=ideal-known-channel-frequency-domain-mmse\n"
            << "repetitions=" << options.repetitions
            << " cycles_per_run=" << options.cycles
            << " first_seed=" << options.first_seed
            << " bootstrap_resamples=" << options.bootstrap_resamples
            << "\nnoise_psd_db_re_1upa2_per_hz=" << options.noise_psd_db
            << " scalar_in_band_noise_db_re_1upa2="
            << equivalent_noise_db
            << " occupied_bandwidth_hz=" << bandwidth << '\n';
  PrintTotals("Channel 1 sampled scalar", summary.scalar);
  PrintTotals("Channel 2 waveform", summary.waveform);
  std::cout << "difference(Channel2-Channel1): BER=";
  PrintOptional(summary.waveform_minus_scalar_ber);
  std::cout << " paired_bootstrap_95%=";
  PrintInterval(summary.ber_difference_interval_95);
  std::cout << " PER=";
  PrintOptional(summary.waveform_minus_scalar_per);
  std::cout << " paired_bootstrap_95%=";
  PrintInterval(summary.per_difference_interval_95);
  std::cout << "\nInterpretation: negative difference favors Channel 2;"
               " intervals describe seed variability in this fixed scene,"
               " not all sea states.\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    Run(Parse(argc, argv));
    return EXIT_SUCCESS;
  } catch(const std::exception& error) {
    std::cerr << "Channel 1/2 comparison failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
