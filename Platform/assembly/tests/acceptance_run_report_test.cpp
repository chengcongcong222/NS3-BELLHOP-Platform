#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/trace.hpp>

#include "internal/acceptance_run_report.hpp"

namespace {

using namespace ns3_factory;
using namespace assembly::internal;
using namespace contracts;

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

auto MakeFinalSnapshot(const AcceptanceScenarioConfig& config,
                       std::size_t cycles) -> Result<WorldSnapshot> {
  const auto initial = config.InitialWorldSnapshot();
  if(!initial) return std::unexpected(initial.error());
  auto nodes = std::vector<NodeCommittedState>{initial->nodes().begin(),
                                                initial->nodes().end()};
  const auto duration = SimDuration::FromNanoseconds(
      static_cast<std::int64_t>(cycles) *
      config.communication_cycle_duration().nanoseconds());
  const auto completed_at = CheckedAdd(initial->committed_at(), duration);
  if(!completed_at) {
    return std::unexpected(
        Error{ErrorCode::kOverflow, "Final snapshot fixture time overflowed"});
  }
  return WorldSnapshot::Create(SnapshotVersion{cycles},
                               *completed_at,
                               std::move(nodes));
}

auto AppendTrace(std::vector<TraceEvent>& events,
                 SimTime occurred_at,
                 TracePayload payload) -> bool {
  auto event = TraceEvent::Create(occurred_at, std::move(payload));
  if(!event) return false;
  events.push_back(std::move(*event));
  return true;
}

auto MakeTrace(std::size_t cycles,
               SimDuration cycle_duration,
               bool include_loss_statistics)
    -> std::optional<std::vector<TraceEvent>> {
  std::vector<TraceEvent> events;
  if(include_loss_statistics) {
    if(!AppendTrace(
           events,
           At(0),
           TransmissionTrace{TransmissionId{100},
                             PacketId{200},
                             NodeId{10},
                             TraceUnicastTransmissionTarget{NodeId{99}},
                             At(0),
                             At(2'000'000'000)}) ||
       !AppendTrace(events,
                    At(2'000'000'000),
                    ChannelOutcomeTrace{
                        TransmissionId{100},
                        NodeId{99},
                        TraceSignalChannelOutcome{
                            SimDuration::FromNanoseconds(500'000'000),
                            70.0,
                            1}}) ||
       !AppendTrace(events,
                    At(2'000'000'000),
                    ChannelOutcomeTrace{TransmissionId{100},
                                        NodeId{20},
                                        TraceNoArrivalChannelOutcome{}})) {
      return std::nullopt;
    }
    const std::array dispositions{
        TraceReceptionDisposition::kNotDecoded,
        TraceReceptionDisposition::kOverheard,
        TraceReceptionDisposition::kLocalDelivery,
        TraceReceptionDisposition::kRelayEnqueue};
    for(std::size_t index = 0; index < dispositions.size(); ++index) {
      if(!AppendTrace(events,
                      At(2'000'000'000),
                      ReceptionTrace{ReceptionId{300 + index},
                                     TransmissionId{100},
                                     PacketId{200},
                                     NodeId{40 + index},
                                     dispositions[index]})) {
        return std::nullopt;
      }
    }
  }
  for(std::size_t cycle = 0; cycle < cycles; ++cycle) {
    const auto committed_at = At(
        static_cast<std::int64_t>(cycle + 1) *
        cycle_duration.nanoseconds());
    if(!AppendTrace(events,
                    committed_at,
                    CycleCommitTrace{PlanningCycleId{cycle},
                                     SnapshotVersion{cycle},
                                     SnapshotVersion{cycle + 1},
                                     committed_at})) {
      return std::nullopt;
    }
  }
  return events;
}

auto MakeFusionResult(std::uint64_t sequence,
                      std::int64_t started_ns,
                      std::int64_t completed_ns,
                      std::size_t observation_count,
                      std::uint16_t observation_sequence)
    -> FusionResult {
  std::vector<ObservationIdentity> identities;
  identities.reserve(observation_count);
  for(std::size_t index = 0; index < observation_count; ++index) {
    identities.push_back(ObservationIdentity{
        NodeId{index + 1}, observation_sequence});
  }
  return FusionResult{
      sequence,
      At(started_ns),
      At(completed_ns),
      SimDuration::FromNanoseconds(completed_ns - started_ns),
      observation_count,
      std::move(identities),
      200.0,
      150.0,
      3'687,
      0.25,
      completed_ns - started_ns <= 180'000'000'000};
}

struct Fixture final {
  AcceptanceScenarioConfig config;
  phy::internal::RateBasedTxPhy tx_phy;
  WorldSnapshot final_snapshot;
  std::vector<TraceEvent> trace;
  FusionResultStore fusion_results;
};

auto MakeFixture(std::size_t cycles,
                 bool loss_statistics,
                 std::vector<FusionResult> results,
                 AcceptanceScenarioProfile profile =
                     AcceptanceScenarioProfile::kAcceptance4Node,
                 std::optional<std::uint64_t> effective_rate = std::nullopt)
    -> Result<Fixture> {
  auto config = profile == AcceptanceScenarioProfile::kAcceptance4Node
                    ? MakeAcceptance4NodeConfig()
                    : MakeExtended6NodeConfig();
  if(!config) return std::unexpected(config.error());
  auto tx_config = config->TxPhyConfig();
  if(effective_rate) tx_config.bits_per_second = *effective_rate;
  auto tx_phy = phy::internal::RateBasedTxPhy::Create(tx_config);
  auto final_snapshot = MakeFinalSnapshot(*config, cycles);
  auto trace = MakeTrace(
      cycles, config->communication_cycle_duration(), loss_statistics);
  if(!tx_phy) return std::unexpected(tx_phy.error());
  if(!final_snapshot) return std::unexpected(final_snapshot.error());
  if(!trace) {
    return std::unexpected(
        Error{ErrorCode::kInternal, "Could not construct trace fixture"});
  }
  FusionResultStore store;
  for(auto& result : results) {
    const auto appended = store.Append(std::move(result));
    if(!appended) return std::unexpected(appended.error());
  }
  return Fixture{std::move(*config),
                 std::move(*tx_phy),
                 std::move(*final_snapshot),
                 std::move(*trace),
                 std::move(store)};
}

auto BuildProjection(const Fixture& fixture)
    -> Result<AcceptanceRunProjection> {
  return AcceptanceRunProjection::Build(fixture.config,
                                        fixture.tx_phy,
                                        fixture.trace,
                                        fixture.fusion_results,
                                        fixture.final_snapshot);
}

auto TestDeterministicGoldenProjectionAndReport() -> bool {
  const auto fixture = MakeFixture(
      2,
      true,
      {MakeFusionResult(1, 0, 24'000'000'000, 6, 1)});
  if(!fixture) return false;
  const auto first = BuildProjection(*fixture);
  const auto second = BuildProjection(*fixture);
  if(!first || !second || *first != *second) return false;
  const auto report = BuildAcceptanceRunReport(*first);
  return report && first->run_started_at() == SimTime::Zero() &&
         first->run_ended_at() == At(24'000'000'000) &&
         first->simulation_duration().nanoseconds() == 24'000'000'000 &&
         first->cycle_count() == 2 &&
         first->final_snapshot_version() == SnapshotVersion{2} &&
         first->node_count() == 4 && first->mobile_node_count() == 3 &&
         first->fusion_center_count() == 1 &&
         first->configured_rate_bits_per_second() == 60 &&
         first->effective_rate_bits_per_second() == 60 &&
         first->transmission_count() == 1 &&
         first->channel_signal_count() == 1 &&
         first->channel_no_arrival_count() == 1 &&
         first->reception_count() == 4 && first->not_decoded_count() == 1 &&
         first->overheard_count() == 1 &&
         first->local_delivery_count() == 1 &&
         first->relay_enqueue_count() == 1 &&
         first->fusion_result_count() == 1 &&
         first->first_fusion_result() && first->latest_fusion_result() &&
         report->network_node_count.status == AcceptanceMetricStatus::kPass &&
         report->communication_rate.status == AcceptanceMetricStatus::kPass &&
         report->feature_level_fusion.status == AcceptanceMetricStatus::kPass &&
         report->bearing_point_count.status == AcceptanceMetricStatus::kPass &&
         report->fusion_period.status == AcceptanceMetricStatus::kPass &&
         report->bit_error_rate.status ==
             AcceptanceMetricStatus::kNotEvaluated &&
         !report->bit_error_rate.measured_ber &&
         report->overall_status ==
             AcceptanceOverallStatus::kNotFullyEvaluated;
}

auto TestMultiFusionAndLossProjection() -> bool {
  const auto fixture = MakeFixture(
      4,
      true,
      {MakeFusionResult(1, 0, 24'000'000'000, 6, 1),
       MakeFusionResult(2, 24'000'000'000, 48'000'000'000, 6, 2)});
  if(!fixture) return false;
  const auto projection = BuildProjection(*fixture);
  if(!projection) return false;
  const auto report = BuildAcceptanceRunReport(*projection);
  return report && projection->fusion_result_count() == 2 &&
         projection->minimum_observation_count() == 6 &&
         projection->maximum_completed_fusion_period() ==
             SimDuration::FromNanoseconds(24'000'000'000) &&
         projection->channel_no_arrival_count() == 1 &&
         projection->not_decoded_count() == 1 &&
         report->bearing_point_count.status == AcceptanceMetricStatus::kPass &&
         report->fusion_period.status == AcceptanceMetricStatus::kPass;
}

auto TestRateEvidenceComesFromAppliedPhy() -> bool {
  const auto fixture = MakeFixture(
      2,
      false,
      {MakeFusionResult(1, 0, 24'000'000'000, 6, 1)},
      AcceptanceScenarioProfile::kAcceptance4Node,
      61);
  if(!fixture || fixture->config.communication_rate_bits_per_second() != 60) {
    return false;
  }
  const auto projection = BuildProjection(*fixture);
  if(!projection) return false;
  const auto report = BuildAcceptanceRunReport(*projection);
  return report && projection->effective_rate_bits_per_second() == 61 &&
         report->communication_rate.status == AcceptanceMetricStatus::kFail &&
         report->overall_status == AcceptanceOverallStatus::kFail;
}

auto TestExtendedProfileHasProjectionButNoAcceptanceVerdict() -> bool {
  const auto fixture = MakeFixture(
      1,
      false,
      {MakeFusionResult(1, 0, 20'000'000'000, 5, 1)},
      AcceptanceScenarioProfile::kExtended6Node);
  if(!fixture) return false;
  const auto projection = BuildProjection(*fixture);
  return projection && projection->node_count() == 6 &&
         projection->fusion_result_count() == 1 &&
         !BuildAcceptanceRunReport(*projection);
}

auto TestFormatterSnapshot() -> bool {
  const auto fixture = MakeFixture(
      2,
      true,
      {MakeFusionResult(1, 0, 24'000'000'000, 6, 1)});
  if(!fixture) return false;
  const auto projection = BuildProjection(*fixture);
  if(!projection) return false;
  const auto report = BuildAcceptanceRunReport(*projection);
  if(!report) return false;
  const std::string expected =
      "P0 Acceptance Run Report\n"
      "Run: 0.000000000 s -> 24.000000000 s (24.000000000 s), cycles=2, snapshot=2\n"
      "Runtime: transmissions=1, channel_signal=1, channel_no_arrival=1, receptions=4\n"
      "Delivery: not_decoded=1, overheard=1, local_delivery=1, relay_enqueue=1\n"
      "Fusion: results=1, first_period=24.000000000 s, max_period=24.000000000 s\n"
      "NetworkNodeCount: measured=4, requirement=3..4, status=PASS, evidence=AcceptanceScenarioConfig\n"
      "CommunicationRate: configured=60 bit/s, effective=60 bit/s, requirement=60 bit/s, status=PASS, evidence=applied RateBasedTxPhy config\n"
      "BitErrorRate: measured=unavailable, requirement<=0.0001, status=NOT_EVALUATED, evidence=Physical Rx provider does not expose auditable BER yet.\n"
      "FeatureLevelFusion: measured=feature-level, requirement=feature-level, status=PASS, evidence=FusionResultStore workload\n"
      "BearingPointCount: measured_min=6, requirement>=5 per result, status=PASS, evidence=FusionResult.observation_count\n"
      "FusionPeriod: measured_first=24.000000000 s, measured_max=24.000000000 s, requirement<=180.000000000 s, status=PASS, evidence=FusionResult timestamps\n"
      "Overall: NOT_FULLY_EVALUATED\n";
  return FormatAcceptanceRunReport(*report) == expected;
}

}  // namespace

auto main() -> int {
  if(!TestDeterministicGoldenProjectionAndReport()) return 1;
  if(!TestMultiFusionAndLossProjection()) return 2;
  if(!TestRateEvidenceComesFromAppliedPhy()) return 3;
  if(!TestExtendedProfileHasProjectionButNoAcceptanceVerdict()) return 4;
  if(!TestFormatterSnapshot()) return 5;
  return EXIT_SUCCESS;
}
