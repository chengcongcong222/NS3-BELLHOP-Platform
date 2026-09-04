#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/packet.hpp>

#include "internal/channel_processor.hpp"
#include "internal/modulation.hpp"
#include "internal/packet_bit_adapter.hpp"
#include "internal/transmission_waveform_store.hpp"

namespace {

using namespace ns3_factory;
using contracts::ChannelFieldResponse;
using contracts::DigitalPacket;
using contracts::NodeId;
using contracts::PacketId;
using contracts::PropagationPath;
using contracts::SimDuration;
using contracts::TransmissionId;
using contracts::UnicastDestination;
using phy::internal::ApplyMultipath;
using phy::internal::BuildMultipathTaps;
using phy::internal::CreateDemodulator;
using phy::internal::CreateModulator;
using phy::internal::ExtractPayloadBitFrame;
using phy::internal::IdealReferencePhase;
using phy::internal::ModulationConfig;
using phy::internal::ModulationScheme;
using phy::internal::RecoverPayloadBytes;
using phy::internal::RotatePhase;
using phy::internal::TransmissionWaveformArtifact;
using phy::internal::TransmissionWaveformStore;

auto Check(bool condition, std::string_view message) -> bool {
  if(!condition) std::cerr << "FAILED: " << message << '\n';
  return condition;
}

auto ProcessReceiver(
    const phy::internal::SharedTransmissionWaveformArtifact& artifact,
    const ChannelFieldResponse& channel,
    const ModulationConfig& modulation) -> bool {
  const auto taps = BuildMultipathTaps(channel);
  if(!taps) return Check(false, "Bellhop-derived paths become CIR taps");
  const auto propagated =
      ApplyMultipath(artifact->transmitted_waveform(), *taps);
  const auto reference_phase = IdealReferencePhase(*taps);
  if(!propagated || !reference_phase) {
    return Check(false, "X passes through receiver-specific H");
  }
  const auto synchronized = RotatePhase(*propagated, -*reference_phase);
  const auto demodulator = CreateDemodulator(modulation.scheme);
  if(!synchronized || !demodulator) {
    return Check(false, "receiver phase synchronization");
  }
  const auto recovered = (*demodulator)->Demodulate(
      *synchronized, artifact->original_frame().bit_count(), modulation);
  if(!recovered) return Check(false, "waveform demodulation");
  const auto bit_errors = std::mismatch(
      artifact->original_frame().bits().begin(),
      artifact->original_frame().bits().end(),
      recovered->bits().begin());
  const auto payload = RecoverPayloadBytes(*recovered);
  const auto original_payload =
      RecoverPayloadBytes(artifact->original_frame());
  return Check(bit_errors.first == artifact->original_frame().bits().end(),
               "waveform BER is zero by original/demodulated comparison") &&
         Check(payload && original_payload && *payload == *original_payload,
               "demodulated bits recover the original payload");
}

auto TestSharedTransmissionArtifactAndE2eEndpoint() -> bool {
  const DigitalPacket packet{PacketId{44},
                             NodeId{1},
                             UnicastDestination{NodeId{9}},
                             {std::byte{0xA5}, std::byte{0x3C}}};
  const ModulationConfig modulation{ModulationScheme::kBpsk,
                                    8'000.0,
                                    1'000.0,
                                    1.0,
                                    12'000.0,
                                    0.0};
  auto frame = ExtractPayloadBitFrame(packet);
  auto modulator = CreateModulator(modulation.scheme);
  if(!frame || !modulator) return Check(false, "Tx setup");
  auto waveform = (*modulator)->Modulate(*frame, modulation);
  if(!waveform) return Check(false, "DigitalPacket is modulated once");

  TransmissionWaveformStore store;
  auto published = store.Publish(TransmissionWaveformArtifact{
      TransmissionId{77}, packet.packet_id, *frame, *waveform});
  auto receiver_one_artifact = store.Find(TransmissionId{77});
  auto receiver_two_artifact = store.Find(TransmissionId{77});
  if(!published || !receiver_one_artifact || !receiver_two_artifact) {
    return Check(false, "published X is resolvable by every receiver");
  }

  auto first_path = PropagationPath::Create(SimDuration::Zero(), 1.0, 0.0);
  auto second_path = PropagationPath::Create(SimDuration::Zero(), 0.8, 0.3);
  if(!first_path || !second_path) return Check(false, "path setup");
  auto receiver_one_channel = ChannelFieldResponse::Create(
      TransmissionId{77}, NodeId{2}, 80.0,
      SimDuration::FromNanoseconds(2'000'000), {*first_path});
  auto receiver_two_channel = ChannelFieldResponse::Create(
      TransmissionId{77}, NodeId{3}, 120.0,
      SimDuration::FromNanoseconds(9'000'000), {*second_path});
  if(!receiver_one_channel || !receiver_two_channel) {
    return Check(false, "receiver-specific channel setup");
  }

  const auto same_x = published->get() == receiver_one_artifact->get() &&
                      published->get() == receiver_two_artifact->get();
  const auto receiver_one_ok =
      ProcessReceiver(*receiver_one_artifact, *receiver_one_channel,
                      modulation);
  const auto receiver_two_ok =
      ProcessReceiver(*receiver_two_artifact, *receiver_two_channel,
                      modulation);

  auto duplicate = store.Publish(TransmissionWaveformArtifact{
      TransmissionId{77}, packet.packet_id, *frame, *waveform});
  store.ReleaseCycleArtifacts();
  const auto absent_after_cycle = !store.Find(TransmissionId{77});

  return Check(same_x, "broadcast fan-out shares one immutable X") &&
         receiver_one_ok && receiver_two_ok &&
         Check(!duplicate, "one X per TransmissionId") &&
         Check(store.size() == 0U && absent_after_cycle,
               "cycle close releases transient waveform memory");
}

}  // namespace

int main() {
  if(!TestSharedTransmissionArtifactAndE2eEndpoint()) return 1;
  std::cout << "Channel 2 integration baseline tests passed\n";
  return 0;
}
