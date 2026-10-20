#include <cassert>
#include <limits>
#include "internal/receiver_sample_mixer.hpp"

using namespace ns3_factory::phy::internal;

int main() {
  auto a = WaveformBuffer::Create(1000, {{1,2}, {2,3}, {3,4}}).value();
  auto b = WaveformBuffer::Create(1000, {{-1,-2}, {-2,-3}, {-3,-4}}).value();
  auto n = WaveformBuffer::Create(1000, {{.1,0}, {.2,0}, {.3,0}, {.4,0}, {.5,0}}).value();
  std::vector<ReceiverSampleContribution> inputs{{1,10,&a}, {2,10,&b}};
  auto exact = MixReceiverSamples(inputs,10,n,5,2);
  assert(exact && exact->observed == n); // phase cancellation, not power sum
  assert(exact->contributing_signals == 2);
  inputs[1].first_sample = 12;
  auto partial = MixReceiverSamples(inputs,10,n,5,2);
  assert(partial);
  assert(partial->signal_sum.samples()[0] == a.samples()[0]);
  assert(partial->signal_sum.samples()[2] == a.samples()[2] + b.samples()[0]);
  assert(partial->signal_sum.samples()[4] == b.samples()[2]); // delayed tail
  std::reverse(inputs.begin(),inputs.end());
  auto reversed = MixReceiverSamples(inputs,10,n,5,2);
  assert(reversed && reversed->observed == partial->observed);
  inputs = {{1,10,&a}, {2,13,&b}};
  auto adjacent = MixReceiverSamples(inputs,10,n,5,2);
  assert(adjacent && adjacent->signal_sum.samples()[2] == a.samples()[2]);
  assert(adjacent->signal_sum.samples()[3] == b.samples()[0]);
  auto silent = WaveformBuffer::Create(1000, {{0,0}, {0,0}, {0,0}}).value();
  inputs = {{1,10,&a}};
  auto alone = MixReceiverSamples(inputs,10,n,5,2);
  inputs.push_back({2,10,&silent});
  auto with_silent = MixReceiverSamples(inputs,10,n,5,2);
  assert(alone && with_silent && alone->observed == with_silent->observed);
  assert(!MixReceiverSamples(inputs,10,n,4,2));
  assert(!MixReceiverSamples(inputs,10,n,5,1));
  inputs[1].transmission_id = 1;
  assert(!MixReceiverSamples(inputs,10,n,5,2));
  inputs = {{1,std::numeric_limits<std::int64_t>::max(),&a}};
  assert(!MixReceiverSamples(inputs,10,n,5,2));
  auto other_clock = WaveformBuffer::Create(2000, {{1,0}}).value();
  inputs = {{1,10,&other_clock}};
  assert(!MixReceiverSamples(inputs,10,n,5,2));
  inputs = {{1,10,nullptr}};
  assert(!MixReceiverSamples(inputs,10,n,5,2));
  auto huge = WaveformBuffer::Create(1000, {{std::numeric_limits<double>::max(),0}}).value();
  inputs = {{1,10,&huge}, {2,10,&huge}};
  assert(!MixReceiverSamples(inputs,10,n,5,2));
}
