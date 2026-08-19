#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <vector>

#include "internal/event_dispatcher.hpp"
#include "internal/ns3_kernel_gateway.hpp"

using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::SimDuration;
using ns3_factory::contracts::SimTime;
using ns3_factory::kernel::internal::CycleCloseEvent;
using ns3_factory::kernel::internal::EventDispatcher;
using ns3_factory::kernel::internal::EventPhase;
using ns3_factory::kernel::internal::EventSequenceId;
using ns3_factory::kernel::internal::Ns3KernelGateway;
using ns3_factory::kernel::internal::SessionFinalizeEvent;
using ns3_factory::kernel::internal::SignalArrivalEvent;
using ns3_factory::kernel::internal::TxStartEvent;

template <typename T>
concept HasAdvance = requires(T& value) {
  value.Advance();
};

template <typename T>
concept HasTick = requires(T& value) {
  value.Tick();
};

static_assert(static_cast<std::uint8_t>(EventPhase::kSessionFinalize) == 10);
static_assert(static_cast<std::uint8_t>(EventPhase::kSignalArrival) == 20);
static_assert(static_cast<std::uint8_t>(EventPhase::kInputReady) == 30);
static_assert(static_cast<std::uint8_t>(EventPhase::kRuntimeDecision) == 40);
static_assert(static_cast<std::uint8_t>(EventPhase::kTxStart) == 50);
static_assert(static_cast<std::uint8_t>(EventPhase::kCycleClose) == 90);
static_assert(!HasAdvance<Ns3KernelGateway>);
static_assert(!HasTick<Ns3KernelGateway>);
static_assert(!HasAdvance<EventDispatcher>);
static_assert(!HasTick<EventDispatcher>);
static_assert(!std::is_copy_constructible_v<EventDispatcher>);

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

