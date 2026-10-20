#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/application/repositories.hpp>
#include <ns3_factory/application/run_service.hpp>
#include <ns3_factory/worker/protocol.hpp>

#include "internal/acceptance_preset.hpp"
#include "internal/acceptance_run_executor.hpp"
#include "internal/environment_asset_repository.hpp"
#include "internal/waveform_phy.hpp"

namespace ns3_factory::worker::internal {

struct RunExecutionRequest final {
  application::RunId run_id;
  application::ScenarioId scenario_id;
  application::ExperimentId experiment_id;
  application::ResourceVersion definition_version;
  application::EnvironmentReference environment;
  application::AcceptanceProfile profile;
  std::size_t simulation_cycle_count;
  application::RxQualityMode quality_mode;
  double equivalent_noise_power_db_re_1upa2;
  std::uint64_t deterministic_seed;
  PhysicalLayerImplementation physical_layer;
  WaveformModulation waveform_modulation;
  WaveformNoiseModel waveform_noise;
  double waveform_noise_psd_db_re_1upa2_per_hz;
};

struct RunExecutionDefinition final {
  application::ScenarioDefinition scenario;
  application::ExperimentDefinition experiment;
  std::shared_ptr<const contracts::INodeTrajectory> trajectories;
  std::optional<phy::internal::OsdmConfig> osdm;
  std::shared_ptr<const phy::internal::RecordedWaveformChannel> recorded;
  std::map<contracts::NodeId,phy::internal::ModulationScheme> transmitter_modulations{};
  std::vector<assembly::internal::GeneratedFeatureReport> frozen_bearing_inputs{};
};

struct WorkerExecutionSnapshot final {
  application::RunRecord run;
  std::optional<application::RunResult> result;
  std::vector<application::RunEventRecord> events;
};

[[nodiscard]] inline auto MakeAcceptancePresetDefinition(
    const RunExecutionRequest& request)
    -> contracts::Result<RunExecutionDefinition> {
  if(request.physical_layer==PhysicalLayerImplementation::kChannel3Recorded)
    return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
        "Recorded mode requires an explicit immutable authored recording, not an Acceptance preset"});
  auto scenario = application::internal::MakeAcceptanceScenarioPreset(
      request.profile,
      request.scenario_id,
      request.definition_version,
      "Worker Acceptance Scenario",
      request.environment);
  if(!scenario) return std::unexpected(scenario.error());
  auto experiment = application::internal::MakeAcceptanceExperimentPreset(
      request.profile,
      request.experiment_id,
      request.definition_version,
      "Worker Acceptance Experiment",
      application::ScenarioReference{scenario->scenario_id(),
                                     scenario->version()},
      request.simulation_cycle_count,
      request.quality_mode,
      request.equivalent_noise_power_db_re_1upa2,
      request.deterministic_seed);
  if(!experiment) return std::unexpected(experiment.error());
  if(request.physical_layer ==
     PhysicalLayerImplementation::kChannel2Waveform) {
    auto waveform_experiment = application::ExperimentDefinition::Create(
        experiment->experiment_id(),
        experiment->version(),
        experiment->name(),
        experiment->scenario(),
        experiment->routing(),
        application::MacConfiguration{
            experiment->mac().mode,
            contracts::SimDuration::FromNanoseconds(6'000'000'000),
            experiment->mac().guard_interval},
        experiment->phy(),
        experiment->fusion(),
        experiment->network_update_interval_cycles(),
        experiment->simulation_cycle_count(),
        experiment->deterministic_seed());
    if(!waveform_experiment) {
      return std::unexpected(waveform_experiment.error());
    }
    experiment = std::move(waveform_experiment);
  }
  return RunExecutionDefinition{std::move(*scenario),
                                     std::move(*experiment), nullptr};
}

class SimulationWorker final {
 public:
  explicit SimulationWorker(
      const environment::internal::EnvironmentAssetRepository& environments,
      contracts::IPlanningEvidenceSink* evidence=nullptr)
      noexcept
      : environments_(environments),planning_evidence_(evidence) {}

  [[nodiscard]] auto last_execution() const noexcept
      -> const std::optional<WorkerExecutionSnapshot>& {
    return last_execution_;
  }

