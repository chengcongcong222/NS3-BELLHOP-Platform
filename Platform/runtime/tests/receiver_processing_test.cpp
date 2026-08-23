#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>

#include "internal/communication_id_allocator.hpp"
#include "internal/in_flight_signal_ledger.hpp"
#include "internal/receiver_processor.hpp"
#include "internal/receiver_window_builder.hpp"
#include "internal/reception_session.hpp"
#include "internal/transmission_executor.hpp"

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
using ns3_factory::contracts::PropagationPath;
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
using ns3_factory::runtime::internal::CommunicationIdAllocator;
using ns3_factory::runtime::internal::CycleWorkingState;
using ns3_factory::runtime::internal::InFlightSignalLedger;
using ns3_factory::runtime::internal::ReceiverProcessor;
using ns3_factory::runtime::internal::ReceiverWindowBuilder;
using ns3_factory::runtime::internal::ReceptionSession;
using ns3_factory::runtime::internal::TransmissionExecutionRequest;
using ns3_factory::runtime::internal::TransmissionExecutor;

template <typename T>
concept HasCommitMethod = requires(T& value) {
  value.Commit();
};

template <typename T>
concept HasScheduleMethod = requires(T& value) {
  value.Schedule();
};

static_assert(!std::is_copy_constructible_v<CommunicationIdAllocator>);
static_assert(!std::is_move_constructible_v<CommunicationIdAllocator>);
static_assert(!HasCommitMethod<InFlightSignalLedger>);
static_assert(!HasScheduleMethod<InFlightSignalLedger>);
static_assert(!HasCommitMethod<ReceiverProcessor>);
static_assert(!HasScheduleMethod<ReceiverProcessor>);
static_assert(requires(CommunicationIdAllocator& allocator) {
  { allocator.NextReceptionId() } -> std::same_as<Result<ReceptionId>>;
});
static_assert(requires(InFlightSignalLedger& ledger,
                       ReceivedSignal signal,
                       NodeId receiver) {
  { ledger.Insert(std::move(signal)) } ->
      std::same_as<ns3_factory::contracts::Status>;
  { ledger.SignalsForReceiver(receiver) } ->
      std::same_as<std::span<const ReceivedSignal>>;
});

constexpr auto Seconds(std::int64_t value) -> SimTime {
  return SimTime::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto DurationSeconds(std::int64_t value) -> SimDuration {
  return SimDuration::FromNanoseconds(value * 1'000'000'000);
}

auto MakeSignal(TransmissionId transmission_id,
                NodeId receiver_node_id,
                SimTime started_at,
                SimDuration first_arrival_delay,
                SimDuration duration = SimDuration::FromNanoseconds(100),
                double center_frequency_hz = 1'000.0,
                double bandwidth_hz = 100.0,
                bool with_multipath = false) -> Result<ReceivedSignal> {
  auto emission = TxEmission::Create(
      transmission_id,
      PacketId{transmission_id.value() + 1'000},
      NodeId{9},
      started_at,
      duration,
      center_frequency_hz,
      bandwidth_hz,
      180.0);
  if(!emission) {
    return std::unexpected(emission.error());
  }

  std::vector<PropagationPath> paths;
  if(with_multipath) {
    auto first = PropagationPath::Create(
        SimDuration::Zero(), 1.0, 0.0);
    auto later = PropagationPath::Create(
        SimDuration::FromNanoseconds(50), 0.5, 0.25);
    if(!first || !later) {
      return std::unexpected(!first ? first.error() : later.error());
    }
    paths.push_back(*later);
    paths.push_back(*first);
  }

  auto response = ChannelFieldResponse::Create(
      transmission_id,
      receiver_node_id,
      70.0,
      first_arrival_delay,
      std::move(paths));
  if(!response) {
    return std::unexpected(response.error());
  }
  return ReceivedSignal::Create(*emission, *response);
}

constexpr auto MakeNode(std::uint64_t id,
                        double x_meters,
                        double x_velocity) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{x_meters, 0.0, -10.0},
                  Velocity3d{x_velocity, 0.0, 0.0}}};
}

