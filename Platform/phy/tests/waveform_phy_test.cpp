#include <iostream>
#include <string_view>

#include "internal/scalar_ber_rx_phy.hpp"
#include "internal/waveform_phy.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::phy::internal;

namespace {
auto Check(bool value, std::string_view label) -> bool {
  if(!value) std::cerr << "FAILED: " << label << '\n';
  return value;
}

struct Observer : IWaveformReceptionObserver {
  std::vector<WaveformReceptionSummary> summaries;
  void Observe(const WaveformReceptionSummary& s) noexcept override {
    summaries.push_back(s);
  }
};

auto Config(ModulationScheme scheme = ModulationScheme::kBpsk)
    -> WaveformPhyConfig {
  return {{scheme, 6'000.0, 60.0, 1'000.0, 25'000.0, 30.0},
          4'000.0, NoNoiseConfig{}, true,
          WaveformFrameFormat::kPayloadWithCrc32, 1'000'000U};
}

auto Request(const TxEmission& emission, double gain = 0.5,
             double phase = 0.25, double tl = 900.0)
    -> Result<RxDecodeRequest> {
  auto path = PropagationPath::Create(SimDuration::Zero(), gain, phase);
  if(!path) return std::unexpected(path.error());
  auto response = ChannelFieldResponse::Create(
      emission.transmission_id(), NodeId{2}, tl,
      SimDuration::FromNanoseconds(500'000'000), {*path});
  if(!response) return std::unexpected(response.error());
  auto signal = ReceivedSignal::Create(emission, *response);
  if(!signal) return std::unexpected(signal.error());
  auto window = ReceiverWindow::Create(NodeId{2}, *signal, {});
  if(!window) return std::unexpected(window.error());
  auto noise = NoiseObservation::Create(
      NodeId{2}, signal->first_arrival_at(), signal->last_effect_end_at(),
      signal->lower_frequency_hz(), signal->upper_frequency_hz(), 45.0);
  if(!noise) return std::unexpected(noise.error());
  return RxDecodeRequest::Create(*window, *noise);
}

auto RoundTrip(ModulationScheme scheme) -> bool {
  TransmissionWaveformStore store;
  const auto config = Config(scheme);
  auto tx = WaveformTxPhy::Create(config, store);
  Observer observer;
  auto rx = WaveformRxPhy::Create(config, store, &observer);
  DigitalPacket packet{PacketId{10}, NodeId{1},
                       UnicastDestination{NodeId{2}},
                       {std::byte{0xa5}, std::byte{0x3c}}};
  const TxEncodeRequest encode{TransmissionId{20}, NodeId{1},
                               UnicastTransmissionTarget{NodeId{2}},
                               SimTime::Zero()};
  if(!tx || !rx) return Check(false, "create providers");
  auto emission = tx->Encode(packet, encode);
  if(!emission) return Check(false, "encode");
  auto request = Request(*emission);
  if(!request) return Check(false, "receive request");
  auto decoded = rx->Decode(*request);
  auto repeated = rx->Decode(*request);
  auto artifact = store.Find(TransmissionId{20});
  auto duplicate = tx->Encode(packet, encode);
  if(!Check(decoded && repeated && *decoded == *repeated, "determinism") ||
     !Check(decoded->recovered_payload() &&
                *decoded->recovered_payload() == packet.payload,
            "receiver recovers payload") ||
     !Check(!duplicate && store.size() == 1, "single publish") ||
     !Check(observer.summaries.size() == 2 &&
                observer.summaries[0].bit_error_count == 0 &&
                observer.summaries[0].noise_power_upa2 == 0.0,
            "true BER and no noise") ||
     !Check(emission->duration().nanoseconds() == 800'000'000,
            "CRC overhead in duration")) return false;

  // Replace only the evaluation truth. Decisions/payload must stay identical.
  TransmissionWaveformStore altered_store;
  auto wrong_truth = BitFrame::Create(std::vector<std::uint8_t>(16, 0));
  if(!artifact || !wrong_truth) return false;
  auto published = altered_store.Publish(TransmissionWaveformArtifact{
      TransmissionId{20}, PacketId{10}, *wrong_truth,
      (*artifact)->transmitted_waveform()});
  Observer altered_observer;
  auto altered_rx = WaveformRxPhy::Create(config, altered_store,
                                           &altered_observer);
  if(!published || !altered_rx) return false;
  auto altered = altered_rx->Decode(*request);
  // Compare every decision/identity/payload field. The newly formalized
  // post-decision comparison must change, just like observer BER already did.
  if(!Check(altered && altered->transmission_id()==decoded->transmission_id() &&
                altered->packet_id()==decoded->packet_id() &&
                altered->receiver_node_id()==decoded->receiver_node_id() &&
                altered->outcome()==decoded->outcome() &&
                altered->recovered_payload()==decoded->recovered_payload() &&
                altered->quality_evidence()==decoded->quality_evidence(), "truth cannot decide payload") ||
     !Check(altered->bit_comparison() && decoded->bit_comparison() &&
                altered->bit_comparison()->crc_ok==decoded->bit_comparison()->crc_ok &&
                altered->bit_comparison()->compared_payload_bits==decoded->bit_comparison()->compared_payload_bits &&
                altered->bit_comparison()->erroneous_payload_bits>0 &&
                decoded->bit_comparison()->erroneous_payload_bits==0,
            "formal BER is post-decision; truth does not affect CRC") ||
     !Check(altered_observer.summaries[0].bit_error_count > 0,
            "truth only affects observed BER")) return false;

  // Physically corrupt one symbol in X. CRC must reject without consulting truth.
  std::vector<WaveformBuffer::Sample> samples{
      (*artifact)->transmitted_waveform().samples().begin(),
      (*artifact)->transmitted_waveform().samples().end()};
  for(std::size_t i = 0; i < 100; ++i) {
    if(scheme == ModulationScheme::kBpsk) samples[i] = -samples[i];
    else samples[i] = std::conj(samples[i]);
  }
  auto damaged_waveform = WaveformBuffer::Create(6'000.0, std::move(samples));
  TransmissionWaveformStore damaged_store;
  if(!damaged_waveform) return false;
  auto damaged_publish = damaged_store.Publish(TransmissionWaveformArtifact{
      TransmissionId{20}, PacketId{10}, (*artifact)->original_frame(),
      std::move(*damaged_waveform)});
  auto damaged_rx = WaveformRxPhy::Create(config, damaged_store);
  if(!damaged_publish || !damaged_rx) return false;
  auto damaged = damaged_rx->Decode(*request);
  if(!Check(damaged && damaged->outcome() == DecodeOutcome::kNotDecoded &&
                !damaged->recovered_payload(), "CRC rejects physical corruption"))
    return false;
  auto decorator = ScalarBerRxPhyDecorator::Create(*rx, 60);
  if(!decorator) return false;
  auto decorated = decorator->Decode(*request);
  if(!Check(decorated && decorated->recovered_payload() ==
                             decoded->recovered_payload(),
            "quality decorator preserves decoded payload")) return false;
  store.ReleaseCycleArtifacts();
  return Check(!rx->Decode(*request), "expired X is an explicit error");
}

auto InvalidProfiles() -> bool {
  TransmissionWaveformStore store;
  auto config = Config(ModulationScheme::kQpsk);
  if(!Check(!WaveformTxPhy::Create(config, store), "reserved QPSK is explicit")) return false;
  config = Config();
  config.maximum_samples = 1;
  if(!Check(!WaveformTxPhy::Create(config, store), "small sample limit rejected")) return false;
  config = Config();
  config.modulation.sample_rate_hz = 1e300;
  config.modulation.symbol_rate_baud = 1e-300;
  return Check(!WaveformTxPhy::Create(config, store),
               "nonrepresentable samples per symbol rejected before allocation");
}

auto NoiseAfterChannel() -> bool {
  const auto config = Config().modulation;
  auto bits = BitFrame::Create({0, 1, 0, 1});
  auto modulator = CreateModulator(config.scheme);
  if(!bits || !modulator) return false;
  auto x = (*modulator)->Modulate(*bits, config);
  if(!x) return false;
  const std::vector<MultipathTap> taps{{0.0, {0.02, 0.0}}};
  for(const NoiseProfile& noise : {
          NoiseProfile{AwgnNoiseConfig{10.0, 52}},
          NoiseProfile{WenzNoiseConfig{0.5, 4.0, 52}}}) {
    auto out = ReceiveWaveform(*x, 4, config, taps, noise, false);
    auto n = GenerateNoise(noise, x->sample_count(), x->sample_rate_hz(),
                            config.acoustic_carrier_hz);
    if(!out || !n) return false;
    auto expected = AddWaveforms(out->channel_output, *n);
    if(!Check(expected && *expected == out->received,
              "Y=H(X)+N (noise not attenuated by H)")) return false;
  }
  auto first = PropagationPath::Create(SimDuration::Zero(), 1.0, 0.0);
  auto second = PropagationPath::Create(
      SimDuration::FromNanoseconds(20'000), 1.0, 0.0);
  if(!first || !second) return false;
  auto response = ChannelFieldResponse::Create(
      TransmissionId{1}, NodeId{2}, 100.0, SimDuration::Zero(),
      {*first, *second});
  if(!response) return false;
  auto taps_with_carrier = BuildBasebandMultipathTaps(*response, 25'000);
  return Check(taps_with_carrier &&
                   std::abs((*taps_with_carrier)[1].pressure_gain +
                            std::complex<double>{1.0, 0.0}) < 1e-10,
               "half carrier period gives opposite path phase");
}
}  // namespace

int main() {
  const std::string_view check = "123456789";
  const auto bytes = std::as_bytes(std::span{check.data(), check.size()});
  if(!Check(WaveformCrc32(bytes) == 0xcbf43926U, "CRC known vector") ||
     !RoundTrip(ModulationScheme::kBpsk) ||
     !RoundTrip(ModulationScheme::kBfsk) || !NoiseAfterChannel() ||
     !InvalidProfiles()) return 1;
  std::cout << "Waveform providers, truth isolation and post-channel noise passed\n";
}
