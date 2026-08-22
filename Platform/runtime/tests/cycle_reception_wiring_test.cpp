#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/application_delivery_store.hpp"
#include "internal/commit_service.hpp"
#include "internal/communication_id_allocator.hpp"
#include "internal/cycle_signal_runtime.hpp"
#include "internal/cycle_working_state.hpp"
#include "internal/in_flight_signal_ledger.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/receiver_processor.hpp"
#include "internal/reception_disposition_applier.hpp"
#include "internal/reception_disposition_service.hpp"
#include "internal/reception_result_accumulator.hpp"
#include "internal/transmission_executor.hpp"
#include "internal/transmission_record_store.hpp"
#include "internal/world_state_store.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::runtime::internal;

namespace {

constexpr auto At(std::int64_t value) -> SimTime {
  return SimTime::FromNanoseconds(value);
}

constexpr auto For(std::int64_t value) -> SimDuration {
  return SimDuration::FromNanoseconds(value);
}

constexpr auto Node(std::uint64_t id) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{true, true, DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, 0.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto Packet(std::uint64_t id, PacketDestination destination)
    -> DigitalPacket {
  return DigitalPacket{PacketId{id},
                       NodeId{0},
                       std::move(destination),
                       {std::byte{0xA5}, std::byte{0x5A}}};
}

class MockTxPhy final : public ITxPhy {
 public:
  auto Encode(const DigitalPacket& packet,
              const TxEncodeRequest& request) const
      -> Result<TxEmission> override {
    return TxEmission::Create(request.transmission_id,
                              packet.packet_id,
                              request.sender_node_id,
                              request.started_at,
                              For(1),
                              25'000.0,
                              4'000.0,
                              180.0);
  }
};

class MockChannel final : public IChannelFieldProvider {
 public:
  auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldResponse> override {
    return ChannelFieldResponse::Create(query.transmission_id(),
                                        query.receiver_node_id(),
                                        70.0,
                                        For(1),
                                        {});
  }
};

class MockNoise final : public INoiseFieldProvider {
 public:
  auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    return NoiseObservation::Create(query.receiver_node_id(),
                                    query.observed_from(),
                                    query.observed_until(),
                                    query.lower_frequency_hz(),
                                    query.upper_frequency_hz(),
                                    40.0);
  }
};

class MockRxPhy final : public IRxPhy {
 public:
  explicit MockRxPhy(NodeId not_decoded = NodeId{99}) noexcept
      : not_decoded_(not_decoded) {}

  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    const auto& signal = request.receiver_window().desired_signal();
    const auto outcome = signal.receiver_node_id() == not_decoded_
                             ? DecodeOutcome::kNotDecoded
                             : DecodeOutcome::kDecoded;
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  outcome);
  }

 private:
  NodeId not_decoded_;
};

struct Fixture final {
  WorldStateStore world;
  CycleWorkingState working;
  PacketQueueStore queues;
  ApplicationDeliveryStore deliveries;
  CommunicationIdAllocator ids{TransmissionId{10}, ReceptionId{20}};
  MockTxPhy tx_phy;
  MockChannel channel;
  MockNoise noise;
  MockRxPhy rx_phy;
  TransmissionExecutor executor;
  ReceiverProcessor receiver;
  CommitService commit;
  InFlightSignalLedger ledger;
  ReceptionResultAccumulator results;
  TransmissionRecordStore records;
  ReceptionDispositionService disposition_service;
  ReceptionDispositionApplier disposition_applier;
  CycleSignalRuntime runtime;

  Fixture(WorldSnapshot snapshot,
          CycleWorkingState cycle,
          PacketQueueStore packet_queues,
          ApplicationDeliveryStore application_deliveries,
          NodeId not_decoded,
          SnapshotVersion expected_version)
      : world(snapshot),
        working(std::move(cycle)),
        queues(std::move(packet_queues)),
        deliveries(std::move(application_deliveries)),
        rx_phy(not_decoded),
        executor(ids, tx_phy, channel),
        receiver(ids, noise, rx_phy),
        commit(world),
        disposition_applier(queues, deliveries),
        runtime(executor,
                receiver,
                working,
                commit,
                ledger,
                results,
                records,
                disposition_service,
                disposition_applier,
                expected_version,
                At(10)) {}
};

