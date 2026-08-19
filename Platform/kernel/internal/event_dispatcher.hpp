#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/time.hpp>

#include "internal/ns3_kernel_gateway.hpp"

namespace ns3_factory::kernel::internal {

enum class EventPhase : std::uint8_t {
  kSessionFinalize = 10,
  kSignalArrival = 20,
  kInputReady = 30,
  kRuntimeDecision = 40,
  kTxStart = 50,
  kCycleClose = 90,
};

class EventSequenceId final {
 public:
  constexpr explicit EventSequenceId(std::uint64_t value) noexcept
      : value_(value) {}

  [[nodiscard]] constexpr auto value() const noexcept -> std::uint64_t {
    return value_;
  }

  constexpr auto operator<=>(const EventSequenceId&) const noexcept = default;

 private:
  std::uint64_t value_;
};

struct EventKey final {
  contracts::SimTime time;
  EventPhase phase;
  EventSequenceId sequence;

  constexpr auto operator<=>(const EventKey&) const noexcept = default;
};

using EventCallback = std::function<contracts::Status()>;

struct TxStartEvent final {
  contracts::SimTime started_at;
  EventCallback callback;
};

struct SignalArrivalEvent final {
  contracts::SimTime first_arrival_at;
  EventCallback callback;
};

struct SessionFinalizeEvent final {
  contracts::SimTime last_effect_end_at;
  EventCallback callback;
};

struct CycleCloseEvent final {
  contracts::SimTime close_time;
  EventCallback callback;
};

class EventDispatcher final {
 public:
  explicit EventDispatcher(
      Ns3KernelGateway& gateway,
      EventSequenceId first_sequence = EventSequenceId{0}) noexcept
      : gateway_(gateway), next_sequence_(first_sequence.value()) {}

  EventDispatcher(const EventDispatcher&) = delete;
  auto operator=(const EventDispatcher&) -> EventDispatcher& = delete;
  EventDispatcher(EventDispatcher&&) = delete;
  auto operator=(EventDispatcher&&) -> EventDispatcher& = delete;

  [[nodiscard]] auto ScheduleAt(contracts::SimTime time,
                                EventPhase phase,
                                EventCallback callback)
      -> contracts::Result<EventKey>;

  [[nodiscard]] auto Schedule(TxStartEvent event)
      -> contracts::Result<EventKey> {
    return ScheduleAt(event.started_at,
                      EventPhase::kTxStart,
                      std::move(event.callback));
  }

  [[nodiscard]] auto Schedule(SignalArrivalEvent event)
      -> contracts::Result<EventKey> {
    return ScheduleAt(event.first_arrival_at,
                      EventPhase::kSignalArrival,
                      std::move(event.callback));
  }

  [[nodiscard]] auto Schedule(SessionFinalizeEvent event)
      -> contracts::Result<EventKey> {
    return ScheduleAt(event.last_effect_end_at,
                      EventPhase::kSessionFinalize,
                      std::move(event.callback));
  }

  [[nodiscard]] auto Schedule(CycleCloseEvent event)
      -> contracts::Result<EventKey> {
    return ScheduleAt(event.close_time,
                      EventPhase::kCycleClose,
                      std::move(event.callback));
  }

  [[nodiscard]] auto Run() -> contracts::Status;

  [[nodiscard]] auto pending_event_count() const noexcept -> std::size_t;

 private:
  struct EventRecord final {
    EventKey key;
    EventCallback callback;
  };

  struct TimeBucket final {
    contracts::SimTime time;
    std::vector<EventRecord> records;
  };

  [[nodiscard]] static constexpr auto IsValidPhase(EventPhase phase) noexcept
      -> bool;

  [[nodiscard]] auto AllocateSequence()
      -> contracts::Result<EventSequenceId>;

  [[nodiscard]] auto FindBucket(contracts::SimTime time)
      -> std::vector<TimeBucket>::iterator;

  auto DispatchTime(contracts::SimTime time) -> void;

