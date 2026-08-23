// ON-only cross-module integration fixture; production runtime remains ns-3-free.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/event_dispatcher.hpp"
#include "internal/ns3_kernel_gateway.hpp"
#include "internal/communication_id_allocator.hpp"
#include "internal/commit_service.hpp"
#include "internal/cycle_signal_runtime.hpp"
#include "internal/cycle_working_state.hpp"
#include "internal/in_flight_signal_ledger.hpp"
#include "internal/receiver_processor.hpp"
#include "internal/reception_result_accumulator.hpp"
#include "internal/transmission_executor.hpp"
#include "internal/world_state_store.hpp"

using ns3_factory::contracts::BroadcastDestination;
using ns3_factory::contracts::BroadcastTransmissionTarget;
using ns3_factory::contracts::ChannelFieldResponse;
using ns3_factory::contracts::ChannelFieldOutcome;
using ns3_factory::contracts::ChannelQuery;
using ns3_factory::contracts::DecodeOutcome;
using ns3_factory::contracts::DigitalPacket;
using ns3_factory::contracts::DuplexMode;
using ns3_factory::contracts::Error;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::IChannelFieldProvider;
using ns3_factory::contracts::INoiseFieldProvider;
using ns3_factory::contracts::IRxPhy;
using ns3_factory::contracts::ITxPhy;
using ns3_factory::contracts::MotionState;
using ns3_factory::contracts::NodeCapabilityProfile;
using ns3_factory::contracts::NodeCommittedState;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::NoiseObservation;
using ns3_factory::contracts::NoiseQuery;
using ns3_factory::contracts::PacketId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::ReceivedSignal;
using ns3_factory::contracts::ReceptionId;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::RxDecodeRequest;
using ns3_factory::contracts::RxDecodeResult;
using ns3_factory::contracts::SimDuration;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::TransmissionId;
using ns3_factory::contracts::TxEmission;
using ns3_factory::contracts::TxEncodeRequest;
using ns3_factory::contracts::TxOpportunity;
using ns3_factory::contracts::Velocity3d;
using ns3_factory::contracts::WorldSnapshot;
using ns3_factory::kernel::internal::CycleCloseEvent;
using ns3_factory::kernel::internal::EventDispatcher;
using ns3_factory::kernel::internal::Ns3KernelGateway;
using ns3_factory::kernel::internal::SessionFinalizeEvent;
using ns3_factory::kernel::internal::SignalArrivalEvent;
using ns3_factory::kernel::internal::TxStartEvent;
using ns3_factory::runtime::internal::CommunicationIdAllocator;
using ns3_factory::runtime::internal::CommitService;
using ns3_factory::runtime::internal::CycleSignalRuntime;
using ns3_factory::runtime::internal::CycleWorkingState;
using ns3_factory::runtime::internal::InFlightSignalLedger;
using ns3_factory::runtime::internal::ReceiverProcessor;
using ns3_factory::runtime::internal::ReceptionResultAccumulator;
using ns3_factory::runtime::internal::TransmissionExecutionRequest;
using ns3_factory::runtime::internal::TransmissionExecutor;
using ns3_factory::runtime::internal::WorldStateStore;

constexpr auto Seconds(std::int64_t value) -> SimTime {
  return SimTime::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto DurationSeconds(std::int64_t value) -> SimDuration {
  return SimDuration::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto MakeNode(std::uint64_t id,
                        double x_meters,
                        double velocity) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{x_meters, 0.0, -10.0},
                  Velocity3d{velocity, 0.0, 0.0}}};
}

auto MakeInitialSnapshot() -> Result<WorldSnapshot> {
  return WorldSnapshot::Create(
      SnapshotVersion{0},
      SimTime::Zero(),
      std::vector<NodeCommittedState>{
          MakeNode(4, 40.0, 0.0),
          MakeNode(3, 30.0, 0.0),
          MakeNode(1, 10.0, 0.0),
          MakeNode(0, 0.0, 0.0)});
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
  [[nodiscard]] auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldOutcome> override {
    ++query_count_;
    return ChannelFieldResponse::Create(query.transmission_id(),
                                        query.receiver_node_id(),
                                        70.0,
                                        DurationSeconds(1),
                                        {});
  }

  [[nodiscard]] auto query_count() const noexcept -> std::size_t {
    return query_count_;
  }

 private:
  mutable std::size_t query_count_{0};
};

