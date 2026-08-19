#include <cstdlib>

#include <ns3/event-id.h>
#include <ns3/nstime.h>
#include <ns3/simulator.h>

int main() {
  int observed_time = 0;

  const ns3::EventId event = ns3::Simulator::Schedule(
      ns3::MilliSeconds(1),
      [&observed_time]() {
        observed_time =
            ns3::Simulator::Now() == ns3::MilliSeconds(1) ? 1 : -1;
      });

  if(event.IsExpired()) {
    ns3::Simulator::Destroy();
    return EXIT_FAILURE;
  }

  ns3::Simulator::Run();
  ns3::Simulator::Destroy();

  return observed_time == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
}
