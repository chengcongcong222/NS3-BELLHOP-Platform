#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/commit_service.hpp"
#include "internal/communication_id_allocator.hpp"
#include "internal/cycle_coordinator.hpp"
#include "internal/cycle_signal_runtime.hpp"
#include "internal/cycle_working_state.hpp"
#include "internal/event_dispatcher.hpp"
#include "internal/in_flight_signal_ledger.hpp"
#include "internal/ns3_kernel_gateway.hpp"
#include "internal/plan_installer.hpp"
#include "internal/receiver_processor.hpp"
#include "internal/reception_result_accumulator.hpp"
#include "internal/transmission_executor.hpp"
#include "internal/world_state_store.hpp"

using namespace ns3_factory::contracts;
using ns3_factory::kernel::internal::CycleCoordinator;
using ns3_factory::kernel::internal::CycleCoordinatorState;
using ns3_factory::kernel::internal::EventDispatcher;
using ns3_factory::kernel::internal::EventPhase;
using ns3_factory::kernel::internal::IPlanExecutionHook;
using ns3_factory::kernel::internal::Ns3KernelGateway;
using ns3_factory::kernel::internal::PlanInstaller;
using ns3_factory::kernel::internal::SessionFinalizeEvent;
using ns3_factory::kernel::internal::SignalArrivalEvent;
using namespace ns3_factory::runtime::internal;

namespace {

constexpr auto Seconds(std::int64_t value) -> SimTime {
  return SimTime::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto DurationSeconds(std::int64_t value) -> SimDuration {
  return SimDuration::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto MakeNode(std::uint64_t id, double x_meters)
    -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{x_meters, 0.0, -10.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto MakeInitialSnapshot() -> Result<WorldSnapshot> {
  return WorldSnapshot::Create(
      SnapshotVersion{0},
      SimTime::Zero(),
      std::vector<NodeCommittedState>{
          MakeNode(4, 40.0),
          MakeNode(3, 30.0),
          MakeNode(1, 10.0),
          MakeNode(0, 0.0)});
}

auto MakePlan() -> Result<ProtocolCyclePlan> {
  const auto timing = CycleTiming::Create(
      PlanningCycleId{0}, SnapshotVersion{0}, SimTime::Zero(), Seconds(10));
  if(!timing) {
    return std::unexpected(timing.error());
  }
  return ProtocolCyclePlan::Create(
      *timing, {TxOpportunity{NodeId{0}, Seconds(1)}});
}

class MockTxPhy final : public ITxPhy {
 public:
  [[nodiscard]] auto Encode(const DigitalPacket& packet,
                            const TxEncodeRequest& request) const
      -> Result<TxEmission> override {
    ++encode_count_;
    return TxEmission::Create(request.transmission_id,
                              packet.packet_id,
                              request.sender_node_id,
                              request.started_at,
                              DurationSeconds(1),
                              25'000.0,
                              4'000.0,
                              180.0);
  }

  [[nodiscard]] auto encode_count() const noexcept -> std::size_t {
    return encode_count_;
  }

 private:
  mutable std::size_t encode_count_{0};
};

class MockChannel final : public IChannelFieldProvider {
 public:
  explicit MockChannel(SimDuration first_arrival_delay) noexcept
      : first_arrival_delay_(first_arrival_delay) {}

  [[nodiscard]] auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldOutcome> override {
    ++query_count_;
    return ChannelFieldResponse::Create(query.transmission_id(),
                                        query.receiver_node_id(),
                                        70.0,
                                        first_arrival_delay_,
                                        {});
  }

  [[nodiscard]] auto query_count() const noexcept -> std::size_t {
    return query_count_;
  }

 private:
  SimDuration first_arrival_delay_;
  mutable std::size_t query_count_{0};
};

class MockNoise final : public INoiseFieldProvider {
 public:
  [[nodiscard]] auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    ++query_count_;
    return NoiseObservation::Create(query.receiver_node_id(),
                                    query.observed_from(),
                                    query.observed_until(),
                                    query.lower_frequency_hz(),
                                    query.upper_frequency_hz(),
                                    45.0);
  }

  [[nodiscard]] auto query_count() const noexcept -> std::size_t {
    return query_count_;
  }

 private:
  mutable std::size_t query_count_{0};
};

class MockRxPhy final : public IRxPhy {
 public:
  [[nodiscard]] auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    ++decode_count_;
    const auto& desired = request.receiver_window().desired_signal();
    const auto outcome = desired.receiver_node_id() == NodeId{4}
                             ? DecodeOutcome::kNotDecoded
                             : DecodeOutcome::kDecoded;
    return RxDecodeResult::Create(desired.transmission_id(),
                                  desired.emission().packet_id(),
                                  desired.receiver_node_id(),
                                  outcome);
  }

  [[nodiscard]] auto decode_count() const noexcept -> std::size_t {
    return decode_count_;
  }

 private:
  mutable std::size_t decode_count_{0};
};

// Test-only assembly boundary. Production PlanInstaller never sees packet,
// target, receiver candidates, PHY, or runtime owners.
class FixtureExecutionHook final : public IPlanExecutionHook {
 public:
  FixtureExecutionHook(EventDispatcher& dispatcher,
                       CycleSignalRuntime& runtime) noexcept
      : dispatcher_(dispatcher), runtime_(runtime) {}

  [[nodiscard]] auto OnTxStart(const TxOpportunity& opportunity,
                               SimTime now) -> Status override {
    ++tx_start_count;
    if(opportunity != TxOpportunity{NodeId{0}, Seconds(1)} ||
       now != opportunity.eligible_at) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "Fixture received an unexpected TxOpportunity"});
    }

