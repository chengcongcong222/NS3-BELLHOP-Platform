#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "internal/acceptance_preset.hpp"
#include "internal/acceptance_run_executor.hpp"
#include "internal/reference_environment_asset.hpp"

namespace {

using namespace ns3_factory;
using namespace contracts;
using namespace application;
using namespace application::internal;
using namespace environment::internal;
using namespace phy::internal;

template<class T>
auto Require(Result<T> result) -> T {
  if(!result) throw std::runtime_error(result.error().message);
  return std::move(*result);
}

struct Options {
  std::string modulation = "osdm";
  std::string noise = "none";
  std::filesystem::path asset_root = PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT;
  double psd_db = 0.0;
  double wind = 0.0;
  double shipping = 0.5;
  std::uint64_t seed = 42;
  bool verify = false;
};

auto Parse(int argc, char** argv) -> Options {
  Options options;
  for(int i = 1; i < argc; ++i) {
    const std::string_view argument{argv[i]};
    if(argument == "--verify") {
      options.verify = true;
      continue;
    }
    if(i + 1 == argc) throw std::runtime_error("Missing option value");
    const std::string value{argv[++i]};
    auto number = [&] {
      std::size_t used = 0;
      const auto parsed = std::stod(value, &used);
      if(used != value.size() || !std::isfinite(parsed)) {
        throw std::runtime_error("Expected a finite number");
      }
      return parsed;
    };
    if(argument == "--modulation") options.modulation = value;
    else if(argument == "--noise") options.noise = value;
    else if(argument == "--asset-root") options.asset_root = value;
    else if(argument == "--psd-db") options.psd_db = number();
    else if(argument == "--wind") options.wind = number();
    else if(argument == "--shipping") options.shipping = number();
    else if(argument == "--seed") {
      if(value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
        throw std::runtime_error("Seed must be an unsigned integer");
      }
      options.seed = std::stoull(value);
    } else throw std::runtime_error("Unknown option: " + std::string{argument});
  }
  if(options.modulation != "osdm" && options.modulation != "bpsk" &&
     options.modulation != "bfsk") {
    throw std::runtime_error("Supported modulation: osdm, bpsk, bfsk");
  }
  if(options.noise != "none" && options.noise != "awgn" && options.noise != "wenz") {
    throw std::runtime_error("Supported noise: none, awgn, wenz");
  }
  return options;
}

// Only remove the unique directory successfully created by this invocation.
class TemporaryAssetRepository {
 public:
  TemporaryAssetRepository() {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for(int attempt = 0; attempt < 100; ++attempt) {
      auto candidate = std::filesystem::temp_directory_path() /
          ("ns3-channel2-feature-" + std::to_string(timestamp) + "-" +
           std::to_string(attempt));
      if(std::filesystem::create_directory(candidate)) {
        path = std::move(candidate);
        return;
      }
    }
    throw std::runtime_error("Cannot create a unique temporary asset repository");
  }
  ~TemporaryAssetRepository() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  TemporaryAssetRepository(const TemporaryAssetRepository&) = delete;
  auto operator=(const TemporaryAssetRepository&) -> TemporaryAssetRepository& = delete;
  std::filesystem::path path;
};

class ReceptionObserver final : public IWaveformReceptionObserver {
 public:
  void Observe(const WaveformReceptionSummary& summary) noexcept override {
    rows.push_back(summary);
  }
  std::vector<WaveformReceptionSummary> rows;
};

auto ExecuteDemo(const Options& options) -> int {
  TemporaryAssetRepository temporary;
  auto repository = Require(EnvironmentAssetRepository::Open(temporary.path));
  auto asset = Require(BuildReferenceShallowWaterV1(options.asset_root));
  const auto provenance = Require(ReferenceShallowWaterV1Provenance());
  const auto asset_id = Require(EnvironmentAssetId::Create(
      kReferenceShallowWaterV1AssetId));
  const auto registered = repository.Register(asset_id, asset, provenance);
  if(!registered) throw std::runtime_error(registered.error().message);
  const auto scenario = Require(MakeAcceptanceScenarioPreset(
      AcceptanceProfile::kAcceptance4Node,
      Require(ScenarioId::Create("channel2-feature-demo")), 1,
      "Channel 2: actual feature reports over Bellhop waveform",
      EnvironmentReference{asset_id.value(), 1}));
  const auto preset = Require(MakeAcceptanceExperimentPreset(
      AcceptanceProfile::kAcceptance4Node,
      Require(ExperimentId::Create("channel2-feature-demo")), 1,
      "Waveform feature round trip",
      ScenarioReference{scenario.scenario_id(), scenario.version()}, 2,
      RxQualityMode::kNone, 45.0, options.seed));
  // Keep a common six-second slot for OSDM and the slower legacy BPSK/BFSK
  // selections so their ns-3 scheduling contract remains directly comparable.
  const auto experiment = Require(ExperimentDefinition::Create(
      preset.experiment_id(), 1, "Waveform feature round trip",
      preset.scenario(), preset.routing(),
      MacConfiguration{MacMode::kTdma, SimDuration::FromNanoseconds(6'000'000'000),
                       SimDuration::FromNanoseconds(2'000'000'000)},
      preset.phy(), preset.fusion(), preset.network_update_interval_cycles(),
      preset.simulation_cycle_count(), options.seed));
  NoiseProfile noise = NoNoiseConfig{};
  if(options.noise == "awgn") noise = AwgnNoiseConfig{options.psd_db, options.seed};
  if(options.noise == "wenz") {
    noise = WenzNoiseConfig{options.shipping, options.wind, options.seed};
  }
  ReceptionObserver observer;
  auto scheme = ModulationScheme::kOsdm;
  if(options.modulation == "bpsk") scheme = ModulationScheme::kBpsk;
  if(options.modulation == "bfsk") scheme = ModulationScheme::kBfsk;
  const auto symbol_rate =
      scheme == ModulationScheme::kOsdm ? 6'000.0 : 60.0;
  const ModulationConfig modulation{
      scheme, 6'000.0, symbol_rate,
      std::pow(10.0,
               experiment.phy().source_level_db_re_1upa_at_1m / 20.0),
      25'000.0, 30.0,
      OsdmConfig{1024U, 16U, 640U, OsdmConstellation::kQpsk,
                 true, 0.05, 1280U}};
  const auto equalization =
      scheme == ModulationScheme::kOsdm
          ? ChannelEqualizationMode::kTrainingSequenceMmse
          : ChannelEqualizationMode::kIdealFrequencyDomainMmse;
  WaveformPhyConfig config{
      modulation,
      4'000.0, noise,
      {equalization}, true,
      WaveformFrameFormat::kPayloadWithCrc32, 1'000'000};
  AcceptanceRunExecutor executor{repository, WaveformExecutionOptions{config, &observer}};
  const auto framed_sample_count =
      Require(ModulatedSampleCount(152U, modulation));
  NullTraceSink trace;
  const auto run_id = Require(RunId::Create("channel2-feature-demo"));
  const auto result = Require(executor.Execute(run_id, scenario, experiment, trace));

  std::cout << "Channel 2: feature -> 15 payload bytes -> 120 bits (+32 CRC bits)"
               " -> modulation -> Bellhop H(X) -> + receiver noise -> demodulation"
               " -> CRC -> recovered payload -> feature fusion\n"
            << "asset=" << (options.asset_root / "bellhop/reference_shallow_water_v1.arr")
            << "\nchannel_source=Bellhop-derived .arr; reference environment, NOT field measurement"
            << "\nmodulation=" << options.modulation << " noise=" << options.noise
            << " seed=" << options.seed
            << " equalization="
            << (scheme == ModulationScheme::kOsdm
                    ? "training-sequence-mmse" : "ideal-frequency-domain-mmse")
            << " fs=6000 carrier=25000 rate="
            << symbol_rate << " slot_seconds=6\n";
  if(scheme == ModulationScheme::kOsdm) {
    std::cout << "osdm_base_constellation=QPSK K=1024 M=16 N=64 CP=640 training=CAZAC"
                 " framed_samples=" << framed_sample_count << '\n';
  }
  if(options.noise == "awgn") {
    std::cout << "noise_psd_db_re_1upa2_per_hz=" << options.psd_db << '\n';
  }
  if(options.noise == "wenz") {
    std::cout << "wind_mps=" << options.wind << " shipping=" << options.shipping << '\n';
  }
  std::cout << "tx,rx,paths,payload_bits,bit_errors,BER,CRC_OK,HX_power_upa2,noise_power_upa2\n";
  std::size_t errors = 0, bits = 0, failed = 0, target_failures = 0;
  for(const auto& row : observer.rows) {
    std::cout << row.transmission_id.value() << ',' << row.receiver_node_id.value()
              << ',' << row.path_count << ',' << row.payload_bit_count
              << ',' << row.bit_error_count << ',' << std::setprecision(8)
              << static_cast<double>(row.bit_error_count) / row.payload_bit_count
              << ',' << row.crc_ok << ',' << row.channel_output_power_upa2
              << ',' << row.noise_power_upa2 << '\n';
    errors += row.bit_error_count;
    bits += row.payload_bit_count;
    failed += !row.crc_ok;
    target_failures += !row.crc_ok &&
                       row.receiver_node_id == scenario.fusion_center_node_id();
  }
  std::cout << "transmissions=" << result.projection.transmission_count
            << " reception_attempts=" << observer.rows.size()
            << " no_arrival=" << result.projection.channel_no_arrival_count
            << " delivered_features=" << result.projection.local_delivery_count
            << " fusion_results=" << result.fusion_results.size() << '\n';
  if(bits != 0) {
    std::cout << "all_receptions_payload_BER=" << static_cast<double>(errors) / bits
              << " CRC_frame_error_ratio=" << static_cast<double>(failed) / observer.rows.size()
              << " (includes overheard receptions)\n";
  }
  for(const auto& fusion : result.fusion_results) {
    std::cout << "recovered_feature_fusion: observations=" << fusion.observation_count
              << " target_x=" << fusion.estimated_target_x_meters
              << " target_y=" << fusion.estimated_target_y_meters << '\n';
  }
  std::cout << "Waveform BER above is counted against TX truth AFTER decoding;"
               " legacy scalar BER report is intentionally not evaluated.\n";
  if(options.verify) {
    if(options.noise == "none" && result.projection.local_delivery_count == 0) {
      throw std::runtime_error("No feature report completed the waveform/runtime round trip");
    }
    if(result.projection.transmission_count != 6 ||
       observer.rows.size() != result.projection.reception_count ||
       observer.rows.empty() ||
       result.projection.local_delivery_count + target_failures != 6 ||
       !result.acceptance_report ||
       result.acceptance_report->bit_error_rate != MetricStatus::kNotEvaluated) {
      throw std::runtime_error("Waveform/runtime accounting verification failed");
    }
    for(const auto& row : observer.rows) {
      if(row.path_count <= 1 || row.payload_bit_count != 120 ||
         row.received_sample_count <= framed_sample_count ||
         (options.noise == "none" ? row.noise_power_upa2 != 0.0
                                  : row.noise_power_upa2 <= 0.0)) {
        throw std::runtime_error("Bellhop waveform/noise verification failed");
      }
    }
    if(options.noise == "awgn" && options.psd_db >= 120.0 &&
       (errors == 0 || failed == 0 || result.projection.local_delivery_count != 0 ||
        !result.fusion_results.empty())) {
      throw std::runtime_error("Strong noise unexpectedly bypassed demodulation");
    }
    // Re-run the same ns-3 workload: reproducible decisions, features and BER.
    const auto first_rows = observer.rows;
    observer.rows.clear();
    const auto repeat = Require(executor.Execute(run_id, scenario, experiment, trace));
    if(repeat != result || first_rows.size() != observer.rows.size()) {
      throw std::runtime_error("Repeated waveform run is not deterministic");
    }
    for(std::size_t i = 0; i < first_rows.size(); ++i) {
      const auto& a = first_rows[i];
      const auto& b = observer.rows[i];
      if(a.transmission_id != b.transmission_id || a.receiver_node_id != b.receiver_node_id ||
         a.bit_error_count != b.bit_error_count || a.crc_ok != b.crc_ok ||
         a.channel_output_power_upa2 != b.channel_output_power_upa2 ||
         a.noise_power_upa2 != b.noise_power_upa2) {
        throw std::runtime_error("Repeated waveform samples are not deterministic");
      }
    }
  }
  return EXIT_SUCCESS;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if(argc == 2 && std::string_view{argv[1]} == "--help") {
    std::cout << "Options: --modulation osdm|bpsk|bfsk --noise none|awgn|wenz "
                 "--psd-db N --wind N --shipping N --seed N --asset-root PATH\n";
    return EXIT_SUCCESS;
  }
  try {
    return ExecuteDemo(Parse(argc, argv));
  } catch(const std::exception& error) {
    std::cerr << "Channel 2 demo failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
