#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "channel_processor.hpp"
#include "modulation.hpp"
#include "noise_generator.hpp"
#include "waveform.hpp"

namespace ns3_factory::phy::internal {

// Standalone/offline waveform composition. This is deliberately not an
// ITxPhy, IChannelFieldProvider, INoiseFieldProvider, or IRxPhy runtime
// implementation; production provider adapters retain those frozen roles.

struct WaveformPipelineConfig final {
  ModulationConfig modulation;
  std::vector<MultipathTap> channel_taps;
  NoiseProfile noise;
  bool ideal_phase_synchronization;

  auto operator==(const WaveformPipelineConfig&) const -> bool = default;
};

class WaveformPipelineResult final {
 public:
  WaveformPipelineResult(WaveformBuffer transmitted,
                         WaveformBuffer channel_output,
                         WaveformBuffer noisy_received,
                         BitFrame recovered,
                         std::size_t bit_error_count)
      : transmitted_(std::move(transmitted)),
        channel_output_(std::move(channel_output)),
        noisy_received_(std::move(noisy_received)),
        recovered_(std::move(recovered)),
        bit_error_count_(bit_error_count) {}

  [[nodiscard]] auto transmitted() const noexcept
      -> const WaveformBuffer& {
    return transmitted_;
  }

  [[nodiscard]] auto channel_output() const noexcept
      -> const WaveformBuffer& {
    return channel_output_;
  }

  [[nodiscard]] auto noisy_received() const noexcept
      -> const WaveformBuffer& {
    return noisy_received_;
  }

  [[nodiscard]] auto recovered() const noexcept -> const BitFrame& {
    return recovered_;
  }

  [[nodiscard]] constexpr auto bit_error_count() const noexcept
      -> std::size_t {
    return bit_error_count_;
  }

  [[nodiscard]] auto bit_error_rate() const noexcept -> double {
    return static_cast<double>(bit_error_count_) /
           static_cast<double>(recovered_.bit_count());
  }

  [[nodiscard]] constexpr auto packet_error() const noexcept -> bool {
    return bit_error_count_ != 0U;
  }

 private:
  WaveformBuffer transmitted_;
  WaveformBuffer channel_output_;
  WaveformBuffer noisy_received_;
  BitFrame recovered_;
  std::size_t bit_error_count_;
};

[[nodiscard]] inline auto RunWaveformPipeline(
    const BitFrame& source,
    const WaveformPipelineConfig& config)
    -> Result<WaveformPipelineResult> {
  const auto modulator = CreateModulator(config.modulation.scheme);
  if(!modulator) {
    return std::unexpected(modulator.error());
  }
  const auto demodulator = CreateDemodulator(config.modulation.scheme);
  if(!demodulator) {
    return std::unexpected(demodulator.error());
  }

  auto transmitted =
      (*modulator)->Modulate(source, config.modulation);
  if(!transmitted) {
    return std::unexpected(transmitted.error());
  }
  auto channel_output =
      ApplyMultipath(*transmitted, config.channel_taps);
  if(!channel_output) {
    return std::unexpected(channel_output.error());
  }
  auto noise = GenerateNoise(config.noise,
                             channel_output->sample_count(),
                             channel_output->sample_rate_hz(),
                             config.modulation.acoustic_carrier_hz);
  if(!noise) {
    return std::unexpected(noise.error());
  }
  auto noisy_received = AddWaveforms(*channel_output, *noise);
  if(!noisy_received) {
    return std::unexpected(noisy_received.error());
  }

  if(config.ideal_phase_synchronization) {
    const auto phase = IdealReferencePhase(config.channel_taps);
    if(!phase) {
      return std::unexpected(phase.error());
    }
    noisy_received = RotatePhase(*noisy_received, -*phase);
    if(!noisy_received) {
      return std::unexpected(noisy_received.error());
    }
  }

  auto recovered = (*demodulator)->Demodulate(
      *noisy_received, source.bit_count(), config.modulation);
  if(!recovered) {
    return std::unexpected(recovered.error());
  }

  std::size_t bit_error_count = 0U;
  for(std::size_t index = 0; index < source.bit_count(); ++index) {
    if(source.bits()[index] != recovered->bits()[index]) {
      ++bit_error_count;
    }
  }
  return WaveformPipelineResult{std::move(*transmitted),
                                std::move(*channel_output),
                                std::move(*noisy_received),
                                std::move(*recovered),
                                bit_error_count};
}

}  // namespace ns3_factory::phy::internal
