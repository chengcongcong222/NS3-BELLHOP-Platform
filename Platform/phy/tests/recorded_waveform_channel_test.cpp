#include <iostream>
#include "internal/recorded_waveform_channel.hpp"
#include "internal/waveform_phy.hpp"
using namespace ns3_factory;
using namespace contracts;
using namespace phy::internal;
int main() {
  auto c=std::make_shared<recorded::Channel>(recorded::Channel{1000,1000,10,30,3,
      std::vector<recorded::Complex>(90),{},false,1,1});
  for(std::size_t t=0;t<30;++t)c->coefficients[3*t]=1;
  RecordedLink link{NodeId{0},NodeId{1},c,100,SimDuration::FromNanoseconds(200'000'000),1,1000,8000,1000,nullptr};
  auto provider=RecordedWaveformChannel::Create({link});
  if(!provider) return 1;
  WaveformPhyConfig config{{ModulationScheme::kOsdm,1000,1000,1,1000,30},1000,
      NoNoiseConfig{},{ChannelEqualizationMode::kTrainingSequenceMmse},false,
      WaveformFrameFormat::kPayloadWithCrc32,100000};
  config.modulation.osdm.block_length_symbols=64;
  config.modulation.osdm.vector_length_symbols=8;
  config.modulation.osdm.cyclic_prefix_samples=16;
  config.modulation.osdm.training_preamble_enabled=true;
  config.modulation.osdm.maximum_sync_search_samples=32;
  config.modulation.osdm.minimum_sync_correlation=.01;
  TransmissionWaveformStore store;
  auto tx=WaveformTxPhy::Create(config,store);
  auto rx=WaveformRxPhy::Create(config,store,nullptr,&*provider);
  if(!tx||!rx) return 2;
  DigitalPacket packet{PacketId{1},NodeId{0},UnicastDestination{NodeId{1}},std::vector<std::byte>(4,std::byte{0x5a})};
  TransmissionTarget target=UnicastTransmissionTarget{NodeId{1}};
  auto emission=tx->Encode(packet,{TransmissionId{1},NodeId{0},target,SimTime::Zero()});
  if(!emission) {std::cerr<<emission.error().message;return 3;}
  auto q=ChannelQuery::Create(TransmissionId{1},NodeId{0},NodeId{1},{0,0,-10},{100,0,-10},
      SimTime::Zero(),1000,1000,emission->duration());
  auto response=provider->Query(*q);
  if(!response||!std::holds_alternative<RecordedChannelResponse>(*response))return 4;
  auto s=ReceivedSignal::Create(*emission,std::get<RecordedChannelResponse>(*response));
  auto artifact=store.Find(TransmissionId{1});
  auto processed=provider->Process((*artifact)->transmitted_waveform(),*emission,*s->recorded_response());
  if(!processed){std::cerr<<processed.error().message;return 5;}
  auto repeated=provider->Process((*artifact)->transmitted_waveform(),*emission,*s->recorded_response());
  if(!repeated||processed->channel_output!=repeated->channel_output)return 6;
  auto reverse=ChannelQuery::Create(TransmissionId{2},NodeId{1},NodeId{0},{},{},SimTime::Zero(),1000,1000,emission->duration());
  if(provider->Query(*reverse))return 7;
  auto late=ChannelQuery::Create(TransmissionId{3},NodeId{0},NodeId{1},{},{},SimTime::FromNanoseconds(3'000'000'000),1000,1000,emission->duration());
  if(provider->Query(*late))return 8;
  auto window=ReceiverWindow::Create(NodeId{1},*s,{});
  auto noise=NoiseObservation::Create(NodeId{1},s->first_arrival_at(),s->last_effect_end_at(),
      s->lower_frequency_hz(),s->upper_frequency_hz(),0);
  auto request=RxDecodeRequest::Create(*window,*noise);
  auto decoded=rx->Decode(*request);
  if(!decoded) {std::cerr<<decoded.error().message;return 9;}
  if(!decoded->recovered_payload()||*decoded->recovered_payload()!=packet.payload||
      !decoded->bit_comparison()||decoded->bit_comparison()->erroneous_payload_bits!=0)return 10;
  std::cout<<"Recorded bridge deterministic, directed coverage and full envelope passed\n";
  return 0;
}
