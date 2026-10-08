#pragma once

#include "channel_equalizer.hpp"
#include "channel_processor.hpp"
#include "modulation.hpp"
#include "noise_generator.hpp"

namespace ns3_factory::phy::internal {

struct WaveformReceiverOutput final {
  WaveformBuffer channel_output;
  WaveformBuffer received;
  BitFrame recovered;
};

// Pure receive-side operation. X is already generated and is never modulated
// again here. Receive length is framing metadata, not a source-bit oracle.
[[nodiscard]] inline auto ReceiveWaveform(
    const WaveformBuffer& transmitted,
    std::size_t frame_bit_count,
    const ModulationConfig& modulation,
    std::span<const MultipathTap> taps,
    const NoiseProfile& noise_profile,
    const ChannelEqualizationConfig& equalization,
    bool ideal_phase_synchronization) -> Result<WaveformReceiverOutput> {
  auto channel_output = ApplyMultipath(transmitted, taps);
  if(!channel_output) return std::unexpected(channel_output.error());
  auto noise = GenerateNoise(noise_profile, channel_output->sample_count(),
                             channel_output->sample_rate_hz(),
                             modulation.acoustic_carrier_hz);
  if(!noise) return std::unexpected(noise.error());
  // Ambient noise is added at the receiver, AFTER propagation.
  auto received = AddWaveforms(*channel_output, *noise);
  if(!received) return std::unexpected(received.error());
  auto decoder_input = *received;
  if(equalization.mode != ChannelEqualizationMode::kDisabled) {
    const auto transmitted_power =
        modulation.symbol_amplitude_upa * modulation.symbol_amplitude_upa;
    const auto noise_power = channel_equalizer_detail::MeanPower(*noise);
    auto equalized = EqualizeKnownChannel(
        decoder_input, taps, transmitted.sample_count(), transmitted_power,
        noise_power, equalization);
    if(!equalized) return std::unexpected(equalized.error());
    decoder_input = std::move(*equalized);
  } else if(ideal_phase_synchronization) {
    auto phase = IdealReferencePhase(taps);
    if(!phase) return std::unexpected(phase.error());
    auto synchronized = RotatePhase(decoder_input, -*phase);
    if(!synchronized) return std::unexpected(synchronized.error());
    decoder_input = std::move(*synchronized);
  }
  auto demodulator = CreateDemodulator(modulation.scheme);
  if(!demodulator) return std::unexpected(demodulator.error());
  auto recovered = (*demodulator)->Demodulate(
      decoder_input, frame_bit_count, modulation);
  if(!recovered) return std::unexpected(recovered.error());
  return WaveformReceiverOutput{std::move(*channel_output),
                                std::move(*received), std::move(*recovered)};
}

}  // namespace ns3_factory::phy::internal
