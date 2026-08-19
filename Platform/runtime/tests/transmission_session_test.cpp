#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "internal/communication_id_allocator.hpp"
#include "internal/transmission_executor.hpp"
#include "internal/transmission_session.hpp"

using ns3_factory::contracts::BroadcastDestination;
using ns3_factory::contracts::BroadcastTransmissionTarget;
using ns3_factory::contracts::ChannelFieldResponse;
using ns3_factory::contracts::ChannelQuery;
using ns3_factory::contracts::DigitalPacket;
using ns3_factory::contracts::DuplexMode;
using ns3_factory::contracts::Error;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::IChannelFieldProvider;
using ns3_factory::contracts::ITxPhy;
using ns3_factory::contracts::MotionState;
using ns3_factory::contracts::NodeCapabilityProfile;
using ns3_factory::contracts::NodeCommittedState;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PacketId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::SimDuration;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::TransmissionId;
using ns3_factory::contracts::TxEmission;
using ns3_factory::contracts::TxEncodeRequest;
using ns3_factory::contracts::TxOpportunity;
using ns3_factory::contracts::UnicastDestination;
using ns3_factory::contracts::UnicastTransmissionTarget;
using ns3_factory::contracts::Velocity3d;
using ns3_factory::contracts::WorldSnapshot;
using ns3_factory::runtime::internal::CommunicationIdAllocator;
using ns3_factory::runtime::internal::CycleWorkingState;
using ns3_factory::runtime::internal::TransmissionExecutionRequest;
using ns3_factory::runtime::internal::TransmissionExecutor;
using ns3_factory::runtime::internal::TransmissionSession;

template <typename T>
concept HasReceptionId = requires(const T& value) {
  value.reception_id();
};

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
static_assert(!HasReceptionId<TransmissionSession>);
static_assert(!HasCommitMethod<TransmissionExecutor>);
static_assert(!HasScheduleMethod<TransmissionExecutor>);
static_assert(requires(CommunicationIdAllocator& allocator) {
  { allocator.NextTransmissionId() } ->
      std::same_as<Result<TransmissionId>>;
});
static_assert(requires(const TransmissionExecutor& executor,
                       const CycleWorkingState& working_state,
                       TransmissionExecutionRequest request) {
  { executor.ExecuteTransmission(working_state, std::move(request)) } ->
      std::same_as<Result<TransmissionSession>>;
});

enum class EmissionMode {
  kValid,
  kWrongTransmission,
  kWrongPacket,
  kWrongSender,
  kWrongStart,
  kZeroDuration,
};

class MockTxPhy final : public ITxPhy {
 public:
  explicit MockTxPhy(EmissionMode mode = EmissionMode::kValid,
                     SimDuration duration =
                         SimDuration::FromNanoseconds(20)) noexcept
      : mode_(mode), duration_(duration) {}

  [[nodiscard]] auto Encode(const DigitalPacket& packet,
                            const TxEncodeRequest& request) const
      -> Result<TxEmission> override {
    ++encode_count_;

    auto transmission_id = request.transmission_id;
    auto packet_id = packet.packet_id;
    auto sender_id = request.sender_node_id;
    auto started_at = request.started_at;
    auto duration = duration_;
    switch(mode_) {
      case EmissionMode::kValid:
        break;
      case EmissionMode::kWrongTransmission:
        transmission_id = TransmissionId{request.transmission_id.value() + 1};
        break;
      case EmissionMode::kWrongPacket:
        packet_id = PacketId{packet.packet_id.value() + 1};
        break;
      case EmissionMode::kWrongSender:
        sender_id = NodeId{request.sender_node_id.value() + 1};
        break;
      case EmissionMode::kWrongStart:
        started_at = SimTime::FromNanoseconds(
            request.started_at.nanoseconds() - 1);
        break;
      case EmissionMode::kZeroDuration:
        duration = SimDuration::Zero();
        break;
    }

    return TxEmission::Create(transmission_id,
                              packet_id,
                              sender_id,
                              started_at,
                              duration,
                              25'000.0,
                              4'000.0,
                              180.0);
  }

  [[nodiscard]] auto encode_count() const noexcept -> std::size_t {
    return encode_count_;
  }

