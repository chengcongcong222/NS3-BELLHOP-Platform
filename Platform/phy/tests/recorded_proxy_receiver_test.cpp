#include <cassert>
#include <iostream>
#include "internal/recorded_proxy_waveform_channel.hpp"
#include "internal/waveform_phy.hpp"

using namespace ns3_factory;
using namespace contracts;
using namespace phy::internal;

int main() {
 for(const auto scheme : {ModulationScheme::kBpsk, ModulationScheme::kBfsk, ModulationScheme::kOsdm}) {
 for(const auto payload_bytes : {4U,5U}) {
  auto channel=[](double gain) {
    auto c=std::make_shared<recorded::Channel>(recorded::Channel{1000,8000,10,1000,3,
        std::vector<recorded::Complex>(3000),{},false,1,1});
    for(std::size_t t=0;t<1000;++t)c->coefficients[3*t]=gain;
    return c;
  };
  const auto positive=channel(1),negative=channel(-1);
  const auto noise=std::make_shared<const std::vector<double>>(800000, .01);
  RecordedLink a{NodeId{0},NodeId{2},positive,100,SimDuration::FromNanoseconds(200'000'000),
      1,1000,8000,1000,noise};
  auto b=a; b.transmitter=NodeId{1}; b.channel=negative;
  auto legacy=RecordedWaveformChannel::Create({a});
  auto proxy=RecordedProxyWaveformChannel::Create({a,b});
  assert(legacy && proxy);
  auto invalid=b; invalid.noise=std::make_shared<const std::vector<double>>(100000,.01);
  assert(!RecordedProxyWaveformChannel::Create({a,invalid})); // not one noise owner
  invalid=b; invalid.baseband_rate_hz=2000;
  assert(!RecordedProxyWaveformChannel::Create({a,invalid}));
  invalid=b; invalid.modeled_reference_delay=SimDuration::FromNanoseconds(1);
  assert(!RecordedProxyWaveformChannel::Create({a,invalid}));
  WaveformPhyConfig config{{scheme,1000,scheme==ModulationScheme::kOsdm ? 1000. : 10.,1,1000,30},1000,
      NoNoiseConfig{},{scheme==ModulationScheme::kOsdm ? ChannelEqualizationMode::kTrainingSequenceMmse
                                                    : ChannelEqualizationMode::kDisabled},false,
      WaveformFrameFormat::kPayloadWithCrc32,100000};
  config.modulation.osdm.block_length_symbols=64;
  config.modulation.osdm.vector_length_symbols=8;
  config.modulation.osdm.cyclic_prefix_samples=16;
  config.modulation.osdm.training_preamble_enabled=true;
  config.modulation.osdm.maximum_sync_search_samples=32;
  config.modulation.osdm.minimum_sync_correlation=.01;
  TransmissionWaveformStore store;
  auto tx=WaveformTxPhy::Create(config,store);
  auto rx=WaveformRxPhy::Create(config,store,nullptr,&*proxy);
  auto old_rx=WaveformRxPhy::Create(config,store,nullptr,&*legacy);
  assert(tx && rx && old_rx);
  auto signal=[&](std::uint64_t id, NodeId sender, std::int64_t at) {
    DigitalPacket packet{PacketId{id},sender,UnicastDestination{NodeId{2}},
        std::vector<std::byte>(payload_bytes,std::byte{0x5a})};
    auto emission=tx->Encode(packet,{TransmissionId{id},sender,
        UnicastTransmissionTarget{NodeId{2}},SimTime::FromNanoseconds(at)});
    assert(emission);
    auto query=ChannelQuery::Create(TransmissionId{id},sender,NodeId{2},{},{},
        emission->started_at(),1000,1000,emission->duration());
    assert(query);
    auto response=proxy->Query(*query); assert(response);
    auto received=ReceivedSignal::Create(*emission,std::get<RecordedChannelResponse>(*response));
    assert(received); return *received;
  };
  auto s0=signal(1,NodeId{0},0),s1=signal(2,NodeId{1},0);
  auto w0=store.Find(TransmissionId{1}),w1=store.Find(TransmissionId{2});
  assert(w0 && w1);
  std::vector<RecordedWaveformInput> inputs{
      {&(*w0)->transmitted_waveform(),&s0.emission(),s0.recorded_response()},
      {&(*w1)->transmitted_waveform(),&s1.emission(),s1.recorded_response()}};
  auto isolated=proxy->ProcessWindow(std::span{inputs.data(),1},100000);
  auto old=legacy->Process(*inputs[0].waveform,*inputs[0].emission,*inputs[0].response);
  assert(isolated && old);
  assert(isolated->channel_output==old->channel_output && isolated->noise==old->noise);
  auto canceled=proxy->ProcessWindow(inputs,100000); assert(canceled);
  assert(canceled->noise==isolated->noise); // no per-link noise multiplication
  for(const auto& sample:canceled->channel_output.samples())assert(std::abs(sample)<1e-14);
  auto stronger=b; stronger.relative_amplitude=2;
  auto strong_provider=RecordedProxyWaveformChannel::Create({a,stronger}); assert(strong_provider);
  auto strong=strong_provider->ProcessWindow(inputs,100000); assert(strong);
  for(std::size_t i=0;i<strong->channel_output.sample_count();++i)
    assert(std::abs(strong->channel_output.samples()[i]+isolated->channel_output.samples()[i])<1e-12);
  // One passband sample is an eighth carrier cycle, not an integer baseband
  // shift. Equal and opposite CIRs must NOT cancel after this physical offset.
  auto offset=signal(4,NodeId{1},125'000);
  auto offset_wave=store.Find(TransmissionId{4}); assert(offset_wave);
  auto shifted_inputs=inputs;
  shifted_inputs[1]={&(*offset_wave)->transmitted_waveform(),&offset.emission(),offset.recorded_response()};
  auto shifted=proxy->ProcessWindow(shifted_inputs,100000); assert(shifted);
  double shifted_energy=0;
  for(const auto& value:shifted->channel_output.samples())shifted_energy+=std::norm(value);
  assert(shifted_energy>1e-6);
  auto separated=signal(5,NodeId{1},20'000'000'000);
  auto separated_wave=store.Find(TransmissionId{5}); assert(separated_wave);
  shifted_inputs[1]={&(*separated_wave)->transmitted_waveform(),&separated.emission(),separated.recorded_response()};
  auto nonoverlap=proxy->ProcessWindow(shifted_inputs,100000); assert(nonoverlap);
  assert(nonoverlap->channel_output==isolated->channel_output && nonoverlap->noise==isolated->noise);
  auto off_grid=ChannelQuery::Create(TransmissionId{9},NodeId{0},NodeId{2},{},{},
      SimTime::FromNanoseconds(1),1000,1000,s0.emission().duration());
  assert(off_grid && !proxy->Query(*off_grid));
  auto request=[&](std::vector<ReceivedSignal> overlaps) {
    auto window=ReceiverWindow::Create(NodeId{2},s0,std::move(overlaps)); assert(window);
    auto n=NoiseObservation::Create(NodeId{2},s0.first_arrival_at(),s0.last_effect_end_at(),
        s0.lower_frequency_hz(),s0.upper_frequency_hz(),0); assert(n);
    auto r=RxDecodeRequest::Create(*window,*n); assert(r); return *r;
  };
  auto clean=rx->Decode(request({})),old_clean=old_rx->Decode(request({}));
  assert(clean && old_clean && clean->recovered_payload()==old_clean->recovered_payload());
  assert(clean->recovered_payload());
  auto collision=rx->Decode(request({s1}));
  if(!collision) std::cerr<<"Collision decode error: "<<collision.error().message<<"\n";
  assert(collision);
  if(payload_bytes==5 || scheme==ModulationScheme::kOsdm)
    assert(collision->outcome()==DecodeOutcome::kNotDecoded && !collision->recovered_payload());
  if(collision->outcome()==DecodeOutcome::kDecoded) {
    // A valid CRC codeword can occur even when the desired signal cancels.
    // Do not secretly reject it using truth: retain the nonzero bit-error evidence.
    assert(collision->bit_comparison() && collision->bit_comparison()->erroneous_payload_bits>0);
    assert(*collision->recovered_payload()!=std::vector<std::byte>(payload_bytes,std::byte{0x5a}));
  }
  assert(!old_rx->Decode(request({s1}))); // legacy rejection is unchanged
  auto later=signal(3,NodeId{1},40'000'000);
  auto partial=rx->Decode(request({later})); assert(partial);
  auto again=proxy->ProcessWindow(inputs,100000); assert(again);
  assert(again->channel_output==canceled->channel_output && again->noise==canceled->noise);
  std::cout<<"Proxy passband mixture: legacy equality, cancellation, partial overlap, real decoder, single noise passed\n";
 }
 }
}
