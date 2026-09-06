#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "internal/acceptance_feature.hpp"
#include "internal/acoustic_field_channel_provider.hpp"
#include "internal/reference_environment_asset.hpp"
#include "internal/waveform_phy.hpp"

namespace {
using namespace ns3_factory::contracts;
using namespace ns3_factory::assembly::internal;
using namespace ns3_factory::environment::internal;
using namespace ns3_factory::phy::internal;

template<class T>
auto Require(Result<T> result) -> T {
  if(!result) throw std::runtime_error(result.error().message);
  return std::move(*result);
}

void Verify(bool value, const char* description) {
  if(!value) throw std::runtime_error(description);
}

void TestFeatureRoundTrip(const AcousticFieldChannelProvider& channel,
                         ModulationScheme scheme) {
  const DetectionFeatureReportV1 feature{17, 12'345, -950, 258, -534, 85, 0};
  const auto bytes = Require(EncodeDetectionFeatureReportV1(feature));
  const DigitalPacket packet{PacketId{42}, NodeId{20},
      UnicastDestination{NodeId{99}}, {bytes.begin(), bytes.end()}};
  const WaveformPhyConfig config{
      {scheme, 6000.0, 60.0, std::pow(10.0, 110.0 / 20.0), 25000.0, 30.0},
      4000.0, NoNoiseConfig{}, true,
      WaveformFrameFormat::kPayloadWithCrc32, 1'000'000};
  TransmissionWaveformStore store;
  const auto tx = Require(WaveformTxPhy::Create(config, store));
  const auto rx = Require(WaveformRxPhy::Create(config, store));
  const auto emission = Require(tx.Encode(packet,
      {TransmissionId{52}, NodeId{20}, BroadcastTransmissionTarget{},
       SimTime::Zero()}));
  const auto shared_x = Require(store.Find(TransmissionId{52}));
  Verify(shared_x->original_frame().bit_count() == 120 &&
         shared_x->transmitted_waveform().sample_count() == 15200,
         "Feature must become 120 payload bits plus 32 physical CRC bits");
  // Two receivers at the same modeled depth/range intentionally see the same
  // real Bellhop cell, using one immutable X, not two modulation operations.
  for(const auto receiver : {NodeId{99}, NodeId{100}}) {
    const auto query = Require(ChannelQuery::Create(
        TransmissionId{52}, NodeId{20}, receiver, {0, 0, -65}, {1000, 0, -8},
        SimTime::Zero(), 25000.0, 4000.0));
    const auto outcome = Require(channel.Query(query));
    const auto* response = std::get_if<ChannelFieldResponse>(&outcome);
    Verify(response && response->paths().size() == 9 &&
           response->first_arrival_delay() > SimDuration::Zero(),
           "Expected real Bellhop arrivals, not a synthesized direct path");
    const auto signal = Require(ReceivedSignal::Create(emission, *response));
    const auto window = Require(ReceiverWindow::Create(receiver, signal, {}));
    const auto noise = Require(NoiseObservation::Create(receiver,
        signal.first_arrival_at(), signal.last_effect_end_at(),
        signal.lower_frequency_hz(), signal.upper_frequency_hz(), 45.0));
    const auto request = Require(RxDecodeRequest::Create(window, noise));
    const auto recovered = Require(rx.Decode(request));
    Verify(recovered.outcome() == DecodeOutcome::kDecoded &&
           recovered.recovered_payload() &&
           *recovered.recovered_payload() == packet.payload,
           "Receiver bytes did not survive Bellhop waveform propagation");
    const auto recovered_feature = Require(
        DecodeDetectionFeatureReportV1(*recovered.recovered_payload()));
    Verify(recovered_feature == feature,
           "Recovered feature fields must match every original field");
    Verify(store.size() == 1 &&
           Require(store.Find(TransmissionId{52})).get() == shared_x.get(),
           "Receivers must share one Transmission waveform");
  }
}
}  // namespace

auto main() -> int {
  try {
    const auto asset = Require(BuildReferenceShallowWaterV1(
        PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT));
    const auto policy = Require(DiscreteFrequencySelectionPolicy::Create(0.0));
    const auto channel = Require(AcousticFieldChannelProvider::Create(
        std::make_shared<const AcousticFieldAsset>(asset), policy));
    TestFeatureRoundTrip(channel, ModulationScheme::kBpsk);
    TestFeatureRoundTrip(channel, ModulationScheme::kBfsk);
    std::cout << "Feature fields round-trip through real Bellhop paths; X shared\n";
    return EXIT_SUCCESS;
  } catch(const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
