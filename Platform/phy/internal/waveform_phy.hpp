#pragma once

#include <functional>
#include <limits>
#include <optional>

#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "packet_bit_adapter.hpp"
#include "transmission_waveform_store.hpp"
#include "waveform_frame_codec.hpp"
#include "waveform_receiver.hpp"

namespace ns3_factory::phy::internal {

enum class WaveformFrameFormat { kPayloadWithCrc32 };

struct WaveformPhyConfig final {
  ModulationConfig modulation;
  double occupied_bandwidth_hz;
  NoiseProfile noise;
  ChannelEqualizationConfig equalization;
  bool ideal_phase_synchronization;
  WaveformFrameFormat frame_format;
  std::size_t maximum_samples;
};

[[nodiscard]] inline auto ValidateWaveformPhyConfig(const WaveformPhyConfig& c)
    -> contracts::Status {
  auto minimum_samples = ModulatedSampleCount(40U, c.modulation);
  if(!minimum_samples) return std::unexpected(minimum_samples.error());
  auto modem = CreateModulator(c.modulation.scheme);
  if(!modem) return std::unexpected(modem.error());
  if(!std::isfinite(c.occupied_bandwidth_hz) ||
     c.occupied_bandwidth_hz <= 0.0 ||
     c.occupied_bandwidth_hz / 2.0 >= c.modulation.acoustic_carrier_hz ||
     (c.equalization.mode != ChannelEqualizationMode::kDisabled &&
      c.equalization.mode !=
          ChannelEqualizationMode::kIdealFrequencyDomainMmse) ||
     c.frame_format != WaveformFrameFormat::kPayloadWithCrc32 ||
     c.maximum_samples < *minimum_samples) {
    return std::unexpected(Error{ErrorCode::kInvalidArgument,
                                 "Invalid waveform band, equalizer, frame or "
                                 "sample limit"});
  }
  auto noise = GenerateNoise(c.noise, 1, c.modulation.sample_rate_hz,
                             c.modulation.acoustic_carrier_hz);
  if(!noise) return std::unexpected(noise.error());
  return {};
}

// A non-causal observation after demodulation. BER counts payload bits (CRC
// excluded). The observer must outlive the Rx and may not affect its decision.
struct WaveformReceptionSummary final {
  contracts::TransmissionId transmission_id;
  contracts::NodeId receiver_node_id;
  std::size_t path_count;
  std::size_t payload_bit_count;
  std::size_t bit_error_count;
  bool crc_ok;
  std::size_t received_sample_count;
  double channel_output_power_upa2;
  double noise_power_upa2;
};

class IWaveformReceptionObserver {
 public:
  virtual ~IWaveformReceptionObserver() = default;
  virtual void Observe(const WaveformReceptionSummary&) noexcept = 0;
};

class WaveformTxPhy final : public contracts::ITxPhy {
 public:
  [[nodiscard]] static auto Create(WaveformPhyConfig config,
                                  ITransmissionWaveformArtifacts& artifacts)
      -> Result<WaveformTxPhy> {
    auto valid = ValidateWaveformPhyConfig(config);
    if(!valid) return std::unexpected(valid.error());
    return WaveformTxPhy{std::move(config), artifacts};
  }