auto TestTimeAuthorityAndConversions() -> bool {
  Ns3KernelGateway gateway;
  const auto initial_now = gateway.PlatformNow();
  const auto platform_time = At(1'234'567'890);
  const auto ns3_time = Ns3KernelGateway::ToNs3Time(platform_time);
  const auto round_trip = ns3_time
                              ? Ns3KernelGateway::FromNs3Time(*ns3_time)
                              : ns3_factory::contracts::Result<SimTime>{
                                    std::unexpected(ns3_time.error())};
  const auto duration = SimDuration::FromNanoseconds(-123'456);
  const auto ns3_duration = Ns3KernelGateway::ToNs3Time(duration);
  const auto duration_round_trip =
      ns3_duration
          ? Ns3KernelGateway::FromNs3Duration(*ns3_duration)
          : ns3_factory::contracts::Result<SimDuration>{
                std::unexpected(ns3_duration.error())};

  constexpr auto kMinimum =
      std::numeric_limits<SimTime::representation_type>::min();
  constexpr auto kMaximum =
      std::numeric_limits<SimTime::representation_type>::max();
  const auto overflow = Ns3KernelGateway::CheckedDelay(
      At(kMinimum), At(kMaximum));
  const auto invalid = Ns3KernelGateway::CheckedDelay(At(2), At(1));

  bool callback_observed_ns3_now = false;
  const auto scheduled = gateway.ScheduleAt(
      At(25),
      [&]() {
        const auto now = gateway.PlatformNow();
        callback_observed_ns3_now = now && *now == At(25);
      });
  const auto negative_schedule = gateway.ScheduleAt(At(-1), []() {});
  if(!initial_now || *initial_now != SimTime::Zero() || !ns3_time ||
     !round_trip || *round_trip != platform_time || !ns3_duration ||
     !duration_round_trip || *duration_round_trip != duration || overflow ||
     overflow.error().code != ErrorCode::kOverflow || invalid ||
     invalid.error().code != ErrorCode::kOutOfRange || !scheduled ||
     negative_schedule ||
     negative_schedule.error().code != ErrorCode::kOutOfRange) {
    gateway.Destroy();
    return false;
  }

  gateway.Run();
  const auto final_now = gateway.PlatformNow();
  const bool success = callback_observed_ns3_now && final_now &&
                       *final_now == At(25);
  gateway.Destroy();
  return success;
}

auto TestPhaseAndStableSequenceOrdering() -> bool {
  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway, EventSequenceId{100}};
  std::vector<int> order;
  const auto callback = [&](int value) {
    return [&, value]() -> ns3_factory::contracts::Status {
      const auto now = gateway.PlatformNow();
      if(!now || *now != At(50)) {
        return std::unexpected(
            ns3_factory::contracts::Error{
                ErrorCode::kFailedPrecondition,
                "test callback observed wrong PlatformNow"});
      }
      order.push_back(value);
      return {};
    };
  };

  const auto close = dispatcher.Schedule(
      CycleCloseEvent{At(50), callback(90)});
  const auto tx = dispatcher.Schedule(
      TxStartEvent{At(50), callback(50)});
  const auto decision = dispatcher.ScheduleAt(
      At(50), EventPhase::kRuntimeDecision, callback(40));
  const auto input = dispatcher.ScheduleAt(
      At(50), EventPhase::kInputReady, callback(30));
  const auto arrival = dispatcher.Schedule(
      SignalArrivalEvent{At(50), callback(20)});
  const auto finalize = dispatcher.Schedule(
      SessionFinalizeEvent{At(50), callback(10)});
  if(!close || !tx || !decision || !input || !arrival || !finalize ||
     close->sequence != EventSequenceId{100} ||
     finalize->sequence != EventSequenceId{105}) {
    gateway.Destroy();
    return false;
  }

  const auto status = dispatcher.Run();
  const std::vector<int> expected{10, 20, 30, 40, 50, 90};
  const bool success = status && order == expected &&
                       dispatcher.pending_event_count() == 0;
  gateway.Destroy();
  return success;
}

auto TestSamePhaseTieBreakAndDynamicRule() -> bool {
  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway};
  std::vector<int> order;
  bool earlier_rejected = false;

  for(int value : {1, 2, 3}) {
    const auto key = dispatcher.ScheduleAt(
        At(10),
        EventPhase::kInputReady,
        [&, value]() -> ns3_factory::contracts::Status {
          order.push_back(value);
          return {};
        });
    if(!key) {
      gateway.Destroy();
      return false;
    }
  }

  const auto dynamic = dispatcher.ScheduleAt(
      At(20),
      EventPhase::kRuntimeDecision,
      [&]() -> ns3_factory::contracts::Status {
        order.push_back(40);
        const auto rejected = dispatcher.ScheduleAt(
            At(20),
            EventPhase::kSignalArrival,
            []() { return ns3_factory::contracts::Status{}; });
        earlier_rejected =
            !rejected &&
            rejected.error().code == ErrorCode::kFailedPrecondition;
        const auto later = dispatcher.Schedule(
            TxStartEvent{
                At(20),
                [&]() -> ns3_factory::contracts::Status {
                  order.push_back(50);
                  return {};
                }});
        if(!later) {
          return std::unexpected(later.error());
        }
        return {};
      });
  if(!dynamic) {
    gateway.Destroy();
    return false;
  }

  const auto status = dispatcher.Run();
  const std::vector<int> expected{1, 2, 3, 40, 50};
  const bool success = status && earlier_rejected && order == expected;
  gateway.Destroy();
  return success;
}

auto TestCallbackFailureIsExplicit() -> bool {
  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway};
  bool later_callback_ran = false;
  const auto failure = dispatcher.ScheduleAt(
      At(5),
      EventPhase::kRuntimeDecision,
      []() -> ns3_factory::contracts::Status {
        return std::unexpected(
            ns3_factory::contracts::Error{
                ErrorCode::kUnavailable,
                "test fixture callback failure"});
      });
  const auto later = dispatcher.Schedule(
      CycleCloseEvent{
          At(10),
          [&]() -> ns3_factory::contracts::Status {
            later_callback_ran = true;
            return {};
          }});
  if(!failure || !later) {
    gateway.Destroy();
    return false;
  }

  const auto status = dispatcher.Run();
  const bool success = !status &&
                       status.error().code == ErrorCode::kUnavailable &&
                       !later_callback_ran;
  gateway.Destroy();
  return success;
}

auto main() -> int {
  if(!TestTimeAuthorityAndConversions() ||
     !TestPhaseAndStableSequenceOrdering() ||
     !TestSamePhaseTieBreakAndDynamicRule() ||
     !TestCallbackFailureIsExplicit()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