class MockNoise final : public INoiseFieldProvider {
 public:
  [[nodiscard]] auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    ++query_count_;
    return NoiseObservation::Create(
        query.receiver_node_id(),
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

auto MakeBroadcastRequest() -> TransmissionExecutionRequest {
  return TransmissionExecutionRequest{
      TxOpportunity{NodeId{0}, Seconds(1)},
      DigitalPacket{PacketId{77},
                    NodeId{0},
                    BroadcastDestination{},
                    std::vector<std::byte>{std::byte{0x5A}}},
      BroadcastTransmissionTarget{},
      Seconds(1),
      {NodeId{4}, NodeId{1}, NodeId{3}}};
}

auto MakeSignal(TransmissionId transmission_id,
                SimTime arrival_at,
                SimDuration duration) -> Result<ReceivedSignal> {
  auto emission = TxEmission::Create(transmission_id,
                                     PacketId{transmission_id.value() + 100},
                                     NodeId{0},
                                     arrival_at,
                                     duration,
                                     25'000.0,
                                     4'000.0,
                                     180.0);
  if(!emission) {
    return std::unexpected(emission.error());
  }
  auto response = ChannelFieldResponse::Create(
      transmission_id, NodeId{1}, 70.0, SimDuration::Zero(), {});
  if(!response) {
    return std::unexpected(response.error());
  }
  return ReceivedSignal::Create(*emission, *response);
}

auto TestBroadcastFullClosure() -> bool {
  auto snapshot = MakeInitialSnapshot();
  if(!snapshot) {
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
  MockChannel channel;
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
  std::size_t transmission_sessions = 0;
  std::size_t arrival_events = 0;
  std::size_t finalize_events = 0;
  bool arrival_did_not_decode = true;
  bool finalize_did_not_commit = true;

  const auto close = dispatcher.Schedule(CycleCloseEvent{
      Seconds(10),
      [&]() -> ns3_factory::contracts::Status {
        const auto now = gateway.PlatformNow();
        if(!now || *now != Seconds(10)) {
          return std::unexpected(
              Error{ErrorCode::kFailedPrecondition,
                    "CycleClose did not run at PlatformNow"});
        }
        return runtime.HandleCycleClose(*now);
      }});
  const auto tx_start = dispatcher.Schedule(TxStartEvent{
      Seconds(1),
      [&]() -> ns3_factory::contracts::Status {
        const auto now = gateway.PlatformNow();
        if(!now || *now != Seconds(1)) {
          return std::unexpected(
              Error{ErrorCode::kFailedPrecondition,
                    "TxStart did not run at PlatformNow"});
        }
        auto session = runtime.HandleTxStart(*now, MakeBroadcastRequest());
        if(!session) {
          return std::unexpected(session.error());
        }
        ++transmission_sessions;

        for(const auto& signal : session->received_signals()) {
          const auto arrival = dispatcher.Schedule(SignalArrivalEvent{
              signal.first_arrival_at(),
              [&, signal]() -> ns3_factory::contracts::Status {
                const auto callback_now = gateway.PlatformNow();
                if(!callback_now ||
                   *callback_now != signal.first_arrival_at()) {
                  return std::unexpected(
                      Error{ErrorCode::kFailedPrecondition,
                            "SignalArrival did not run at first arrival"});
                }
                ++arrival_events;
                arrival_did_not_decode =
                    arrival_did_not_decode && rx_phy.decode_count() == 0;
                return runtime.HandleSignalArrival(*callback_now, signal);
              }});
          if(!arrival) {
            return std::unexpected(arrival.error());
          }

          const auto finalize = dispatcher.Schedule(SessionFinalizeEvent{
              signal.last_effect_end_at(),
              [&, signal]() -> ns3_factory::contracts::Status {
                const auto callback_now = gateway.PlatformNow();
                if(!callback_now ||
                   *callback_now != signal.last_effect_end_at()) {
                  return std::unexpected(
                      Error{ErrorCode::kFailedPrecondition,
                            "SessionFinalize did not run at signal end"});
                }
                ++finalize_events;
                finalize_did_not_commit =
                    finalize_did_not_commit &&
                    store.current_snapshot().version() == SnapshotVersion{0};
                return runtime.HandleSessionFinalize(*callback_now, signal);
              }});
          if(!finalize) {
            return std::unexpected(finalize.error());
          }
        }
        return {};
      }});
  if(!close || !tx_start) {
    gateway.Destroy();
    return false;
  }

  const auto run_status = dispatcher.Run();
  const auto duplicate_close = runtime.HandleCycleClose(Seconds(10));
  const auto& committed = store.current_snapshot();
  const auto sessions = results.sessions();
  const bool success =
      run_status && transmission_sessions == 1 && tx_phy.encode_count() == 1 &&
      channel.query_count() == 3 && arrival_events == 3 &&
      finalize_events == 3 && noise.query_count() == 3 &&
      rx_phy.decode_count() == 3 && sessions.size() == 3 &&
      arrival_did_not_decode && finalize_did_not_commit && ledger.empty() &&
      committed.version() == SnapshotVersion{1} &&
      committed.committed_at() == Seconds(10) && !duplicate_close &&
      duplicate_close.error().code == ErrorCode::kAlreadyExists &&
      sessions[0].reception().receiver_node_id == NodeId{1} &&
      sessions[1].reception().receiver_node_id == NodeId{3} &&
      sessions[2].reception().receiver_node_id == NodeId{4} &&
      sessions[2].decode_result().outcome() == DecodeOutcome::kNotDecoded;
  gateway.Destroy();
  return success;
}

auto RunSignalPair(bool touching) -> bool {
  auto snapshot = MakeInitialSnapshot();
  auto first = MakeSignal(TransmissionId{10},
                          Seconds(1),
                          touching ? DurationSeconds(1)
                                   : DurationSeconds(3));
  auto second = MakeSignal(TransmissionId{11},
                           Seconds(2),
                           DurationSeconds(1));
  if(!snapshot || !first || !second) {
    return false;
  }
  const auto close_time = touching ? Seconds(3) : Seconds(5);
  WorldStateStore store{*snapshot};
  auto working_result = CycleWorkingState::Create(
      store.current_snapshot(), PlanningCycleId{1}, SimTime::Zero());
  if(!working_result) {
    return false;
  }
  auto working = std::move(*working_result);
  CommunicationIdAllocator ids{TransmissionId{1}, ReceptionId{200}};
  MockTxPhy tx_phy;
  MockChannel channel;
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
                             close_time};
  Ns3KernelGateway gateway;
  EventDispatcher dispatcher{gateway};
  std::vector<int> boundary_order;

  const auto schedule_arrival = [&](const ReceivedSignal& signal,
                                    int boundary_marker) {
    return dispatcher.Schedule(SignalArrivalEvent{
        signal.first_arrival_at(),
        [&, signal, boundary_marker]() -> ns3_factory::contracts::Status {
          const auto now = gateway.PlatformNow();
          if(!now) {
            return std::unexpected(now.error());
          }
          if(boundary_marker != 0) {
            boundary_order.push_back(boundary_marker);
          }
          return runtime.HandleSignalArrival(*now, signal);
        }});
  };
  const auto schedule_finalize = [&](const ReceivedSignal& signal,
                                     int boundary_marker) {
    return dispatcher.Schedule(SessionFinalizeEvent{
        signal.last_effect_end_at(),
        [&, signal, boundary_marker]() -> ns3_factory::contracts::Status {
          const auto now = gateway.PlatformNow();
          if(!now) {
            return std::unexpected(now.error());
          }
          if(boundary_marker != 0) {
            boundary_order.push_back(boundary_marker);
          }
          return runtime.HandleSessionFinalize(*now, signal);
        }});
  };

  // Intentionally schedule the boundary arrival before the boundary finalize;
  // EventPhase must still place finalize first.
  const auto second_arrival = schedule_arrival(*second, touching ? 20 : 0);
  const auto first_finalize = schedule_finalize(*first, touching ? 10 : 0);
  const auto first_arrival = schedule_arrival(*first, 0);
  const auto second_finalize = schedule_finalize(*second, 0);
  const auto close = dispatcher.Schedule(CycleCloseEvent{
      close_time,
      [&]() -> ns3_factory::contracts::Status {
        const auto now = gateway.PlatformNow();
        if(!now) {
          return std::unexpected(now.error());
        }
        return runtime.HandleCycleClose(*now);
      }});
  if(!second_arrival || !first_finalize || !first_arrival ||
     !second_finalize || !close) {
    gateway.Destroy();
    return false;
  }

  const auto run_status = dispatcher.Run();
  const auto sessions = results.sessions();
  const bool overlap_expectation =
      sessions.size() == 2 &&
      sessions[0].receiver_window().overlapping_signals().size() ==
          (touching ? 0U : 1U) &&
      sessions[1].receiver_window().overlapping_signals().size() ==
          (touching ? 0U : 1U);
  const bool phase_expectation =
      !touching || boundary_order == std::vector<int>{10, 20};
  const bool success = run_status && overlap_expectation &&
                       phase_expectation && noise.query_count() == 2 &&
                       rx_phy.decode_count() == 2 && ledger.empty() &&
                       store.current_snapshot().version() ==
                           SnapshotVersion{1} &&
                       store.current_snapshot().committed_at() == close_time;
  gateway.Destroy();
  return success;
}

auto TestCycleCloseProtectionAndEarlyRejection() -> bool {
  auto snapshot = MakeInitialSnapshot();
  if(!snapshot) {
    return false;
  }
  WorldStateStore store{*snapshot};
  auto working_result = CycleWorkingState::Create(
      store.current_snapshot(), PlanningCycleId{2}, SimTime::Zero());
  if(!working_result) {
    return false;
  }
  auto working = std::move(*working_result);
  CommunicationIdAllocator ids{TransmissionId{300}, ReceptionId{300}};
  MockTxPhy tx_phy;
  MockChannel channel;
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
                             Seconds(2)};

  auto request = MakeBroadcastRequest();
  request.candidate_receivers = {NodeId{1}};
  const auto request_time = request.started_at;
  const auto rejected = runtime.HandleTxStart(
      request_time, std::move(request));
  auto no_receivers = MakeBroadcastRequest();
  no_receivers.started_at = Seconds(2);
  no_receivers.candidate_receivers.clear();
  const auto no_receiver_time = no_receivers.started_at;
  const auto zero_receiver_rejected = runtime.HandleTxStart(
      no_receiver_time, std::move(no_receivers));
  auto active = MakeSignal(
      TransmissionId{999}, Seconds(1), DurationSeconds(2));
  if(rejected || rejected.error().code != ErrorCode::kFailedPrecondition ||
     zero_receiver_rejected ||
     zero_receiver_rejected.error().code !=
         ErrorCode::kFailedPrecondition ||
     !active || !ledger.Insert(*active)) {
    return false;
  }
  const auto close = runtime.HandleCycleClose(Seconds(2));
  return !close && close.error().code == ErrorCode::kFailedPrecondition &&
         store.current_snapshot().version() == SnapshotVersion{0} &&
         !ledger.empty();
}

auto main() -> int {
  if(!TestBroadcastFullClosure() || !RunSignalPair(true) ||
     !RunSignalPair(false) ||
     !TestCycleCloseProtectionAndEarlyRejection()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