auto MakeFixture(NodeId not_decoded = NodeId{99},
                 SnapshotVersion expected_version = SnapshotVersion{0})
    -> std::unique_ptr<Fixture> {
  auto snapshot = WorldSnapshot::Create(
      SnapshotVersion{0},
      SimTime::Zero(),
      std::vector<NodeCommittedState>{Node(3), Node(0), Node(2), Node(1)});
  if(!snapshot) return nullptr;
  auto working = CycleWorkingState::Create(
      *snapshot, PlanningCycleId{0}, SimTime::Zero());
  auto queues = PacketQueueStore::Create(
      {NodeId{3}, NodeId{0}, NodeId{2}, NodeId{1}});
  auto deliveries = ApplicationDeliveryStore::Create(
      {NodeId{3}, NodeId{0}, NodeId{2}, NodeId{1}});
  if(!working || !queues || !deliveries) return nullptr;
  return std::make_unique<Fixture>(*snapshot,
                                   std::move(*working),
                                   std::move(*queues),
                                   std::move(*deliveries),
                                   not_decoded,
                                   expected_version);
}

auto Request(std::uint64_t packet_id,
             NodeId sender,
             PacketDestination destination,
             TransmissionTarget target,
             std::vector<NodeId> receivers)
    -> TransmissionExecutionRequest {
  return TransmissionExecutionRequest{TxOpportunity{sender, At(1)},
                                      Packet(packet_id,
                                             std::move(destination)),
                                      std::move(target),
                                      At(1),
                                      std::move(receivers)};
}

auto FinalizeAll(Fixture& fixture,
                 const TransmissionSession& session) -> Status {
  for(const auto& signal : session.received_signals()) {
    auto status = fixture.runtime.HandleSignalArrival(
        signal.first_arrival_at(), signal);
    if(!status) return status;
  }
  for(const auto& signal : session.received_signals()) {
    auto status = fixture.runtime.HandleSessionFinalize(
        signal.last_effect_end_at(), signal);
    if(!status) return status;
  }
  return {};
}

auto TestRelayRecordLifetimeAndCleanup() -> bool {
  auto fixture = MakeFixture();
  if(!fixture) return false;
  auto session = fixture->runtime.HandleTxStart(
      At(1),
      Request(7,
              NodeId{0},
              UnicastDestination{NodeId{3}},
              UnicastTransmissionTarget{NodeId{1}},
              {NodeId{3}, NodeId{1}, NodeId{2}}));
  if(!session || fixture->records.size() != 1 ||
     !fixture->records.Find(session->transmission().transmission_id) ||
     !FinalizeAll(*fixture, *session)) {
    return false;
  }

  const auto relayed = fixture->queues.PeekFront(NodeId{1});
  if(fixture->records.size() != 1 || fixture->results.sessions().size() != 3 ||
     fixture->deliveries.size() != 0 || !relayed || !*relayed ||
     **relayed != session->packet()) {
    return false;
  }

  const auto closed = fixture->runtime.HandleCycleClose(At(10));
  return closed && fixture->records.size() == 0 && fixture->ledger.empty() &&
         fixture->results.sessions().size() == 3 &&
         *fixture->queues.size(NodeId{1}) == 1 &&
         fixture->world.current_snapshot().version() == SnapshotVersion{1};
}

auto TestFinalDeliveryAndBroadcastPersistence() -> bool {
  auto final_fixture = MakeFixture();
  if(!final_fixture) return false;
  auto final_session = final_fixture->runtime.HandleTxStart(
      At(1),
      Request(8,
              NodeId{1},
              UnicastDestination{NodeId{3}},
              UnicastTransmissionTarget{NodeId{3}},
              {NodeId{0}, NodeId{2}, NodeId{3}}));
  if(!final_session || !FinalizeAll(*final_fixture, *final_session) ||
     final_fixture->deliveries.size() != 1 ||
     *final_fixture->queues.size(NodeId{3}) != 0 ||
     !final_fixture->runtime.HandleCycleClose(At(10))) {
    return false;
  }
  if(final_fixture->deliveries.size() != 1 ||
     final_fixture->records.size() != 0) {
    return false;
  }

  auto broadcast_fixture = MakeFixture();
  if(!broadcast_fixture) return false;
  auto broadcast_session = broadcast_fixture->runtime.HandleTxStart(
      At(1),
      Request(9,
              NodeId{0},
              BroadcastDestination{},
              BroadcastTransmissionTarget{},
              {NodeId{1}, NodeId{2}, NodeId{3}}));
  if(!broadcast_session || broadcast_fixture->records.size() != 1 ||
     !FinalizeAll(*broadcast_fixture, *broadcast_session)) {
    return false;
  }
  return broadcast_session->received_signals().size() == 3 &&
         broadcast_fixture->results.sessions().size() == 3 &&
         broadcast_fixture->deliveries.size() == 3 &&
         *broadcast_fixture->queues.size(NodeId{1}) == 0 &&
         *broadcast_fixture->queues.size(NodeId{2}) == 0 &&
         *broadcast_fixture->queues.size(NodeId{3}) == 0;
}

