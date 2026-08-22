#include <cstdint>
#include <cstdlib>
#include <vector>

#include "internal/event_dispatcher.hpp"
#include "internal/ns3_kernel_gateway.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::kernel::internal;

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

auto TestFreshDispatcherAtCurrentTime() -> bool {
  Ns3KernelGateway gateway;
  const auto initial = gateway.PlatformNow();
  std::vector<int> order;

  {
    EventDispatcher first{gateway};
    const auto scheduled = first.ScheduleAt(
        At(10), EventPhase::kCycleClose, [&]() -> Status {
          order.push_back(1);
          return {};
        });
    if(!scheduled || !first.Run()) {
      gateway.Destroy();
      return false;
    }
  }
  const auto after_first = gateway.PlatformNow();

  {
    EventDispatcher second{gateway};
    const auto at_boundary = second.ScheduleAt(
        At(10), EventPhase::kTxStart, [&]() -> Status {
          order.push_back(2);
          return {};
        });
    if(!at_boundary || !second.Run()) {
      gateway.Destroy();
      return false;
    }
    const auto after_boundary = gateway.PlatformNow();
    const auto later = second.ScheduleAt(
        At(20), EventPhase::kCycleClose, [&]() -> Status {
          order.push_back(3);
          return {};
        });
    if(!after_boundary || *after_boundary != At(10) || !later ||
       !second.Run()) {
      gateway.Destroy();
      return false;
    }
  }
  const auto after_later = gateway.PlatformNow();
  gateway.Destroy();
  const auto after_destroy = gateway.PlatformNow();
  return initial && *initial == SimTime::Zero() && after_first &&
         *after_first == At(10) && after_later && *after_later == At(20) &&
         after_destroy && *after_destroy == SimTime::Zero() &&
         order == std::vector<int>{1, 2, 3};
}

}  // namespace

auto main() -> int {
  return TestFreshDispatcherAtCurrentTime() ? EXIT_SUCCESS : EXIT_FAILURE;
}