    TransmissionExecutionRequest request{
        opportunity,
        DigitalPacket{PacketId{77},
                      NodeId{0},
                      BroadcastDestination{},
                      std::vector<std::byte>{std::byte{0x5A}}},
        BroadcastTransmissionTarget{},
        now,
        {NodeId{4}, NodeId{1}, NodeId{3}}};
    auto session = runtime_.HandleTxStart(now, std::move(request));
    if(!session) {
      return std::unexpected(session.error());
    }
    ++transmission_session_count;
    ++transmission_count;
    ++tx_emission_count;
    received_signal_count += session->received_signals().size();

    for(const auto& signal : session->received_signals()) {
      auto arrival = dispatcher_.Schedule(SignalArrivalEvent{
          signal.first_arrival_at(),
          [this, signal]() -> Status {
            const auto callback_now = dispatcher_.PlatformNow();
            if(!callback_now) {
              return std::unexpected(callback_now.error());
            }
            ++arrival_count;
            return runtime_.HandleSignalArrival(*callback_now, signal);
          }});
      if(!arrival) {
        return std::unexpected(arrival.error());
      }

      auto finalize = dispatcher_.Schedule(SessionFinalizeEvent{
          signal.last_effect_end_at(),
          [this, signal]() -> Status {
            const auto callback_now = dispatcher_.PlatformNow();
            if(!callback_now) {
              return std::unexpected(callback_now.error());
            }
            ++finalize_count;
            return runtime_.HandleSessionFinalize(*callback_now, signal);
          }});
      if(!finalize) {
        return std::unexpected(finalize.error());
      }
    }
    return {};
  }

  [[nodiscard]] auto OnCycleClose(const CycleTiming& timing,
                                  SimTime now) -> Status override {
    ++cycle_close_count;
    if(now != timing.closes_at()) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "Fixture CycleClose time mismatch"});
    }
    return runtime_.HandleCycleClose(now);
  }

  std::size_t tx_start_count{0};
  std::size_t transmission_session_count{0};
  std::size_t transmission_count{0};
  std::size_t tx_emission_count{0};
  std::size_t received_signal_count{0};
  std::size_t arrival_count{0};
  std::size_t finalize_count{0};
  std::size_t cycle_close_count{0};

 private:
  EventDispatcher& dispatcher_;
  CycleSignalRuntime& runtime_;
};

