#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>

#include "internal/acceptance_run_executor.hpp"
#include "internal/reference_environment_asset.hpp"

namespace {
using namespace ns3_factory;
using namespace contracts;
using namespace assembly::internal;
using namespace environment::internal;
using namespace phy::internal;
using namespace runtime::internal;
using namespace kernel::internal;
using namespace structure::internal;
namespace app = application::internal::acceptance_run_executor_detail;

template<class T> auto Require(Result<T> result) -> T {
  if(!result) throw std::runtime_error(result.error().message);
  return std::move(*result);
}
void Require(Status status) {
  if(!status) throw std::runtime_error(status.error().message);
}
void Check(bool value, const char* message) {
  if(!value) throw std::runtime_error(message);
}

auto Config(bool strong_noise) -> WaveformPhyConfig {
  return {{ModulationScheme::kBpsk, 6000, 60, std::pow(10.0, 110.0 / 20),
           25000, 30},
          4000, AwgnNoiseConfig{strong_noise ? 120.0 : 0.0, 42}, true,
          WaveformFrameFormat::kPayloadWithCrc32, 1'000'000};
}
auto Channel() -> AcousticFieldChannelProvider {
  auto asset = Require(BuildReferenceShallowWaterV1(
      PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT));
  return Require(AcousticFieldChannelProvider::Create(
      std::make_shared<const AcousticFieldAsset>(std::move(asset)),
      Require(DiscreteFrequencySelectionPolicy::Create(0))));
}
void Hex(std::span<const std::byte> bytes) {
  for(const auto byte : bytes) {
    std::cout << std::hex << std::setfill('0') << std::setw(2)
              << std::to_integer<unsigned>(byte);
  }
  std::cout << std::dec << std::setfill(' ');
}

struct Observer final : IWaveformReceptionObserver {
  std::vector<WaveformReceptionSummary> rows;
  void Observe(const WaveformReceptionSummary& row) noexcept override {
    rows.push_back(row);
  }
};

// A test-only provider decorator records actual decoder bytes. The injected
// missing-payload fault discards them AFTER a real decode, never fabricates Y.
class RecordingRx final : public IRxPhy {
 public:
  RecordingRx(const IRxPhy& inner, bool missing = false)
      : inner_(inner), missing_(missing) {}
  auto Decode(const RxDecodeRequest& request) const -> Result<RxDecodeResult> override {
    auto decoded = inner_.Decode(request);
    if(!decoded) return decoded;
    if(decoded->recovered_payload()) {
      recovered.emplace(decoded->transmission_id(), *decoded->recovered_payload());
    }
    if(missing_ && decoded->outcome() == DecodeOutcome::kDecoded) {
      return RxDecodeResult::Create(decoded->transmission_id(), decoded->packet_id(),
                                   decoded->receiver_node_id(), DecodeOutcome::kDecoded);
    }
    return decoded;
  }
  mutable std::map<TransmissionId, std::vector<std::byte>> recovered;
 private:
  const IRxPhy& inner_;
  bool missing_;
};

// Differential causality fixture: retain D in the network record but physically
// transmit a valid frame D' with a different bearing target. If fusion falls
// back to record bytes it will estimate the old target and this test fails.
class RecordingTx final : public ITxPhy {
 public:
  RecordingTx(const ITxPhy& inner, bool alternate = false)
      : inner_(inner), alternate_(alternate) {}
  auto Encode(const DigitalPacket& packet, const TxEncodeRequest& request) const
      -> Result<TxEmission> override {
    auto physical = packet;
    if(alternate_) {
      auto report = Require(DecodeDetectionFeatureReportV1(packet.payload));
      report.bearing_centidegrees = Require(GenerateGlobalBearingCentidegrees(
          report.sensor_x_meters_quantized, report.sensor_y_meters_quantized,
          -200, 300));
      const auto bytes = Require(EncodeDetectionFeatureReportV1(report));
      physical.payload.assign(bytes.begin(), bytes.end());
    }
    ++calls;
    original.emplace(request.transmission_id, packet.payload);
    transmitted.emplace(request.transmission_id, physical.payload);
    return inner_.Encode(physical, request);
  }
  mutable std::size_t calls{0};
  mutable std::map<TransmissionId, std::vector<std::byte>> original, transmitted;
 private:
  const ITxPhy& inner_;
  bool alternate_;
};

// One actual ns-3 TxStart -> arrival -> finalize -> delivery -> cycle commit.
void SingleLink(bool strong_noise) {
  auto channel = Channel();
  const auto config = Config(strong_noise);
  TransmissionWaveformStore store;
  auto tx = Require(WaveformTxPhy::Create(config, store));
  Observer observer;
  auto rx = Require(WaveformRxPhy::Create(config, store, &observer));
  RecordingRx recording_rx{rx};
  const DigitalPacket packet{PacketId{42}, NodeId{1}, UnicastDestination{NodeId{2}},
                             {std::byte{0xa5}, std::byte{0x3c}}};
  auto snapshot = Require(WorldSnapshot::Create(SnapshotVersion{0}, SimTime::Zero(),
      {acceptance_config_detail::Node(1, {0,0,-65}, {}, ProtocolRole::kMember).initial_state,
       acceptance_config_detail::Node(2, {1000,0,-8}, {}, ProtocolRole::kSink).initial_state}));
  WorldStateStore world{snapshot};
  auto trajectories = Require(trajectory::BindGenerated(snapshot, SimTime::FromNanoseconds(6'000'000'000)));
  auto working = Require(CycleWorkingState::Create(snapshot, PlanningCycleId{0}, SimTime::Zero(), trajectories));
  auto queues = Require(PacketQueueStore::Create({NodeId{1}, NodeId{2}}));
  auto deliveries = Require(ApplicationDeliveryStore::Create({NodeId{1}, NodeId{2}}));
  CommunicationIdAllocator ids{TransmissionId{1000}, ReceptionId{2000}};
  app::ConstantNoise noise{45}; // Legacy metadata; explicit NoiseProfile controls N.
  TransmissionExecutor transmitter{ids, tx, channel};
  ReceiverProcessor receiver{ids, noise, recording_rx};
  CommitService commit{world};
  InFlightSignalLedger ledger;
  ReceptionResultAccumulator results;
  TransmissionRecordStore records;
  ReceptionDispositionService disposition;
  ReceptionDispositionApplier apply{queues, deliveries};
  const auto close = SimTime::FromNanoseconds(6'000'000'000);
  CycleSignalRuntime runtime{transmitter, receiver, working, commit, ledger, results,
      records, disposition, apply, SnapshotVersion{0}, close};
  Ns3KernelGateway gateway;
  struct Destroy { Ns3KernelGateway& gateway; ~Destroy() { gateway.Destroy(); } } destroy{gateway};
  EventDispatcher dispatcher{gateway};
  Ns3TransmissionSessionEventSink sink{dispatcher, runtime};
  Require(dispatcher.Schedule(TxStartEvent{SimTime::Zero(), [&]() -> Status {
    auto session = runtime.HandleTxStart(SimTime::Zero(),
        {{NodeId{1}, SimTime::Zero()}, packet, UnicastTransmissionTarget{NodeId{2}},
         SimTime::Zero(), {NodeId{2}}});
    if(!session) return std::unexpected(session.error());
    const auto artifact = Require(store.Find(session->transmission().transmission_id));
    const auto& signal = session->received_signals().front();
    std::cout << "N1_to_N2 modulation=BPSK fs=6000 carrier=25000 symbol_rate=60"
              << " AWGN_PSD=" << (strong_noise ? 120 : 0) << " seed=42\nD=";
    Hex(packet.payload);
    std::cout << "\nX_samples=" << artifact->transmitted_waveform().sample_count()
              << " paths=" << signal.channel_response().paths().size()
              << " first_arrival_ns=" << signal.first_arrival_at().nanoseconds()
              << " max_excess_ns=" << signal.latest_excess_delay().nanoseconds() << '\n';
    for(const auto& path : signal.channel_response().paths()) {
      std::cout << "path excess_ns=" << path.excess_delay().nanoseconds()
                << " gain=" << path.pressure_gain_linear()
                << " phase=" << path.phase_radians() << '\n';
    }
    return sink.Publish(*session);
  }}));
  Require(dispatcher.Schedule(CycleCloseEvent{close, [&]() {
    return runtime.HandleCycleClose(close);
  }}));
  Require(dispatcher.Run());
  Check(observer.rows.size() == 1, "single link must decode exactly once");
  const auto& row = observer.rows.front();
  std::cout << "Y_samples=" << row.received_sample_count << " bit_errors=" << row.bit_error_count
            << " compared_bits=" << row.payload_bit_count
            << " BER=" << static_cast<double>(row.bit_error_count) / row.payload_bit_count
            << " CRC=" << row.crc_ok << " deliveries=" << deliveries.size() << "\nD_hat=";
  if(deliveries.size()) Hex(deliveries.deliveries().front().packet.payload);
  else std::cout << "absent";
  std::cout << '\n';
  if(strong_noise) {
    Check(row.bit_error_count > 0 && !row.crc_ok && deliveries.size() == 0,
          "noise must block payload delivery");
  } else {
    Check(row.crc_ok && row.bit_error_count == 0 && deliveries.size() == 1 &&
          deliveries.deliveries().front().packet.payload == packet.payload,
          "normal link must recover the explicit payload");
  }
  store.ReleaseCycleArtifacts();
}

void Fusion(std::string_view mode) {
  const bool missing = mode == "missing";
  const bool alternate = mode == "alternate";
  const bool strong = mode == "degraded";
  const bool abstract = mode == "abstract";
  auto params = Require(Acceptance4NodeParameters());
  using acceptance_config_detail::Node;
  params.nodes = {Node(1, {1000,0,-65}, {}, ProtocolRole::kMember),
                  Node(3, {0,1000,-65}, {}, ProtocolRole::kMember),
                  Node(4, {-1000,0,-65}, {}, ProtocolRole::kMember),
                  Node(2, {0,0,-8}, {}, ProtocolRole::kSink)};
  params.maximum_planned_payload_bytes = 19;
  params.tdma_slot_duration = SimDuration::FromNanoseconds(6'000'000'000);
  params.environment_asset_id = Require(EnvironmentAssetId::Create(kReferenceShallowWaterV1AssetId));
  auto config = Require(AcceptanceScenarioConfig::Create(std::move(params)));
  auto channel = Channel();
  auto snapshot = Require(config.InitialWorldSnapshot());
  WorldStateStore world{snapshot};
  auto queues = Require(PacketQueueStore::Create({NodeId{1},NodeId{2},NodeId{3},NodeId{4}}));
  auto deliveries = Require(ApplicationDeliveryStore::Create({NodeId{1},NodeId{2},NodeId{3},NodeId{4}}));
  auto application = Require(AcceptanceFeatureApplication::Create(config, queues, deliveries, SimTime::Zero()));
  auto policy = Require(ConnectivityDecisionPolicy::Create(std::nullopt, 0.5, 0.5));
  app::FusionCenterEstimator estimator{NodeId{2}};
  ConfiguredRoleAssignmentPolicy roles{config.RoleBindings()};
  SingleSinkStarTopologyPolicy topology;
  StructureBuilder structure{policy, estimator, roles, topology};
  app::AcceptancePlanner planner{config};
  app::ConstantNoise noise{45};
  TransmissionWaveformStore store;
  auto tx = Require(WaveformTxPhy::Create(Config(strong), store));
  auto scalar_tx = Require(RateBasedTxPhy::Create({60, 25000, 4000, 110}));
  RecordingTx recording_tx{abstract ? static_cast<const ITxPhy&>(scalar_tx)
                                   : static_cast<const ITxPhy&>(tx), alternate};
  Observer observer;
  auto rx = Require(WaveformRxPhy::Create(Config(strong), store, &observer));
  RecordingRx recording_rx{rx, missing};
  app::DeterministicRxPhy scalar_rx;
  const auto composition = abstract
      ? PhyExecutionComposition::Abstract(recording_tx, channel, noise, scalar_rx)
      : PhyExecutionComposition::Waveform(recording_tx, channel, noise, recording_rx, store);
  Ns3KernelGateway gateway;
  CommunicationIdAllocator ids{TransmissionId{1000}, ReceptionId{2000}};
  ScenarioRuntime runtime{gateway, world, queues, deliveries, ids, structure, planner,
                          composition, PlanningCycleId{0}, nullptr, 1, &application};
  auto trajectories = Require(trajectory::BindGenerated(world.current_snapshot(),
      SimTime::FromNanoseconds(2 * config.communication_cycle_duration().nanoseconds())));
  Require(runtime.BindTrajectories(trajectories));
  auto executed = runtime.RunCycles(2);
  std::cout << "fusion_mode=" << mode << " run_ok=" << executed.has_value()
            << " tx_calls=" << recording_tx.calls << " rx_calls=" << observer.rows.size()
            << " deliveries=" << deliveries.size()
            << " accepted_observations=" << application.accepted_observation_count()
            << " fusion_results=" << application.fusion_results().size() << '\n';
  std::size_t sink_bits = 0, sink_errors = 0, sink_crc_ok = 0;
  for(const auto& row : observer.rows) {
    if(row.receiver_node_id != NodeId{2}) continue;
    sink_bits += row.payload_bit_count;
    sink_errors += row.bit_error_count;
    sink_crc_ok += row.crc_ok ? 1 : 0;
  }
  if(sink_bits != 0) {
    std::cout << "sink_bits=" << sink_bits << " sink_errors=" << sink_errors
              << " sink_BER=" << static_cast<double>(sink_errors) / sink_bits
              << " sink_crc_ok=" << sink_crc_ok << '\n';
  }
  if(missing) {
    Check(!executed && executed.error().code == ErrorCode::kFailedPrecondition &&
          !recording_rx.recovered.empty() && deliveries.size() == 0 &&
          application.accepted_observation_count() == 0 && application.fusion_results().empty(),
          "waveform success without D_hat must be rejected before delivery/fusion");
    Check(store.size() == 0, "failed run must release artifacts");
    return;
  }
  Require(executed);
  Check(recording_tx.calls == 6, "six physical sends must encode exactly six times");
  if(strong) {
    Check(deliveries.size() == 0 && application.accepted_observation_count() == 0 &&
          application.fusion_results().empty(), "strong N must remove fusion observations");
  } else {
    Check(deliveries.size() == 6 && application.accepted_observation_count() == 6 &&
          application.fusion_results().size() == 1, "normal chain must actually fuse six observations");
    for(const auto& delivery : deliveries.deliveries()) {
      const auto id = delivery.transmission_id;
      if(!abstract) {
        Check(delivery.packet.payload == recording_rx.recovered.at(id) &&
              delivery.packet.payload == recording_tx.transmitted.at(id),
              "fusion delivery must equal decoded Y bytes");
        if(alternate) Check(delivery.packet.payload != recording_tx.original.at(id),
                            "differential physical payload must differ from record D");
      }
    }
    const auto& fused = application.fusion_results().front();
    const auto target_x = alternate ? -200.0 : config.target_x_meters();
    const auto target_y = alternate ? 300.0 : config.target_y_meters();
    std::cout << "fused_target=" << fused.estimated_target_x_meters << ','
              << fused.estimated_target_y_meters << '\n';
    Check(std::hypot(fused.estimated_target_x_meters-target_x,
                     fused.estimated_target_y_meters-target_y) < 1.0,
          "fusion must follow the target encoded in physical Y");
  }
  if(!abstract) Check(store.size() == 0, "cycle close must release waveform store");
}
} // namespace

int main(int argc, char** argv) {
  try {
    std::cout << std::setprecision(12);
    const std::string_view mode = argc == 2 ? argv[1] : "normal";
    if(mode == "single" || mode == "single-degraded") SingleLink(mode == "single-degraded");
    else if(mode == "normal" || mode == "alternate" || mode == "missing" ||
            mode == "degraded" || mode == "abstract") Fusion(mode);
    else throw std::runtime_error("Unknown review mode");
    return 0;
  } catch(const std::exception& error) {
    std::cerr << "Review failed: " << error.what() << '\n';
    return 1;
  }
}