auto TestTargetNotDecodedHasNoNetworkEffect() -> bool {
  auto fixture = MakeFixture(NodeId{1});
  if(!fixture) return false;
  auto session = fixture->runtime.HandleTxStart(
      At(1),
      Request(10,
              NodeId{0},
              UnicastDestination{NodeId{3}},
              UnicastTransmissionTarget{NodeId{1}},
              {NodeId{1}, NodeId{2}, NodeId{3}}));
  if(!session || !FinalizeAll(*fixture, *session)) return false;
  return fixture->results.sessions().size() == 3 &&
         fixture->deliveries.size() == 0 &&
         *fixture->queues.size(NodeId{1}) == 0 &&
         *fixture->queues.size(NodeId{2}) == 0 &&
         *fixture->queues.size(NodeId{3}) == 0;
}

auto TestMissingRecordAndApplyFailureOrdering() -> bool {
  auto missing = MakeFixture();
  if(!missing) return false;
  auto unregistered = missing->executor.ExecuteTransmission(
      missing->working,
      Request(11,
              NodeId{0},
              UnicastDestination{NodeId{3}},
              UnicastTransmissionTarget{NodeId{1}},
              {NodeId{1}}));
  if(!unregistered || unregistered->received_signals().empty()) return false;
  const auto signal = unregistered->received_signals().front();
  if(!missing->runtime.HandleSignalArrival(signal.first_arrival_at(),
                                           signal)) {
    return false;
  }
  const auto missing_result = missing->runtime.HandleSessionFinalize(
      signal.last_effect_end_at(), signal);
  if(missing_result || missing_result.error().code != ErrorCode::kNotFound ||
     !missing->results.sessions().empty() || missing->deliveries.size() != 0 ||
     *missing->queues.size(NodeId{1}) != 0) {
    return false;
  }

  auto duplicate = MakeFixture();
  if(!duplicate || !duplicate->queues.Enqueue(
                       NodeId{1},
                       Packet(12, UnicastDestination{NodeId{3}}))) {
    return false;
  }
  auto session = duplicate->runtime.HandleTxStart(
      At(1),
      Request(12,
              NodeId{0},
              UnicastDestination{NodeId{3}},
              UnicastTransmissionTarget{NodeId{1}},
              {NodeId{1}}));
  if(!session) return false;
  const auto duplicate_signal = session->received_signals().front();
  if(!duplicate->runtime.HandleSignalArrival(
         duplicate_signal.first_arrival_at(), duplicate_signal)) {
    return false;
  }
  const auto apply_result = duplicate->runtime.HandleSessionFinalize(
      duplicate_signal.last_effect_end_at(), duplicate_signal);
  return !apply_result &&
         apply_result.error().code == ErrorCode::kAlreadyExists &&
         duplicate->results.sessions().size() == 1 &&
         duplicate->records.size() == 1 &&
         *duplicate->queues.size(NodeId{1}) == 1;
}

auto TestCommitFailureRetainsRecords() -> bool {
  auto fixture = MakeFixture(NodeId{99}, SnapshotVersion{1});
  if(!fixture) return false;
  const auto session = fixture->runtime.HandleTxStart(
      At(1),
      Request(13,
              NodeId{0},
              BroadcastDestination{},
              BroadcastTransmissionTarget{},
              {}));
  if(!session || fixture->records.size() != 1) return false;
  const auto close = fixture->runtime.HandleCycleClose(At(10));
  return !close && close.error().code == ErrorCode::kFailedPrecondition &&
         fixture->records.size() == 1;
}

auto TestMultipleZeroReceiverRecordsCleanup() -> bool {
  auto fixture = MakeFixture();
  if(!fixture) return false;
  for(std::uint64_t packet_id = 20; packet_id < 23; ++packet_id) {
    const auto session = fixture->runtime.HandleTxStart(
        At(1),
        Request(packet_id,
                NodeId{0},
                BroadcastDestination{},
                BroadcastTransmissionTarget{},
                {}));
    if(!session || !session->received_signals().empty()) return false;
  }
  if(fixture->records.size() != 3 ||
     !fixture->runtime.HandleCycleClose(At(10))) {
    return false;
  }
  return fixture->records.size() == 0 && fixture->ledger.empty() &&
         fixture->results.sessions().empty();
}

}  // namespace

auto main() -> int {
  return TestRelayRecordLifetimeAndCleanup() &&
                 TestFinalDeliveryAndBroadcastPersistence() &&
                 TestTargetNotDecodedHasNoNetworkEffect() &&
                 TestMissingRecordAndApplyFailureOrdering() &&
                 TestCommitFailureRetainsRecords() &&
                 TestMultipleZeroReceiverRecordsCleanup()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
