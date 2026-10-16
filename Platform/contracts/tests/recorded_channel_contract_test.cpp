#include <limits>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/trace.hpp>
using namespace ns3_factory::contracts;
int main() {
  const auto d=[](std::int64_t n){return SimDuration::FromNanoseconds(n);};
  auto e=TxEmission::Create(TransmissionId{1},PacketId{1},NodeId{0},SimTime::Zero(),d(100),13000,4000,0);
  auto c=RecordedChannelResponse::Create(TransmissionId{1},NodeId{1},9,200,d(50),d(150));
  if(!e||!c) return 1;
  auto s=ReceivedSignal::Create(*e,*c);
  if(!s||!s->recorded_response()||s->first_arrival_at().nanoseconds()!=50||
      s->last_effect_end_at().nanoseconds()!=200||ComputeP0ScalarReceivedLevelDbRe1upa(*s)) return 2;
  if(RecordedChannelResponse::Create(TransmissionId{1},NodeId{1},9,200,d(-1),d(1))||
     RecordedChannelResponse::Create(TransmissionId{1},NodeId{1},9,200,d(0),d(0))) return 3;
  auto wrong=RecordedChannelResponse::Create(TransmissionId{2},NodeId{1},9,200,d(0),d(1));
  if(ReceivedSignal::Create(*e,*wrong)) return 4;
  auto late=TxEmission::Create(TransmissionId{1},PacketId{1},NodeId{0},
      SimTime::FromNanoseconds(std::numeric_limits<std::int64_t>::max()-100),d(1),13000,4000,0);
  if(ReceivedSignal::Create(*late,*c)) return 5;
  if(!TraceEvent::Create(SimTime::Zero(),ChannelOutcomeTrace{TransmissionId{1},NodeId{1},
      TraceRecordedChannelOutcome{d(50),d(150)}})) return 6;
  if(TraceEvent::Create(SimTime::Zero(),ChannelOutcomeTrace{TransmissionId{1},NodeId{1},
      TraceRecordedChannelOutcome{d(-1),d(150)}})) return 7;
  return 0;
}