  [[nodiscard]] auto Encode(const contracts::DigitalPacket& packet,
                            const contracts::TxEncodeRequest& request) const
      -> Result<contracts::TxEmission> override {
    auto source = ExtractPayloadBitFrame(packet);
    if(!source) return std::unexpected(source.error());
    auto framed_payload = AddWaveformCrc(packet.payload);
    if(!framed_payload) return std::unexpected(framed_payload.error());
    auto framed_packet = packet;
    framed_packet.payload = std::move(*framed_payload);
    auto frame = ExtractPayloadBitFrame(framed_packet);
    if(!frame) return std::unexpected(frame.error());
    const auto expected_samples =
        ModulatedSampleCount(frame->bit_count(), config_.modulation);
    if(!expected_samples || *expected_samples > config_.maximum_samples) {
      return std::unexpected(
          Error{ErrorCode::kOutOfRange,
                "Waveform frame exceeds sample limit"});
    }
    auto modulator = CreateModulator(config_.modulation.scheme);
    if(!modulator) return std::unexpected(modulator.error());
    auto waveform = (*modulator)->Modulate(*frame, config_.modulation);
    if(!waveform) return std::unexpected(waveform.error());
    const auto duration_ns = std::ceil(
        static_cast<long double>(waveform->sample_count()) * 1.0e9L /
        static_cast<long double>(waveform->sample_rate_hz()));
    if(!std::isfinite(duration_ns) || duration_ns <= 0.0L ||
       duration_ns > std::numeric_limits<contracts::NanosecondCount>::max()) {
      return std::unexpected(Error{ErrorCode::kOverflow,
                                   "Waveform duration is out of range"});
    }
    auto emission = contracts::TxEmission::Create(
        request.transmission_id, packet.packet_id, request.sender_node_id,
        request.started_at,
        contracts::SimDuration::FromNanoseconds(
            static_cast<contracts::NanosecondCount>(duration_ns)),
        config_.modulation.acoustic_carrier_hz, config_.occupied_bandwidth_hz,
        20.0 * std::log10(config_.modulation.symbol_amplitude_upa));
    if(!emission) return std::unexpected(emission.error());
    auto published = artifacts_.get().Publish(TransmissionWaveformArtifact{
        request.transmission_id, packet.packet_id, std::move(*source),
        frame->bit_count(), std::move(*waveform)});
    if(!published) return std::unexpected(published.error());
    return emission;
  }

 private:
  WaveformTxPhy(WaveformPhyConfig c, ITransmissionWaveformArtifacts& a)
      : config_(std::move(c)), artifacts_(a) {}
  WaveformPhyConfig config_;
  std::reference_wrapper<ITransmissionWaveformArtifacts> artifacts_;
};

class WaveformRxPhy final : public contracts::IRxPhy {
 public:
  [[nodiscard]] static auto Create(
      WaveformPhyConfig config, const ITransmissionWaveformArtifacts& artifacts,
      IWaveformReceptionObserver* observer = nullptr) -> Result<WaveformRxPhy> {
    auto valid = ValidateWaveformPhyConfig(config);
    if(!valid) return std::unexpected(valid.error());
    return WaveformRxPhy{std::move(config), artifacts, observer};
  }