  [[nodiscard]] auto Run(const RunExecutionRequest& request,
                         IWorkerMessageSink& output,
                         std::optional<RunExecutionDefinition> authored = std::nullopt)
      -> contracts::Status {
    if(used_) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "A SimulationWorker executes exactly one Run"});
    }
    used_ = true;
    if(const auto emitted = output.Emit(WorkerStarted{request.run_id});
       !emitted) {
      return std::unexpected(emitted.error());
    }
    auto definitions = authored
        ? contracts::Result<RunExecutionDefinition>{std::move(*authored)}
        : MakeAcceptancePresetDefinition(request);
    if(!definitions) {
      return EmitFailure(request.run_id,
                         WorkerFailureCategory::kComposition,
                         definitions.error(),
                         std::nullopt,
                         output);
    }

    application::ScenarioRepository scenarios;
    application::ExperimentRepository experiments;
    application::RunRepository runs;
    application::InMemoryRunEventJournal events;
    if(const auto registered =
           scenarios.Register(definitions->scenario);
       !registered) {
      return EmitFailure(request.run_id,
                         WorkerFailureCategory::kComposition,
                         registered.error(),
                         std::nullopt,
                         output);
    }
    if(const auto registered =
           experiments.Register(definitions->experiment);
       !registered) {
      return EmitFailure(request.run_id,
                         WorkerFailureCategory::kComposition,
                         registered.error(),
                         std::nullopt,
                         output);
    }
    std::optional<application::internal::WaveformExecutionOptions>
        waveform_options;
    if(request.physical_layer != PhysicalLayerImplementation::kChannel1Abstract) {
      const auto& phy_config = definitions->experiment.phy();
      phy::internal::NoiseProfile noise = phy::internal::NoNoiseConfig{};
      if(request.waveform_noise == WaveformNoiseModel::kAwgn) {
        noise = phy::internal::AwgnNoiseConfig{
            request.waveform_noise_psd_db_re_1upa2_per_hz,
            request.deterministic_seed};
      }
      const auto modulation = request.waveform_modulation == WaveformModulation::kOsdm
          ? phy::internal::ModulationScheme::kOsdm :
          request.waveform_modulation == WaveformModulation::kBpsk
              ? phy::internal::ModulationScheme::kBpsk
              : phy::internal::ModulationScheme::kBfsk;
      waveform_options.emplace(application::internal::WaveformExecutionOptions{
          phy::internal::WaveformPhyConfig{
              {modulation,
               static_cast<double>(phy_config.bit_rate_bits_per_second) * 100.0,
               static_cast<double>(phy_config.bit_rate_bits_per_second),
               std::pow(10.0,
                        phy_config.source_level_db_re_1upa_at_1m / 20.0),
               phy_config.center_frequency_hz,
               30.0},
              phy_config.occupied_bandwidth_hz,
              noise,
              {phy::internal::ChannelEqualizationMode::kDisabled},
              true,
              phy::internal::WaveformFrameFormat::kPayloadWithCrc32,
              1'000'000},
          nullptr});
      if(request.waveform_modulation == WaveformModulation::kOsdm) {
        if(!definitions->osdm || !definitions->experiment.planning())
          return EmitFailure(request.run_id, WorkerFailureCategory::kComposition,
              {contracts::ErrorCode::kInvalidArgument, "OSDM requires explicit authored planning configuration"},
              std::nullopt, output);
        auto& c = waveform_options->phy;
        c.modulation.osdm = *definitions->osdm;
        const auto bits = phy::internal::BitsPerOsdmSymbol(c.modulation.osdm);
        if(!bits) return EmitFailure(request.run_id, WorkerFailureCategory::kComposition,
            bits.error(), std::nullopt, output);
        c.modulation.sample_rate_hz = static_cast<double>(phy_config.bit_rate_bits_per_second) / *bits;
        c.modulation.symbol_rate_baud = c.modulation.sample_rate_hz;
        c.equalization.mode = phy::internal::ChannelEqualizationMode::kTrainingSequenceMmse;
        c.ideal_phase_synchronization = false;
        if(c.modulation.sample_rate_hz > c.occupied_bandwidth_hz)
          return EmitFailure(request.run_id, WorkerFailureCategory::kComposition,
              {contracts::ErrorCode::kInvalidArgument, "OSDM sample rate exceeds occupied bandwidth"},
              std::nullopt, output);
      }
    }
    if(request.physical_layer==PhysicalLayerImplementation::kChannel3Recorded) {
      if(!definitions->recorded||!waveform_options||request.waveform_noise!=WaveformNoiseModel::kNone)
        return EmitFailure(request.run_id,WorkerFailureCategory::kComposition,
            {contracts::ErrorCode::kFailedPrecondition,"Recorded operator/noise binding is missing"},std::nullopt,output);
      waveform_options->phy.ideal_phase_synchronization=false;
      if(!definitions->transmitter_modulations.empty()) {
        for(const auto& node:definitions->scenario.nodes()) {
          if(!node.capability.can_transmit)continue;
          auto config=waveform_options->phy;
          const auto found=definitions->transmitter_modulations.find(node.node_id);
          if(found!=definitions->transmitter_modulations.end()) {
            config.modulation.scheme=found->second;
            if(found->second!=phy::internal::ModulationScheme::kOsdm) {
              // Explicit common-clock preset: BPSK/BFSK retain the platform's
              // 100 samples/symbol; their nominal rates are therefore fs/100.
              config.modulation.symbol_rate_baud=config.modulation.sample_rate_hz/100;
              config.equalization.mode=phy::internal::ChannelEqualizationMode::kDisabled;
            }
          }
          waveform_options->node_modems.emplace(node.node_id,std::move(config));
        }
      }
    } else if(definitions->recorded) return EmitFailure(request.run_id,WorkerFailureCategory::kComposition,
        {contracts::ErrorCode::kFailedPrecondition,"Recorded source cannot be used as mode A/B"},std::nullopt,output);
    application::internal::RunExecutionExecutor executor = definitions->trajectories
        ? application::internal::RunExecutionExecutor{
              environments_.get(), definitions->trajectories,
              std::move(waveform_options), definitions->recorded}
        : waveform_options
              ? application::internal::RunExecutionExecutor{
                    environments_.get(), std::move(*waveform_options)}
              : application::internal::RunExecutionExecutor{environments_.get()};
    executor.BindPlanningEvidence(planning_evidence_);
    executor.BindFrozenBearingInputs(definitions->frozen_bearing_inputs);
    application::RunService service{
        scenarios, experiments, runs, executor, events};
    const auto reference = application::ExperimentReference{
        definitions->experiment.experiment_id(),
        definitions->experiment.version()};
    const auto created = service.CreateRun(request.run_id, reference);
    if(!created) {
      return EmitFailure(request.run_id,
                         WorkerFailureCategory::kComposition,
                         created.error(),
                         std::nullopt,
                         output);
    }

    const auto executed = service.ExecuteRun(request.run_id);
    const auto record = service.GetRun(request.run_id);
    if(!record) {
      return EmitFailure(request.run_id,
                         WorkerFailureCategory::kSimulation,
                         record.error(),
                         std::nullopt,
                         output);
    }
    auto collected_events = CollectEvents(service, request.run_id);
    if(!collected_events) {
      return EmitFailure(request.run_id,
                         WorkerFailureCategory::kSimulation,
                         collected_events.error(),
                         *record,
                         output);
    }
    if(!executed) {
      last_execution_.emplace(
          WorkerExecutionSnapshot{*record,
                                  std::nullopt,
                                  *collected_events});
      if(const auto emitted = EmitEvents(*collected_events, output);
         !emitted) {
        return emitted;
      }
      return EmitFailure(request.run_id,
                         WorkerFailureCategory::kSimulation,
                         executed.error(),
                         *record,
                         output);
    }
    const auto result = service.GetResult(request.run_id);
    if(!result) {
      last_execution_.emplace(
          WorkerExecutionSnapshot{*record,
                                  std::nullopt,
                                  *collected_events});
      if(const auto emitted = EmitEvents(*collected_events, output);
         !emitted) {
        return emitted;
      }
      return EmitFailure(request.run_id,
                         WorkerFailureCategory::kSimulation,
                         result.error(),
                         *record,
                         output);
    }
    last_execution_.emplace(
        WorkerExecutionSnapshot{*record, *result, *collected_events});
    if(const auto emitted = EmitEvents(*collected_events, output);
       !emitted) {
      return emitted;
    }
    const auto emitted = output.Emit(WorkerCompleted{*record, *result});
    if(!emitted) return std::unexpected(emitted.error());
    return {};
  }

 private:
  [[nodiscard]] static auto CollectEvents(
      const application::RunService& service,
      const application::RunId& run_id)
      -> contracts::Result<std::vector<application::RunEventRecord>> {
    std::vector<application::RunEventRecord> events;
    auto cursor = application::RunEventSequence::BeforeFirst();
    while(true) {
      const auto page = service.ReadEvents(
          run_id, cursor, application::kMaximumRunEventReadLimit);
      if(!page) return std::unexpected(page.error());
      if(page->empty()) return events;
      for(const auto& record : *page) {
        events.push_back(record);
        cursor = record.sequence;
      }
    }
  }

  [[nodiscard]] static auto EmitEvents(
      const std::vector<application::RunEventRecord>& events,
      IWorkerMessageSink& output) -> contracts::Status {
    for(const auto& record : events) {
      const auto emitted = output.Emit(WorkerRunEvent{record});
      if(!emitted) return std::unexpected(emitted.error());
    }
    return {};
  }

  [[nodiscard]] static auto EmitFailure(
      const application::RunId& run_id,
      WorkerFailureCategory category,
      contracts::Error error,
      std::optional<application::RunRecord> run,
      IWorkerMessageSink& output) -> contracts::Status {
    const auto emitted = output.Emit(
        WorkerFailed{run_id, category, error, std::move(run)});
    if(!emitted) return std::unexpected(emitted.error());
    return std::unexpected(std::move(error));
  }

  std::reference_wrapper<
      const environment::internal::EnvironmentAssetRepository>
      environments_;
  std::optional<WorkerExecutionSnapshot> last_execution_;
  bool used_{false};
  contracts::IPlanningEvidenceSink* planning_evidence_{};
};

using AcceptanceWorkerRequest = RunExecutionRequest;
using AcceptanceWorkerDefinitions = RunExecutionDefinition;
[[nodiscard]] inline auto MakeAcceptanceWorkerDefinitions(
    const RunExecutionRequest& request)
    -> contracts::Result<RunExecutionDefinition> {
  return MakeAcceptancePresetDefinition(request);
}

}  // namespace ns3_factory::worker::internal