auto RunPlanDrivenCycle(SimDuration first_arrival_delay,
                        bool expect_zero_delay_failure) -> bool {
  auto snapshot = MakeInitialSnapshot();
  const auto expected_timing = CycleTiming::Create(
      PlanningCycleId{0}, SnapshotVersion{0}, SimTime::Zero(), Seconds(10));
  if(!snapshot || !expected_timing) {
    return false;
  }

  WorldStateStore store{*snapshot};
  auto working_result = CycleWorkingState::Create(
      store.current_snapshot(), PlanningCycleId{0}, SimTime::Zero());
  if(!working_result) {
    return false;
  }
  auto working = std::move(*working_result);
  CommunicationIdAllocator ids{TransmissionId{50}, ReceptionId{100}};
  MockTxPhy tx_phy;
  MockChannel channel{first_arrival_delay};
  MockNoise noise;
  MockRxPhy rx_phy;
  TransmissionExecutor transmitter{ids, tx_phy, channel};
  ReceiverProcessor receiver{ids, noise, rx_phy};
  CommitService commit{store};
  InFlightSignalLedger ledger;
  ReceptionResultAccumulator results;
  CycleSignalRuntime runtime{transmitter,
                             receiver,
                             working,
                             commit,
                             ledger,
                             results,
                             SnapshotVersion{0},
                             Seconds(10)};
  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway};
  PlanInstaller installer{dispatcher};
  FixtureExecutionHook runtime_hook{dispatcher, runtime};
  CycleCoordinator coordinator{installer, runtime_hook};

  const auto installed = [&]() -> Result<
      ns3_factory::kernel::internal::InstalledPlanEvents> {
    auto plan = MakePlan();
    if(!plan) {
      return std::unexpected(plan.error());
    }
    return coordinator.InstallPlan(*plan, SnapshotVersion{0});
  }();
  // The ProtocolCyclePlan and its MacPlan vector are dead before Run().
  if(!installed || installed->tx_start_keys.size() != 1 ||
     installed->tx_start_keys[0].phase != EventPhase::kTxStart ||
     installed->cycle_close_key.phase != EventPhase::kCycleClose ||
     dispatcher.pending_event_count() != 2) {
    gateway.Destroy();
    return false;
  }

  const auto run = dispatcher.Run();
  if(expect_zero_delay_failure) {
    const bool failed_as_frozen =
        !run && run.error().code == ErrorCode::kFailedPrecondition &&
        coordinator.state() == CycleCoordinatorState::kActive &&
        runtime_hook.tx_start_count == 1 &&
        runtime_hook.transmission_session_count == 1 &&
        runtime_hook.transmission_count == 1 &&
        runtime_hook.tx_emission_count == 1 &&
        runtime_hook.received_signal_count == 3 &&
        runtime_hook.arrival_count == 0 &&
        runtime_hook.finalize_count == 0 &&
        runtime_hook.cycle_close_count == 0 && tx_phy.encode_count() == 1 &&
        channel.query_count() == 3 && noise.query_count() == 0 &&
        rx_phy.decode_count() == 0 && results.sessions().empty() &&
        ledger.empty() &&
        store.current_snapshot().version() == SnapshotVersion{0};
    gateway.Destroy();
    return failed_as_frozen;
  }

  const auto double_close =
      coordinator.OnCycleClose(*expected_timing, Seconds(10));
  const auto sessions = results.sessions();
  const bool success =
      run && coordinator.state() == CycleCoordinatorState::kCompleted &&
      runtime_hook.tx_start_count == 1 &&
      runtime_hook.transmission_session_count == 1 &&
      runtime_hook.transmission_count == 1 &&
      runtime_hook.tx_emission_count == 1 &&
      runtime_hook.received_signal_count == 3 &&
      runtime_hook.arrival_count == 3 &&
      runtime_hook.finalize_count == 3 &&
      runtime_hook.cycle_close_count == 1 && tx_phy.encode_count() == 1 &&
      channel.query_count() == 3 && noise.query_count() == 3 &&
      rx_phy.decode_count() == 3 && sessions.size() == 3 && ledger.empty() &&
      sessions[0].reception().receiver_node_id == NodeId{1} &&
      sessions[1].reception().receiver_node_id == NodeId{3} &&
      sessions[2].reception().receiver_node_id == NodeId{4} &&
      sessions[2].decode_result().outcome() == DecodeOutcome::kNotDecoded &&
      store.current_snapshot().version() == SnapshotVersion{1} &&
      store.current_snapshot().committed_at() == Seconds(10) &&
      !double_close && double_close.error().code == ErrorCode::kAlreadyExists;
  gateway.Destroy();
  return success;
}

}  // namespace

auto main() -> int {
  if(!RunPlanDrivenCycle(DurationSeconds(1), false) ||
     !RunPlanDrivenCycle(SimDuration::Zero(), true)) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