  [[nodiscard]] auto Decode(const contracts::RxDecodeRequest& request) const
      -> Result<contracts::RxDecodeResult> override {
    const auto& signal = request.receiver_window().desired_signal();
    if(!request.receiver_window().overlapping_signals().empty()) {
      return std::unexpected(Error{
          ErrorCode::kUnsupported,
          "Waveform interference is not implemented; use isolated slots"});
    }
    auto artifact = artifacts_.get().Find(signal.transmission_id());
    if(!artifact) return std::unexpected(artifact.error());
    if((*artifact)->packet_id() != signal.emission().packet_id()) {
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,
                                   "Waveform packet identity mismatch"});
    }
    const auto& transmitted = (*artifact)->transmitted_waveform();
    const auto frame_bit_count = (*artifact)->framed_bit_count();
    const auto expected_samples =
        ModulatedSampleCount(frame_bit_count, config_.modulation);
    if(!expected_samples) return std::unexpected(expected_samples.error());
    if(transmitted.sample_rate_hz() != config_.modulation.sample_rate_hz ||
       signal.emission().center_frequency_hz() !=
           config_.modulation.acoustic_carrier_hz ||
       transmitted.sample_count() != *expected_samples ||
       transmitted.sample_count() > config_.maximum_samples) {
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,
                                   "Waveform receive configuration mismatch"});
    }
    // Framed length is immutable PHY metadata, not source-bit content.
    if(frame_bit_count <= kWaveformCrcBytes * 8U || frame_bit_count % 8U != 0) {
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,
                                   "Invalid waveform frame length"});
    }
    auto taps = BuildBasebandMultipathTaps(
        signal.channel_response(), config_.modulation.acoustic_carrier_hz);
    if(!taps) return std::unexpected(taps.error());
    for(const auto& tap : *taps) {
      const auto tail = std::ceil(
          tap.excess_delay_seconds * transmitted.sample_rate_hz());
      if(!std::isfinite(tail) ||
         tail > static_cast<double>(
             config_.maximum_samples - transmitted.sample_count())) {
        return std::unexpected(Error{ErrorCode::kOutOfRange,
                                     "Channel tail exceeds sample limit"});
      }
    }
    // Each transmission/receiver has a deterministic independent noise stream.
    auto noise = config_.noise;
    std::visit([&signal](auto& value) {
      if constexpr(requires { value.deterministic_seed; }) {
        auto mix = [](std::uint64_t v) {
          v += 0x9e3779b97f4a7c15ULL;
          v = (v ^ (v >> 30U)) * 0xbf58476d1ce4e5b9ULL;
          v = (v ^ (v >> 27U)) * 0x94d049bb133111ebULL;
          return v ^ (v >> 31U);
        };
        value.deterministic_seed =
            mix(value.deterministic_seed ^ mix(signal.transmission_id().value()) ^
                mix(signal.receiver_node_id().value() + 0x517cc1b727220a95ULL));
      }
    }, noise);
    auto received = ReceiveWaveform(
        transmitted, frame_bit_count, config_.modulation, *taps, noise,
        config_.equalization, config_.ideal_phase_synchronization);
    if(!received) return std::unexpected(received.error());
    auto frame = RecoverPayloadBytes(received->recovered);
    if(!frame) return std::unexpected(frame.error());
    const bool crc_ok = CheckWaveformCrc(*frame);
    auto result = contracts::RxDecodeResult::Create(
        signal.transmission_id(), signal.emission().packet_id(),
        signal.receiver_node_id(), crc_ok ? contracts::DecodeOutcome::kDecoded
                                         : contracts::DecodeOutcome::kNotDecoded);
    if(!result) return std::unexpected(result.error());
    if(crc_ok) {
      frame->resize(frame->size() - kWaveformCrcBytes);
      result = result->WithRecoveredPayload(std::move(*frame));
      if(!result) return std::unexpected(result.error());
    }
    // Truth is read only AFTER all receiver decisions and recovered bytes.
    if(observer_) {
      const auto& truth = (*artifact)->original_frame();
      const auto payload_bits = frame_bit_count - 8U * kWaveformCrcBytes;
      if(truth.bit_count() == payload_bits) {
        std::size_t errors = 0;
        for(std::size_t i = 0; i < payload_bits; ++i) {
          errors += truth.bits()[i] != received->recovered.bits()[i];
        }
        double signal_power = 0.0;
        double noise_power = 0.0;
        for(std::size_t i = 0; i < received->received.sample_count(); ++i) {
          signal_power += std::norm(received->channel_output.samples()[i]);
          noise_power += std::norm(received->received.samples()[i] -
                                   received->channel_output.samples()[i]);
        }
        const auto count =
            static_cast<double>(received->received.sample_count());
        observer_->Observe({signal.transmission_id(), signal.receiver_node_id(),
                            taps->size(), payload_bits, errors, crc_ok,
                            received->received.sample_count(),
                            signal_power / count, noise_power / count});
      }
    }
    return result;
  }

 private:
  WaveformRxPhy(WaveformPhyConfig c, const ITransmissionWaveformArtifacts& a,
                IWaveformReceptionObserver* o)
      : config_(std::move(c)), artifacts_(a), observer_(o) {}
  WaveformPhyConfig config_;
  std::reference_wrapper<const ITransmissionWaveformArtifacts> artifacts_;
  IWaveformReceptionObserver* observer_;
};

}  // namespace ns3_factory::phy::internal
