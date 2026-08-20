#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "internal/event_dispatcher.hpp"
#include "internal/ns3_kernel_gateway.hpp"

using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::Status;
using ns3_factory::kernel::internal::EventDispatcher;
using ns3_factory::kernel::internal::EventPhase;
using ns3_factory::kernel::internal::EventSequenceId;
using ns3_factory::kernel::internal::Ns3KernelGateway;
using ns3_factory::kernel::internal::ScheduledEventIntent;

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

auto TestSequenceCapacityFailureIsAtomic() -> bool {
  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{
      gateway,
      EventSequenceId{std::numeric_limits<std::uint64_t>::max() - 1}};
  std::size_t callback_count = 0;
  std::vector<ScheduledEventIntent> intents;
  for(const auto time : {At(1), At(2), At(3)}) {
    intents.push_back(ScheduledEventIntent{
        time,
        EventPhase::kTxStart,
        [&callback_count]() -> Status {
          ++callback_count;
          return {};
        }});
  }

  const auto pending_before = dispatcher.pending_event_count();
  const auto scheduled = dispatcher.ScheduleBatch(std::move(intents));
  const auto pending_after = dispatcher.pending_event_count();
  const auto run = dispatcher.Run();
  const bool success =
      !scheduled && scheduled.error().code == ErrorCode::kOverflow &&
      pending_before == 0 && pending_after == pending_before && run &&
      callback_count == 0;
  gateway.Destroy();
  return success;
}

auto TestPastFailureLeavesNoBusinessEventsAndRetrySucceeds() -> bool {
  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway};
  std::vector<int> order;

  const auto advance = gateway.ScheduleAt(At(5), []() {});
  if(!advance) {
    gateway.Destroy();
    return false;
  }
  gateway.Run();

  std::vector<ScheduledEventIntent> invalid{
      ScheduledEventIntent{
          At(6),
          EventPhase::kTxStart,
          [&order]() -> Status {
            order.push_back(60);
            return {};
          }},
      ScheduledEventIntent{
          At(4),
          EventPhase::kCycleClose,
          [&order]() -> Status {
            order.push_back(40);
            return {};
          }}};
  const auto pending_before = dispatcher.pending_event_count();
  const auto rejected = dispatcher.ScheduleBatch(std::move(invalid));
  if(rejected || rejected.error().code != ErrorCode::kOutOfRange ||
     dispatcher.pending_event_count() != pending_before) {
    gateway.Destroy();
    return false;
  }

  std::vector<ScheduledEventIntent> valid{
      ScheduledEventIntent{
          At(6),
          EventPhase::kInputReady,
          [&order]() -> Status {
            order.push_back(30);
            return {};
          }},
      ScheduledEventIntent{
          At(6),
          EventPhase::kTxStart,
          [&order]() -> Status {
            order.push_back(50);
            return {};
          }},
      ScheduledEventIntent{
          At(8),
          EventPhase::kCycleClose,
          [&order]() -> Status {
            order.push_back(90);
            return {};
          }}};
  const auto scheduled = dispatcher.ScheduleBatch(std::move(valid));
  if(!scheduled || scheduled->size() != 3 ||
     (*scheduled)[0].sequence != EventSequenceId{0} ||
     (*scheduled)[1].sequence != EventSequenceId{1} ||
     (*scheduled)[2].sequence != EventSequenceId{2} ||
     dispatcher.pending_event_count() != 3) {
    gateway.Destroy();
    return false;
  }

  const auto run = dispatcher.Run();
  const bool success = run && order == std::vector<int>{30, 50, 90} &&
                       dispatcher.pending_event_count() == 0;
  gateway.Destroy();
  return success;
}

auto TestDynamicPhaseFailureRejectsWholeBatch() -> bool {
  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway};
  std::size_t ghost_callback_count = 0;
  bool batch_rejected = false;

  const auto parent = dispatcher.ScheduleAt(
      At(10),
      EventPhase::kRuntimeDecision,
      [&]() -> Status {
        std::vector<ScheduledEventIntent> intents{
            ScheduledEventIntent{
                At(10),
                EventPhase::kTxStart,
                [&ghost_callback_count]() -> Status {
                  ++ghost_callback_count;
                  return {};
                }},
            ScheduledEventIntent{
                At(10),
                EventPhase::kSignalArrival,
                [&ghost_callback_count]() -> Status {
                  ++ghost_callback_count;
                  return {};
                }}};
        const auto scheduled = dispatcher.ScheduleBatch(std::move(intents));
        batch_rejected =
            !scheduled &&
            scheduled.error().code == ErrorCode::kFailedPrecondition;
        return {};
      });
  if(!parent) {
    gateway.Destroy();
    return false;
  }

  const auto run = dispatcher.Run();
  const bool success = run && batch_rejected && ghost_callback_count == 0 &&
                       dispatcher.pending_event_count() == 0;
  gateway.Destroy();
  return success;
}

}  // namespace

auto main() -> int {
  if(!TestSequenceCapacityFailureIsAtomic() ||
     !TestPastFailureLeavesNoBusinessEventsAndRetrySucceeds() ||
     !TestDynamicPhaseFailureRejectsWholeBatch()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