auto MakeWorkingState() -> Result<CycleWorkingState> {
  auto snapshot = WorldSnapshot::Create(
      SnapshotVersion{5},
      SimTime::Zero(),
      std::vector<NodeCommittedState>{
          MakeNode(4, 40.0, 5.0),
          MakeNode(3, 30.0, 4.0),
          MakeNode(1, 10.0, 2.0),
          MakeNode(0, 0.0, 1.0)});
  if(!snapshot) {
    return std::unexpected(snapshot.error());
  }
  return CycleWorkingState::Create(
      *snapshot, PlanningCycleId{9}, SimTime::Zero());
}

enum class NoiseMode {
  kValid,
  kIdentityMismatch,
  kError,
};

class MockNoiseFieldProvider final : public INoiseFieldProvider {
 public:
  explicit MockNoiseFieldProvider(
      NoiseMode mode = NoiseMode::kValid) noexcept
      : mode_(mode) {}

  [[nodiscard]] auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    queries_.push_back(query);
    if(mode_ == NoiseMode::kError) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable,
                "test fixture noise provider failure"});
    }
    const auto receiver = mode_ == NoiseMode::kIdentityMismatch
                              ? NodeId{999}
                              : query.receiver_node_id();
    return NoiseObservation::Create(
        receiver,
        query.observed_from(),
        query.observed_until(),
        query.lower_frequency_hz(),
        query.upper_frequency_hz(),
        45.0);
  }

  [[nodiscard]] auto queries() const noexcept
      -> const std::vector<NoiseQuery>& {
    return queries_;
  }

 private:
  NoiseMode mode_;
  mutable std::vector<NoiseQuery> queries_;
};

enum class RxMode {
  kDecoded,
  kNotDecoded,
  kIdentityMismatch,
  kError,
};

class MockRxPhy final : public IRxPhy {
 public:
  explicit MockRxPhy(RxMode mode = RxMode::kDecoded) noexcept
      : mode_(mode) {}

  [[nodiscard]] auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    ++decode_count_;
    const auto& desired = request.receiver_window().desired_signal();
    decoded_transmissions_.push_back(desired.transmission_id());
    if(mode_ == RxMode::kError) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable,
                "test fixture Rx PHY failure"});
    }

    const auto transmission = mode_ == RxMode::kIdentityMismatch
                                  ? TransmissionId{
                                        desired.transmission_id().value() + 1}
                                  : desired.transmission_id();
    const auto outcome = mode_ == RxMode::kNotDecoded
                             ? DecodeOutcome::kNotDecoded
                             : DecodeOutcome::kDecoded;
    return RxDecodeResult::Create(transmission,
                                  desired.emission().packet_id(),
                                  desired.receiver_node_id(),
                                  outcome);
  }

  [[nodiscard]] auto decode_count() const noexcept -> std::size_t {
    return decode_count_;
  }

 private:
  RxMode mode_;
  mutable std::size_t decode_count_{0};
  mutable std::vector<TransmissionId> decoded_transmissions_;
};

auto TestReceptionIdAllocator() -> bool {
  CommunicationIdAllocator transmission_only{TransmissionId{1}};
  const auto unconfigured = transmission_only.NextReceptionId();
  if(unconfigured ||
     unconfigured.error().code != ErrorCode::kFailedPrecondition) {
    return false;
  }

  CommunicationIdAllocator allocator{TransmissionId{10}, ReceptionId{41}};
  const auto first = allocator.NextReceptionId();
  const auto second = allocator.NextReceptionId();
  if(!first || !second || *first != ReceptionId{41} ||
     *second != ReceptionId{42}) {
    return false;
  }

  constexpr auto kMaximum =
      std::numeric_limits<ReceptionId::value_type>::max();
  CommunicationIdAllocator boundary{TransmissionId{20},
                                    ReceptionId{kMaximum}};
  const auto maximum = boundary.NextReceptionId();
  const auto overflow = boundary.NextReceptionId();
  return maximum && *maximum == ReceptionId{kMaximum} && !overflow &&
         overflow.error().code == ErrorCode::kOverflow;
}

