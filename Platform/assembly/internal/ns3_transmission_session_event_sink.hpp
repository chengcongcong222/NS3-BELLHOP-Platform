#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>

#include "internal/cycle_signal_runtime.hpp"
#include "internal/event_dispatcher.hpp"
#include "internal/transmission_session.hpp"
#include "internal/transmission_session_event_sink.hpp"

namespace ns3_factory::assembly::internal {

class Ns3TransmissionSessionEventSink final
    : public runtime::internal::ITransmissionSessionEventSink {
 public:
  Ns3TransmissionSessionEventSink(
      kernel::internal::EventDispatcher& dispatcher,
      runtime::internal::CycleSignalRuntime& runtime) noexcept
      : dispatcher_(dispatcher), runtime_(runtime) {}

  [[nodiscard]] auto Publish(
      const runtime::internal::TransmissionSession& session)
      -> contracts::Status override;

 private:
  struct OrderedIntent final {
    contracts::SimTime time;
    kernel::internal::EventPhase phase;
    contracts::NodeId receiver_node_id;
    contracts::TransmissionId transmission_id;
    kernel::internal::EventCallback callback;
  };

  kernel::internal::EventDispatcher& dispatcher_;
  runtime::internal::CycleSignalRuntime& runtime_;
};

inline auto Ns3TransmissionSessionEventSink::Publish(
    const runtime::internal::TransmissionSession& session)
    -> contracts::Status {
  std::vector<OrderedIntent> ordered;
  ordered.reserve(session.received_signals().size() * 2);
  auto* const dispatcher = &dispatcher_;
  auto* const runtime = &runtime_;
  for(const auto& signal : session.received_signals()) {
    ordered.push_back(OrderedIntent{
        signal.first_arrival_at(),
        kernel::internal::EventPhase::kSignalArrival,
        signal.receiver_node_id(),
        signal.transmission_id(),
        [dispatcher, runtime, signal]() -> contracts::Status {
          const auto now = dispatcher->PlatformNow();
          if(!now) return std::unexpected(now.error());
          return runtime->HandleSignalArrival(*now, signal);
        }});
    ordered.push_back(OrderedIntent{
        signal.last_effect_end_at(),
        kernel::internal::EventPhase::kSessionFinalize,
        signal.receiver_node_id(),
        signal.transmission_id(),
        [dispatcher, runtime, signal]() -> contracts::Status {
          const auto now = dispatcher->PlatformNow();
          if(!now) return std::unexpected(now.error());
          return runtime->HandleSessionFinalize(*now, signal);
        }});
  }

  std::sort(
      ordered.begin(),
      ordered.end(),
      [](const OrderedIntent& lhs, const OrderedIntent& rhs) {
        if(lhs.time != rhs.time) return lhs.time < rhs.time;
        if(lhs.phase != rhs.phase) {
          return static_cast<std::uint8_t>(lhs.phase) <
                 static_cast<std::uint8_t>(rhs.phase);
        }
        if(lhs.receiver_node_id != rhs.receiver_node_id) {
          return lhs.receiver_node_id < rhs.receiver_node_id;
        }
        return lhs.transmission_id < rhs.transmission_id;
      });

  std::vector<kernel::internal::ScheduledEventIntent> intents;
  intents.reserve(ordered.size());
  for(auto& intent : ordered) {
    intents.push_back(kernel::internal::ScheduledEventIntent{
        intent.time, intent.phase, std::move(intent.callback)});
  }
  const auto scheduled = dispatcher_.ScheduleBatch(std::move(intents));
  if(!scheduled) return std::unexpected(scheduled.error());
  return {};
}

}  // namespace ns3_factory::assembly::internal