  Ns3KernelGateway& gateway_;
  std::uint64_t next_sequence_;
  bool sequence_exhausted_{false};
  bool dispatching_{false};
  std::optional<contracts::SimTime> dispatch_time_;
  std::optional<EventPhase> current_phase_;
  std::optional<contracts::SimTime> last_dispatch_time_;
  std::optional<EventPhase> last_phase_at_time_;
  std::optional<contracts::Error> execution_error_;
  std::vector<TimeBucket> buckets_;
};

inline constexpr auto EventDispatcher::IsValidPhase(EventPhase phase) noexcept
    -> bool {
  switch(phase) {
    case EventPhase::kSessionFinalize:
    case EventPhase::kSignalArrival:
    case EventPhase::kInputReady:
    case EventPhase::kRuntimeDecision:
    case EventPhase::kTxStart:
    case EventPhase::kCycleClose:
      return true;
  }
  return false;
}

inline auto EventDispatcher::AllocateSequence()
    -> contracts::Result<EventSequenceId> {
  if(sequence_exhausted_) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "EventSequenceId allocation exhausted"});
  }
  const EventSequenceId allocated{next_sequence_};
  if(next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    sequence_exhausted_ = true;
  } else {
    ++next_sequence_;
  }
  return allocated;
}

inline auto EventDispatcher::FindBucket(contracts::SimTime time)
    -> std::vector<TimeBucket>::iterator {
  return std::lower_bound(
      buckets_.begin(),
      buckets_.end(),
      time,
      [](const TimeBucket& bucket, contracts::SimTime candidate) {
        return bucket.time < candidate;
      });
}

inline auto EventDispatcher::ScheduleAt(contracts::SimTime time,
                                        EventPhase phase,
                                        EventCallback callback)
    -> contracts::Result<EventKey> {
  if(!IsValidPhase(phase) || !callback) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "EventDispatcher requires a valid phase/callback"});
  }

  const auto now = gateway_.PlatformNow();
  if(!now) {
    return std::unexpected(now.error());
  }
  if(time < *now) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOutOfRange,
                         "EventDispatcher cannot schedule into the past"});
  }

  std::optional<EventPhase> phase_floor;
  if(dispatching_ && dispatch_time_ && *dispatch_time_ == time) {
    phase_floor = current_phase_;
  } else if(last_dispatch_time_ && *last_dispatch_time_ == time) {
    phase_floor = last_phase_at_time_;
  }
  if(phase_floor && static_cast<std::uint8_t>(phase) <=
                        static_cast<std::uint8_t>(*phase_floor)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "Same-time dynamic event must use a later phase"});
  }

  const auto sequence = AllocateSequence();
  if(!sequence) {
    return std::unexpected(sequence.error());
  }
  EventRecord record{EventKey{time, phase, *sequence}, std::move(callback)};

  auto bucket = FindBucket(time);
  if(bucket != buckets_.end() && bucket->time == time) {
    bucket->records.push_back(std::move(record));
    return EventKey{time, phase, *sequence};
  }

  bucket = buckets_.insert(bucket, TimeBucket{time, {std::move(record)}});
  const auto schedule_status = gateway_.ScheduleAt(
      time, [this, time]() { DispatchTime(time); });
  if(!schedule_status) {
    buckets_.erase(bucket);
    return std::unexpected(schedule_status.error());
  }
  return EventKey{time, phase, *sequence};
}

inline auto EventDispatcher::DispatchTime(contracts::SimTime time) -> void {
  dispatching_ = true;
  dispatch_time_ = time;
  current_phase_.reset();

  while(!execution_error_) {
    auto bucket = FindBucket(time);
    if(bucket == buckets_.end() || bucket->time != time ||
       bucket->records.empty()) {
      if(bucket != buckets_.end() && bucket->time == time) {
        buckets_.erase(bucket);
      }
      break;
    }

    const auto next = std::min_element(
        bucket->records.begin(),
        bucket->records.end(),
        [](const EventRecord& lhs, const EventRecord& rhs) {
          return lhs.key < rhs.key;
        });
    EventRecord record = std::move(*next);
    bucket->records.erase(next);
    current_phase_ = record.key.phase;
    last_dispatch_time_ = time;
    last_phase_at_time_ = record.key.phase;

    const auto status = record.callback();
    if(!status) {
      execution_error_ = status.error();
      gateway_.Stop();
    }
  }

  dispatching_ = false;
  dispatch_time_.reset();
  current_phase_.reset();
}

inline auto EventDispatcher::Run() -> contracts::Status {
  execution_error_.reset();
  gateway_.Run();
  if(execution_error_) {
    return std::unexpected(*execution_error_);
  }
  return {};
}

inline auto EventDispatcher::pending_event_count() const noexcept
    -> std::size_t {
  std::size_t count = 0;
  for(const auto& bucket : buckets_) {
    count += bucket.records.size();
  }
  return count;
}

}  // namespace ns3_factory::kernel::internal