auto TestLedgerIdentityAndOrdering() -> bool {
  auto t30_n1 = MakeSignal(TransmissionId{30}, NodeId{1},
                           SimTime::FromNanoseconds(30),
                           SimDuration::Zero());
  auto t20_n1 = MakeSignal(TransmissionId{20}, NodeId{1},
                           SimTime::FromNanoseconds(10),
                           SimDuration::Zero());
  auto t10_n1 = MakeSignal(TransmissionId{10}, NodeId{1},
                           SimTime::FromNanoseconds(10),
                           SimDuration::Zero());
  auto t30_n3 = MakeSignal(TransmissionId{30}, NodeId{3},
                           SimTime::FromNanoseconds(5),
                           SimDuration::Zero());
  if(!t30_n1 || !t20_n1 || !t10_n1 || !t30_n3) {
    return false;
  }

  InFlightSignalLedger ledger;
  if(!ledger.Insert(*t30_n1) || !ledger.Insert(*t20_n1) ||
     !ledger.Insert(*t10_n1) || !ledger.Insert(*t30_n3)) {
    return false;
  }
  const auto duplicate = ledger.Insert(*t30_n1);
  const auto n1 = ledger.SignalsForReceiver(NodeId{1});
  const auto n3 = ledger.SignalsForReceiver(NodeId{3});
  return !duplicate && duplicate.error().code == ErrorCode::kAlreadyExists &&
         ledger.size() == 4 && n1.size() == 3 && n3.size() == 1 &&
         n1[0].transmission_id() == TransmissionId{10} &&
         n1[1].transmission_id() == TransmissionId{20} &&
         n1[2].transmission_id() == TransmissionId{30} &&
         n3[0].transmission_id() == TransmissionId{30};
}