 private:
  EmissionMode mode_;
  SimDuration duration_;
  mutable std::size_t encode_count_{0};
};

class MockChannelFieldProvider final : public IChannelFieldProvider {
 public:
  [[nodiscard]] auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldResponse> override {
    queries_.push_back(query);
    if(fail_on_call_ && queries_.size() == *fail_on_call_) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable,
                "test fixture channel provider failure"});
    }

    const auto receiver = mismatch_identity_ ? NodeId{999}
                                             : query.receiver_node_id();
    return ChannelFieldResponse::Create(
        query.transmission_id(),
        receiver,
        70.0,
        SimDuration::FromNanoseconds(5),
        {});
  }

  auto SetFailOnCall(std::size_t call) noexcept -> void {
    fail_on_call_ = call;
  }

  auto SetMismatchIdentity(bool mismatch) noexcept -> void {
    mismatch_identity_ = mismatch;
  }

  [[nodiscard]] auto queries() const noexcept
      -> const std::vector<ChannelQuery>& {
    return queries_;
  }

 private:
  std::optional<std::size_t> fail_on_call_;
  bool mismatch_identity_{false};
  mutable std::vector<ChannelQuery> queries_;
};

constexpr auto MakeNode(std::uint64_t id,
                        bool can_transmit,
                        bool can_receive,
                        double x_meters,
                        double x_velocity) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{
          can_transmit, can_receive, DuplexMode::kHalfDuplex},
      MotionState{Position3d{x_meters, 0.0, -10.0},
                  Velocity3d{x_velocity, 0.0, 0.0}}};
}