auto TestReceiverWindowSelectionAndSymmetry() -> bool {
  auto desired = MakeSignal(TransmissionId{10}, NodeId{1},
                            SimTime::Zero(), SimDuration::Zero());
  auto overlap = MakeSignal(TransmissionId{11}, NodeId{1},
                            SimTime::FromNanoseconds(50),
                            SimDuration::Zero());
  auto other_band = MakeSignal(TransmissionId{12}, NodeId{1},
                               SimTime::Zero(), SimDuration::Zero(),
                               SimDuration::FromNanoseconds(100),
                               2'000.0, 100.0);
  auto later = MakeSignal(TransmissionId{13}, NodeId{1},
                          SimTime::FromNanoseconds(200),
                          SimDuration::Zero());
  auto other_receiver = MakeSignal(TransmissionId{14}, NodeId{3},
                                   SimTime::FromNanoseconds(50),
                                   SimDuration::Zero());
  auto isolated = MakeSignal(TransmissionId{15}, NodeId{1},
                             SimTime::FromNanoseconds(500),
                             SimDuration::Zero());
  if(!desired || !overlap || !other_band || !later || !other_receiver ||
     !isolated) {
    return false;
  }

  InFlightSignalLedger ledger;
  if(!ledger.Insert(*other_receiver) || !ledger.Insert(*other_band) ||
     !ledger.Insert(*later) || !ledger.Insert(*overlap) ||
     !ledger.Insert(*desired) || !ledger.Insert(*isolated)) {
    return false;
  }

  const auto desired_window = ReceiverWindowBuilder::Build(*desired, ledger);
  const auto overlap_window = ReceiverWindowBuilder::Build(*overlap, ledger);
  const auto isolated_window = ReceiverWindowBuilder::Build(*isolated, ledger);
  auto missing = MakeSignal(TransmissionId{99}, NodeId{1},
                            SimTime::Zero(), SimDuration::Zero());
  const auto missing_window =
      missing ? ReceiverWindowBuilder::Build(*missing, ledger)
              : Result<ns3_factory::contracts::ReceiverWindow>{
                    std::unexpected(missing.error())};
  return desired_window && overlap_window && isolated_window &&
         desired_window->overlapping_signals().size() == 1 &&
         desired_window->overlapping_signals()[0].transmission_id() ==
             TransmissionId{11} &&
         overlap_window->overlapping_signals().size() == 1 &&
         overlap_window->overlapping_signals()[0].transmission_id() ==
             TransmissionId{10} &&
         isolated_window->overlapping_signals().empty() && !missing_window &&
         missing_window.error().code == ErrorCode::kNotFound;
}

auto TestReceiverWindowOrdering() -> bool {
  auto desired = MakeSignal(TransmissionId{100},
                            NodeId{1},
                            SimTime::Zero(),
                            SimDuration::Zero(),
                            SimDuration::FromNanoseconds(500));
  auto t30 = MakeSignal(TransmissionId{30}, NodeId{1},
                        SimTime::FromNanoseconds(30),
                        SimDuration::Zero());
  auto t20 = MakeSignal(TransmissionId{20}, NodeId{1},
                        SimTime::FromNanoseconds(10),
                        SimDuration::Zero());
  auto t10 = MakeSignal(TransmissionId{10}, NodeId{1},
                        SimTime::FromNanoseconds(10),
                        SimDuration::Zero());
  if(!desired || !t30 || !t20 || !t10) {
    return false;
  }

  InFlightSignalLedger ledger;
  if(!ledger.Insert(*t30) || !ledger.Insert(*desired) ||
     !ledger.Insert(*t20) || !ledger.Insert(*t10)) {
    return false;
  }
  const auto window = ReceiverWindowBuilder::Build(*desired, ledger);
  return window && window->overlapping_signals().size() == 3 &&
         window->overlapping_signals()[0].transmission_id() ==
             TransmissionId{10} &&
         window->overlapping_signals()[1].transmission_id() ==
             TransmissionId{20} &&
         window->overlapping_signals()[2].transmission_id() ==
             TransmissionId{30};
}

auto TestProcessingRetentionGeometryAndArrival() -> bool {
  auto working = MakeWorkingState();
  auto t10 = MakeSignal(TransmissionId{10},
                        NodeId{1},
                        Seconds(4),
                        DurationSeconds(1),
                        DurationSeconds(1));
  auto t11 = MakeSignal(TransmissionId{11},
                        NodeId{1},
                        Seconds(4),
                        DurationSeconds(1),
                        DurationSeconds(1));
  if(!working || !t10 || !t11 ||
     !working->UpdateVelocity(
         NodeId{1}, Velocity3d{10.0, 0.0, 0.0}, Seconds(2))) {
    return false;
  }

  InFlightSignalLedger ledger;
  if(!ledger.Insert(*t11) || !ledger.Insert(*t10)) {
    return false;
  }
  CommunicationIdAllocator allocator{TransmissionId{100}, ReceptionId{500}};
  MockNoiseFieldProvider noise;
  MockRxPhy rx;
  const ReceiverProcessor processor{allocator, noise, rx};

  const auto first = processor.ProcessReceivedSignal(*t10, ledger, *working);
  const auto second = processor.ProcessReceivedSignal(*t11, ledger, *working);
  if(!first || !second || noise.queries().size() != 2 ||
     rx.decode_count() != 2 || ledger.size() != 2 ||
     first->receiver_window().overlapping_signals().size() != 1 ||
     second->receiver_window().overlapping_signals().size() != 1 ||
     noise.queries()[0].receiver_position().x_meters != 44.0 ||
     first->reception().arrival_at != Seconds(5) ||
     first->reception().reception_id != ReceptionId{500} ||
     second->reception().reception_id != ReceptionId{501}) {
    return false;
  }

  const auto base_node = working->base_snapshot().FindNode(NodeId{1});
  return base_node && base_node->get().motion.position.x_meters == 10.0;
}

auto TestDecodedAndNotDecodedOutcomes() -> bool {
  auto working = MakeWorkingState();
  auto signal = MakeSignal(TransmissionId{20}, NodeId{0},
                           Seconds(1), DurationSeconds(1),
                           DurationSeconds(1));
  if(!working || !signal) {
    return false;
  }
  InFlightSignalLedger ledger;
  if(!ledger.Insert(*signal)) {
    return false;
  }

  CommunicationIdAllocator decoded_ids{TransmissionId{1}, ReceptionId{600}};
  MockNoiseFieldProvider decoded_noise;
  MockRxPhy decoded_rx{RxMode::kDecoded};
  const ReceiverProcessor decoded_processor{
      decoded_ids, decoded_noise, decoded_rx};
  const auto decoded = decoded_processor.ProcessReceivedSignal(
      *signal, ledger, *working);

  CommunicationIdAllocator not_decoded_ids{TransmissionId{1},
                                           ReceptionId{700}};
  MockNoiseFieldProvider not_decoded_noise;
  MockRxPhy not_decoded_rx{RxMode::kNotDecoded};
  const ReceiverProcessor not_decoded_processor{
      not_decoded_ids, not_decoded_noise, not_decoded_rx};
  const auto not_decoded = not_decoded_processor.ProcessReceivedSignal(
      *signal, ledger, *working);

  return decoded && not_decoded &&
         decoded->decode_result().outcome() == DecodeOutcome::kDecoded &&
         not_decoded->decode_result().outcome() ==
             DecodeOutcome::kNotDecoded &&
         decoded->reception().receiver_node_id == NodeId{0} &&
         decoded_noise.queries().size() == 1 &&
         not_decoded_noise.queries().size() == 1 &&
         decoded_rx.decode_count() == 1 &&
         not_decoded_rx.decode_count() == 1;
}

auto TestProviderAndIdentityFailuresAllocateLate() -> bool {
  auto working = MakeWorkingState();
  auto signal = MakeSignal(TransmissionId{30}, NodeId{1},
                           Seconds(1), DurationSeconds(1),
                           DurationSeconds(1));
  if(!working || !signal) {
    return false;
  }
  InFlightSignalLedger ledger;
  if(!ledger.Insert(*signal)) {
    return false;
  }

  {
    CommunicationIdAllocator ids{TransmissionId{1}, ReceptionId{800}};
    MockNoiseFieldProvider failing_noise{NoiseMode::kError};
    MockRxPhy unused_rx;
    const ReceiverProcessor failing{ids, failing_noise, unused_rx};
    const auto failure =
        failing.ProcessReceivedSignal(*signal, ledger, *working);
    MockNoiseFieldProvider valid_noise;
    MockRxPhy valid_rx;
    const ReceiverProcessor retry{ids, valid_noise, valid_rx};
    const auto success = retry.ProcessReceivedSignal(*signal, ledger, *working);
    if(failure || failure.error().code != ErrorCode::kUnavailable ||
       !success || success->reception().reception_id != ReceptionId{800} ||
       failing_noise.queries().size() != 1 || unused_rx.decode_count() != 0) {
      return false;
    }
  }

  {
    CommunicationIdAllocator ids{TransmissionId{1}, ReceptionId{810}};
    MockNoiseFieldProvider noise;
    MockRxPhy failing_rx{RxMode::kError};
    const ReceiverProcessor failing{ids, noise, failing_rx};
    const auto failure =
        failing.ProcessReceivedSignal(*signal, ledger, *working);
    MockNoiseFieldProvider retry_noise;
    MockRxPhy retry_rx;
    const ReceiverProcessor retry{ids, retry_noise, retry_rx};
    const auto success = retry.ProcessReceivedSignal(*signal, ledger, *working);
    if(failure || failure.error().code != ErrorCode::kUnavailable ||
       !success || success->reception().reception_id != ReceptionId{810} ||
       failing_rx.decode_count() != 1) {
      return false;
    }
  }

  {
    CommunicationIdAllocator ids{TransmissionId{1}, ReceptionId{820}};
    MockNoiseFieldProvider mismatched_noise{NoiseMode::kIdentityMismatch};
    MockRxPhy unused_rx;
    const ReceiverProcessor processor{ids, mismatched_noise, unused_rx};
    const auto failure =
        processor.ProcessReceivedSignal(*signal, ledger, *working);
    if(failure || failure.error().code != ErrorCode::kFailedPrecondition ||
       unused_rx.decode_count() != 0) {
      return false;
    }
  }

  CommunicationIdAllocator ids{TransmissionId{1}, ReceptionId{830}};
  MockNoiseFieldProvider noise;
  MockRxPhy mismatched_rx{RxMode::kIdentityMismatch};
  const ReceiverProcessor processor{ids, noise, mismatched_rx};
  const auto failure =
      processor.ProcessReceivedSignal(*signal, ledger, *working);
  return !failure &&
         failure.error().code == ErrorCode::kFailedPrecondition &&
         noise.queries().size() == 1 && mismatched_rx.decode_count() == 1;
}

auto TestReceptionOverflowAndMultipath() -> bool {
  auto working = MakeWorkingState();
  auto first_signal = MakeSignal(TransmissionId{40}, NodeId{1},
                                 Seconds(1), DurationSeconds(1),
                                 DurationSeconds(1), 1'000.0, 100.0, true);
  auto second_signal = MakeSignal(TransmissionId{41}, NodeId{3},
                                  Seconds(1), DurationSeconds(1),
                                  DurationSeconds(1));
  if(!working || !first_signal || !second_signal) {
    return false;
  }
  InFlightSignalLedger ledger;
  if(!ledger.Insert(*first_signal) || !ledger.Insert(*second_signal)) {
    return false;
  }

  constexpr auto kMaximum =
      std::numeric_limits<ReceptionId::value_type>::max();
  CommunicationIdAllocator ids{TransmissionId{1}, ReceptionId{kMaximum}};
  MockNoiseFieldProvider noise;
  MockRxPhy rx;
  const ReceiverProcessor processor{ids, noise, rx};
  const auto maximum = processor.ProcessReceivedSignal(
      *first_signal, ledger, *working);
  const auto overflow = processor.ProcessReceivedSignal(
      *second_signal, ledger, *working);
  return maximum && maximum->reception().reception_id ==
                        ReceptionId{kMaximum} &&
         maximum->desired_signal().channel_response().paths().size() == 2 &&
         !overflow && overflow.error().code == ErrorCode::kOverflow;
}

class BroadcastTxPhy final : public ITxPhy {
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

class BroadcastChannel final : public IChannelFieldProvider {
 public:
  [[nodiscard]] auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldOutcome> override {
    ++query_count_;
    return ChannelFieldResponse::Create(
        query.transmission_id(),
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

auto TestBroadcastRuntimeClosure() -> bool {
  auto working = MakeWorkingState();
  if(!working) {
    return false;
  }
  CommunicationIdAllocator ids{TransmissionId{50}, ReceptionId{900}};
  BroadcastTxPhy tx_phy;
  BroadcastChannel channel;
  const TransmissionExecutor transmitter{ids, tx_phy, channel};
  TransmissionExecutionRequest request{
      TxOpportunity{NodeId{0}, Seconds(1)},
      DigitalPacket{PacketId{77},
                    NodeId{0},
                    BroadcastDestination{},
                    std::vector<std::byte>{std::byte{0x5A}}},
      BroadcastTransmissionTarget{},
      Seconds(1),
      {NodeId{4}, NodeId{1}, NodeId{3}}};
  const auto transmission =
      transmitter.ExecuteTransmission(*working, std::move(request));
  if(!transmission || tx_phy.encode_count() != 1 ||
     channel.query_count() != 3 ||
     transmission->received_signals().size() != 3) {
    return false;
  }

  InFlightSignalLedger ledger;
  for(const auto& signal : transmission->received_signals()) {
    if(!ledger.Insert(signal)) {
      return false;
    }
  }

  MockNoiseFieldProvider noise;
  MockRxPhy rx;
  const ReceiverProcessor receiver{ids, noise, rx};
  std::vector<ReceptionSession> receptions;
  for(const auto& signal : transmission->received_signals()) {
    auto reception = receiver.ProcessReceivedSignal(signal, ledger, *working);
    if(!reception) {
      return false;
    }
    receptions.push_back(std::move(*reception));
  }

  return receptions.size() == 3 && ledger.size() == 3 &&
         noise.queries().size() == 3 && rx.decode_count() == 3 &&
         tx_phy.encode_count() == 1 && channel.query_count() == 3 &&
         transmission->transmission().transmission_id == TransmissionId{50} &&
         transmission->emission().transmission_id() == TransmissionId{50} &&
         receptions[0].reception().reception_id == ReceptionId{900} &&
         receptions[1].reception().reception_id == ReceptionId{901} &&
         receptions[2].reception().reception_id == ReceptionId{902};
}

auto main() -> int {
  if(!TestReceptionIdAllocator() || !TestLedgerIdentityAndOrdering() ||
     !TestReceiverWindowSelectionAndSymmetry() ||
     !TestReceiverWindowOrdering() ||
     !TestProcessingRetentionGeometryAndArrival() ||
     !TestDecodedAndNotDecodedOutcomes() ||
     !TestProviderAndIdentityFailuresAllocateLate() ||
     !TestReceptionOverflowAndMultipath() ||
     !TestBroadcastRuntimeClosure()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