constexpr auto Seconds(std::int64_t value) -> SimTime {
  return SimTime::FromNanoseconds(value * 1'000'000'000);
}

auto MakeWorkingState() -> Result<CycleWorkingState> {
  auto snapshot = WorldSnapshot::Create(
      SnapshotVersion{4},
      SimTime::Zero(),
      std::vector<NodeCommittedState>{
          MakeNode(6, false, true, 60.0, 7.0),
          MakeNode(5, true, false, 50.0, 6.0),
          MakeNode(4, true, true, 40.0, 5.0),
          MakeNode(3, true, true, 30.0, 4.0),
          MakeNode(2, true, true, 20.0, 3.0),
          MakeNode(1, true, true, 10.0, 2.0),
          MakeNode(0, true, true, 0.0, 1.0)});
  if(!snapshot) {
    return std::unexpected(snapshot.error());
  }
  return CycleWorkingState::Create(
      *snapshot, PlanningCycleId{8}, SimTime::Zero());
}

auto MakeRequest(NodeId sender,
                 std::vector<NodeId> receivers,
                 SimTime started_at = Seconds(10),
                 SimTime eligible_at = Seconds(5),
                 NodeId packet_source = NodeId{0})
    -> TransmissionExecutionRequest {
  return TransmissionExecutionRequest{
      TxOpportunity{sender, eligible_at},
      DigitalPacket{PacketId{70},
                    packet_source,
                    UnicastDestination{NodeId{9}},
                    std::vector<std::byte>{std::byte{0x5A}}},
      UnicastTransmissionTarget{NodeId{3}},
      started_at,
      std::move(receivers)};
}

auto HasError(const Result<TransmissionSession>& result,
              ErrorCode code) -> bool {
  return !result && result.error().code == code;
}

auto TestAllocator() -> bool {
  CommunicationIdAllocator allocator{TransmissionId{41}};
  const auto first = allocator.NextTransmissionId();
  const auto second = allocator.NextTransmissionId();
  if(!first || !second || *first != TransmissionId{41} ||
     *second != TransmissionId{42}) {
    return false;
  }

  constexpr auto kMaximum =
      std::numeric_limits<TransmissionId::value_type>::max();
  CommunicationIdAllocator boundary{TransmissionId{kMaximum}};
  const auto maximum = boundary.NextTransmissionId();
  const auto overflow = boundary.NextTransmissionId();
  return maximum && *maximum == TransmissionId{kMaximum} && !overflow &&
         overflow.error().code == ErrorCode::kOverflow;
}

auto TestOneCandidateAndEmptyFanOut() -> bool {
  auto working = MakeWorkingState();
  if(!working) {
    return false;
  }

  CommunicationIdAllocator allocator{TransmissionId{100}};
  MockTxPhy tx_phy;
  MockChannelFieldProvider channel;
  const TransmissionExecutor executor{allocator, tx_phy, channel};

  auto one = executor.ExecuteTransmission(
      *working, MakeRequest(NodeId{2}, {NodeId{1}}));
  if(!one || tx_phy.encode_count() != 1 || channel.queries().size() != 1 ||
     one->received_signals().size() != 1 ||
     one->transmission().transmission_id != TransmissionId{100} ||
     one->emission().transmission_id() != TransmissionId{100} ||
     one->packet().packet_id != PacketId{70} ||
     one->transmission().ended_at !=
         SimTime::FromNanoseconds(10'000'000'020)) {
    return false;
  }

  MockTxPhy empty_tx_phy;
  MockChannelFieldProvider empty_channel;
  const TransmissionExecutor empty_executor{
      allocator, empty_tx_phy, empty_channel};
  auto empty = empty_executor.ExecuteTransmission(
      *working, MakeRequest(NodeId{2}, {}));
  return empty && empty_tx_phy.encode_count() == 1 &&
         empty_channel.queries().empty() &&
         empty->received_signals().empty() &&
         empty->transmission().transmission_id == TransmissionId{101};
}

auto TestBroadcastCardinalityOrderingAndRelay() -> bool {
  auto working = MakeWorkingState();
  if(!working) {
    return false;
  }

  CommunicationIdAllocator allocator{TransmissionId{200}};
  MockTxPhy tx_phy;
  MockChannelFieldProvider channel;
  const TransmissionExecutor executor{allocator, tx_phy, channel};

  auto request = MakeRequest(NodeId{2},
                             {NodeId{4}, NodeId{1}, NodeId{3}},
                             Seconds(10),
                             Seconds(5),
                             NodeId{0});
  request.target = UnicastTransmissionTarget{NodeId{3}};
  auto session = executor.ExecuteTransmission(*working, std::move(request));
  if(!session || tx_phy.encode_count() != 1 ||
     channel.queries().size() != 3 ||
     session->received_signals().size() != 3 ||
     session->packet().source_node_id != NodeId{0} ||
     session->transmission().sender_node_id != NodeId{2} ||
     !std::holds_alternative<UnicastTransmissionTarget>(
         session->transmission().target) ||
     std::get<UnicastTransmissionTarget>(session->transmission().target)
             .node_id != NodeId{3}) {
    return false;
  }

  constexpr NodeId expected_receivers[]{NodeId{1}, NodeId{3}, NodeId{4}};
  constexpr double expected_rx_x[]{30.0, 70.0, 90.0};
  for(std::size_t index = 0; index < 3; ++index) {
    const auto& query = channel.queries()[index];
    const auto& signal = session->received_signals()[index];
    if(query.receiver_node_id() != expected_receivers[index] ||
       query.transmission_id() != TransmissionId{200} ||
       query.sender_node_id() != NodeId{2} ||
       query.emitted_at() != Seconds(10) ||
       query.tx_position().x_meters != 50.0 ||
       query.rx_position().x_meters != expected_rx_x[index] ||
       signal.transmission_id() != TransmissionId{200} ||
       signal.receiver_node_id() != expected_receivers[index]) {
      return false;
    }
  }

  return session->transmission().transmission_id == TransmissionId{200} &&
         session->emission().transmission_id() == TransmissionId{200};
}

auto TestBroadcastTargetStillCreatesOneSession() -> bool {
  auto working = MakeWorkingState();
  if(!working) {
    return false;
  }
  CommunicationIdAllocator allocator{TransmissionId{210}};
  MockTxPhy tx_phy;
  MockChannelFieldProvider channel;
  const TransmissionExecutor executor{allocator, tx_phy, channel};
  auto request = MakeRequest(NodeId{2}, {NodeId{1}, NodeId{3}, NodeId{4}});
  request.selected_packet.destination = BroadcastDestination{};
  request.target = BroadcastTransmissionTarget{};
  auto session = executor.ExecuteTransmission(*working, std::move(request));
  return session && tx_phy.encode_count() == 1 &&
         channel.queries().size() == 3 &&
         session->received_signals().size() == 3 &&
         std::holds_alternative<BroadcastTransmissionTarget>(
             session->transmission().target);
}

auto TestCandidateValidation() -> bool {
  const auto rejects_before_encode = [](std::vector<NodeId> candidates,
                                        NodeId sender,
                                        ErrorCode expected) {
    auto working = MakeWorkingState();
    if(!working) {
      return false;
    }
    CommunicationIdAllocator allocator{TransmissionId{300}};
    MockTxPhy tx_phy;
    MockChannelFieldProvider channel;
    const TransmissionExecutor executor{allocator, tx_phy, channel};
    const auto result = executor.ExecuteTransmission(
        *working, MakeRequest(sender, std::move(candidates)));
    return HasError(result, expected) && tx_phy.encode_count() == 0 &&
           channel.queries().empty();
  };

  return rejects_before_encode(
             {NodeId{1}, NodeId{1}}, NodeId{2}, ErrorCode::kAlreadyExists) &&
         rejects_before_encode(
             {NodeId{2}}, NodeId{2}, ErrorCode::kInvalidArgument) &&
         rejects_before_encode(
             {NodeId{99}}, NodeId{2}, ErrorCode::kNotFound) &&
         rejects_before_encode(
             {NodeId{5}}, NodeId{2}, ErrorCode::kFailedPrecondition) &&
         rejects_before_encode(
             {NodeId{1}}, NodeId{6}, ErrorCode::kFailedPrecondition);
}

auto TestNodeZeroAsSenderAndReceiver() -> bool {
  auto working = MakeWorkingState();
  if(!working) {
    return false;
  }
  CommunicationIdAllocator allocator{TransmissionId{400}};
  MockTxPhy tx_phy;
  MockChannelFieldProvider channel;
  const TransmissionExecutor executor{allocator, tx_phy, channel};

  auto zero_sender = executor.ExecuteTransmission(
      *working, MakeRequest(NodeId{0}, {NodeId{1}},
                            Seconds(10), Seconds(5), NodeId{0}));
  auto zero_receiver = executor.ExecuteTransmission(
      *working, MakeRequest(NodeId{2}, {NodeId{0}}));
  return zero_sender && zero_receiver && tx_phy.encode_count() == 2 &&
         channel.queries().size() == 2 &&
         channel.queries()[0].sender_node_id() == NodeId{0} &&
         channel.queries()[1].receiver_node_id() == NodeId{0};
}

auto TestEligibilityAndIdentityFailures() -> bool {
  {
    auto working = MakeWorkingState();
    if(!working) {
      return false;
    }
    CommunicationIdAllocator allocator{TransmissionId{500}};
    MockTxPhy tx_phy;
    MockChannelFieldProvider channel;
    const TransmissionExecutor executor{allocator, tx_phy, channel};
    const auto early = executor.ExecuteTransmission(
        *working,
        MakeRequest(NodeId{2},
                    {NodeId{1}},
                    Seconds(4),
                    Seconds(5)));
    if(!HasError(early, ErrorCode::kFailedPrecondition) ||
       tx_phy.encode_count() != 0 || !channel.queries().empty()) {
      return false;
    }
  }

  constexpr EmissionMode mismatch_modes[]{
      EmissionMode::kWrongTransmission,
      EmissionMode::kWrongPacket,
      EmissionMode::kWrongSender,
      EmissionMode::kWrongStart};
  for(const auto mode : mismatch_modes) {
    auto working = MakeWorkingState();
    if(!working) {
      return false;
    }
    CommunicationIdAllocator allocator{TransmissionId{510}};
    MockTxPhy tx_phy{mode};
    MockChannelFieldProvider channel;
    const TransmissionExecutor executor{allocator, tx_phy, channel};
    const auto result = executor.ExecuteTransmission(
        *working, MakeRequest(NodeId{2}, {NodeId{1}}));
    if(!HasError(result, ErrorCode::kFailedPrecondition) ||
       tx_phy.encode_count() != 1 || !channel.queries().empty()) {
      return false;
    }
  }
  return true;
}

auto TestDurationAndEndOverflow() -> bool {
  {
    auto working = MakeWorkingState();
    if(!working) {
      return false;
    }
    CommunicationIdAllocator allocator{TransmissionId{600}};
    MockTxPhy tx_phy{EmissionMode::kZeroDuration};
    MockChannelFieldProvider channel;
    const TransmissionExecutor executor{allocator, tx_phy, channel};
    const auto result = executor.ExecuteTransmission(
        *working, MakeRequest(NodeId{2}, {NodeId{1}}));
    if(!HasError(result, ErrorCode::kFailedPrecondition) ||
       tx_phy.encode_count() != 1 || !channel.queries().empty()) {
      return false;
    }
  }

  auto working = MakeWorkingState();
  if(!working) {
    return false;
  }
  CommunicationIdAllocator allocator{TransmissionId{601}};
  MockTxPhy tx_phy{EmissionMode::kValid,
                   SimDuration::FromNanoseconds(1)};
  MockChannelFieldProvider channel;
  const TransmissionExecutor executor{allocator, tx_phy, channel};
  constexpr auto kMaximum =
      std::numeric_limits<SimTime::representation_type>::max();
  const auto result = executor.ExecuteTransmission(
      *working,
      MakeRequest(NodeId{2},
                  {},
                  SimTime::FromNanoseconds(kMaximum),
                  SimTime::FromNanoseconds(kMaximum)));
  return HasError(result, ErrorCode::kOverflow) &&
         tx_phy.encode_count() == 1 && channel.queries().empty();
}

auto TestChannelFailuresAreAtomic() -> bool {
  {
    auto working = MakeWorkingState();
    if(!working) {
      return false;
    }
    CommunicationIdAllocator allocator{TransmissionId{700}};
    MockTxPhy tx_phy;
    MockChannelFieldProvider channel;
    channel.SetMismatchIdentity(true);
    const TransmissionExecutor executor{allocator, tx_phy, channel};
    const auto result = executor.ExecuteTransmission(
        *working, MakeRequest(NodeId{2}, {NodeId{1}}));
    if(!HasError(result, ErrorCode::kFailedPrecondition) ||
       channel.queries().size() != 1) {
      return false;
    }
  }

  auto working = MakeWorkingState();
  if(!working) {
    return false;
  }
  CommunicationIdAllocator allocator{TransmissionId{701}};
  MockTxPhy tx_phy;
  MockChannelFieldProvider channel;
  channel.SetFailOnCall(2);
  const TransmissionExecutor executor{allocator, tx_phy, channel};
  const auto result = executor.ExecuteTransmission(
      *working, MakeRequest(NodeId{2}, {NodeId{1}, NodeId{3}, NodeId{4}}));
  return HasError(result, ErrorCode::kUnavailable) &&
         tx_phy.encode_count() == 1 && channel.queries().size() == 2;
}

auto TestWorkingAnchorGeometry() -> bool {
  auto working = MakeWorkingState();
  if(!working ||
     !working->UpdateVelocity(NodeId{3},
                              Velocity3d{10.0, 0.0, 0.0},
                              Seconds(5))) {
    return false;
  }

  CommunicationIdAllocator allocator{TransmissionId{800}};
  MockTxPhy tx_phy;
  MockChannelFieldProvider channel;
  const TransmissionExecutor executor{allocator, tx_phy, channel};
  const auto result = executor.ExecuteTransmission(
      *working,
      MakeRequest(NodeId{2},
                  {NodeId{3}},
                  Seconds(10),
                  Seconds(5)));
  return result && channel.queries().size() == 1 &&
         channel.queries()[0].tx_position().x_meters == 50.0 &&
         channel.queries()[0].rx_position().x_meters == 100.0 &&
         working->base_snapshot().FindNode(NodeId{3})->get()
                 .motion.position.x_meters == 30.0;
}

auto main() -> int {
  if(!TestAllocator() || !TestOneCandidateAndEmptyFanOut() ||
     !TestBroadcastCardinalityOrderingAndRelay() ||
     !TestBroadcastTargetStillCreatesOneSession() ||
     !TestCandidateValidation() || !TestNodeZeroAsSenderAndReceiver() ||
     !TestEligibilityAndIdentityFailures() ||
     !TestDurationAndEndOverflow() || !TestChannelFailuresAreAtomic() ||
     !TestWorkingAnchorGeometry()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
