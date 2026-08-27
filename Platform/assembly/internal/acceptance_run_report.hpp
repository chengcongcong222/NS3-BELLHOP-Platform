#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/trace.hpp>

#include "internal/acceptance_feature.hpp"
#include "internal/acceptance_scenario_config.hpp"
#include "internal/rate_based_tx_phy.hpp"

namespace ns3_factory::assembly::internal {

enum class FusionDataLevel : std::uint8_t {
  kFeatureLevel = 1,
};

struct FusionResultSummary final {
  std::uint64_t fusion_sequence;
  contracts::SimTime started_at;
  contracts::SimTime completed_at;
  contracts::SimDuration fusion_period;
  std::size_t observation_count;
  double estimated_target_x_meters;
  double estimated_target_y_meters;

  auto operator==(const FusionResultSummary&) const -> bool = default;
};

class AcceptanceRunProjection final {
 public:
  [[nodiscard]] static auto Build(
      const AcceptanceScenarioConfig& config,
      const phy::internal::RateBasedTxPhy& applied_tx_phy,
      std::span<const contracts::TraceEvent> trace_events,
      const FusionResultStore& fusion_result_store,
      const contracts::WorldSnapshot& final_snapshot)
      -> contracts::Result<AcceptanceRunProjection>;

  [[nodiscard]] constexpr auto profile() const noexcept
      -> AcceptanceScenarioProfile {
    return profile_;
  }

  [[nodiscard]] constexpr auto run_started_at() const noexcept
      -> contracts::SimTime {
    return run_started_at_;
  }

  [[nodiscard]] constexpr auto run_ended_at() const noexcept
      -> contracts::SimTime {
    return run_ended_at_;
  }

  [[nodiscard]] constexpr auto simulation_duration() const noexcept
      -> contracts::SimDuration {
    return simulation_duration_;
  }

  [[nodiscard]] constexpr auto cycle_count() const noexcept -> std::size_t {
    return cycle_count_;
  }

  [[nodiscard]] constexpr auto final_snapshot_version() const noexcept
      -> contracts::SnapshotVersion {
    return final_snapshot_version_;
  }

  [[nodiscard]] constexpr auto node_count() const noexcept -> std::size_t {
    return node_count_;
  }

  [[nodiscard]] constexpr auto mobile_node_count() const noexcept
      -> std::size_t {
    return mobile_node_count_;
  }

  [[nodiscard]] constexpr auto fusion_center_count() const noexcept
      -> std::size_t {
    return fusion_center_count_;
  }

  [[nodiscard]] constexpr auto effective_rate_bits_per_second() const noexcept
      -> std::uint64_t {
    return effective_rate_bits_per_second_;
  }

  [[nodiscard]] constexpr auto configured_rate_bits_per_second() const noexcept
      -> std::uint64_t {
    return configured_rate_bits_per_second_;
  }

  [[nodiscard]] constexpr auto transmission_count() const noexcept
      -> std::size_t {
    return transmission_count_;
  }

  [[nodiscard]] constexpr auto channel_signal_count() const noexcept
      -> std::size_t {
    return channel_signal_count_;
  }

  [[nodiscard]] constexpr auto channel_no_arrival_count() const noexcept
      -> std::size_t {
    return channel_no_arrival_count_;
  }

  [[nodiscard]] constexpr auto reception_count() const noexcept
      -> std::size_t {
    return reception_count_;
  }

  [[nodiscard]] constexpr auto not_decoded_count() const noexcept
      -> std::size_t {
    return not_decoded_count_;
  }

  [[nodiscard]] constexpr auto overheard_count() const noexcept
      -> std::size_t {
    return overheard_count_;
  }

  [[nodiscard]] constexpr auto local_delivery_count() const noexcept
      -> std::size_t {
    return local_delivery_count_;
  }

  [[nodiscard]] constexpr auto relay_enqueue_count() const noexcept
      -> std::size_t {
    return relay_enqueue_count_;
  }

  [[nodiscard]] constexpr auto fusion_result_count() const noexcept
      -> std::size_t {
    return fusion_result_count_;
  }

  [[nodiscard]] constexpr auto first_fusion_result() const noexcept
      -> const std::optional<FusionResultSummary>& {
    return first_fusion_result_;
  }

  [[nodiscard]] constexpr auto latest_fusion_result() const noexcept
      -> const std::optional<FusionResultSummary>& {
    return latest_fusion_result_;
  }

  [[nodiscard]] constexpr auto minimum_observation_count() const noexcept
      -> std::optional<std::size_t> {
    return minimum_observation_count_;
  }

  [[nodiscard]] constexpr auto maximum_completed_fusion_period() const noexcept
      -> std::optional<contracts::SimDuration> {
    return maximum_completed_fusion_period_;
  }

  [[nodiscard]] constexpr auto fusion_data_level() const noexcept
      -> FusionDataLevel {
    return fusion_data_level_;
  }

  [[nodiscard]] constexpr auto required_minimum_bearing_points() const noexcept
      -> std::size_t {
    return required_minimum_bearing_points_;
  }

  [[nodiscard]] constexpr auto required_maximum_fusion_period() const noexcept
      -> contracts::SimDuration {
    return required_maximum_fusion_period_;
  }

  [[nodiscard]] constexpr auto required_maximum_ber() const noexcept
      -> double {
    return required_maximum_ber_;
  }

  auto operator==(const AcceptanceRunProjection&) const -> bool = default;

 private:
  AcceptanceRunProjection(
      AcceptanceScenarioProfile profile,
      contracts::SimTime run_started_at,
      contracts::SimTime run_ended_at,
      contracts::SimDuration simulation_duration,
      std::size_t cycle_count,
      contracts::SnapshotVersion final_snapshot_version,
      std::size_t node_count,
      std::size_t mobile_node_count,
      std::size_t fusion_center_count,
      std::uint64_t configured_rate_bits_per_second,
      std::uint64_t effective_rate_bits_per_second,
      std::size_t transmission_count,
      std::size_t channel_signal_count,
      std::size_t channel_no_arrival_count,
      std::size_t reception_count,
      std::size_t not_decoded_count,
      std::size_t overheard_count,
      std::size_t local_delivery_count,
      std::size_t relay_enqueue_count,
      std::size_t fusion_result_count,
      std::optional<FusionResultSummary> first_fusion_result,
      std::optional<FusionResultSummary> latest_fusion_result,
      std::optional<std::size_t> minimum_observation_count,
      std::optional<contracts::SimDuration> maximum_completed_fusion_period,
      std::size_t required_minimum_bearing_points,
      contracts::SimDuration required_maximum_fusion_period,
      double required_maximum_ber) noexcept
      : profile_(profile),
        run_started_at_(run_started_at),
        run_ended_at_(run_ended_at),
        simulation_duration_(simulation_duration),
        cycle_count_(cycle_count),
        final_snapshot_version_(final_snapshot_version),
        node_count_(node_count),
        mobile_node_count_(mobile_node_count),
        fusion_center_count_(fusion_center_count),
        configured_rate_bits_per_second_(configured_rate_bits_per_second),
        effective_rate_bits_per_second_(effective_rate_bits_per_second),
        transmission_count_(transmission_count),
        channel_signal_count_(channel_signal_count),
        channel_no_arrival_count_(channel_no_arrival_count),
        reception_count_(reception_count),
        not_decoded_count_(not_decoded_count),
        overheard_count_(overheard_count),
        local_delivery_count_(local_delivery_count),
        relay_enqueue_count_(relay_enqueue_count),
        fusion_result_count_(fusion_result_count),
        first_fusion_result_(std::move(first_fusion_result)),
        latest_fusion_result_(std::move(latest_fusion_result)),
        minimum_observation_count_(minimum_observation_count),
        maximum_completed_fusion_period_(maximum_completed_fusion_period),
        required_minimum_bearing_points_(required_minimum_bearing_points),
        required_maximum_fusion_period_(required_maximum_fusion_period),
        required_maximum_ber_(required_maximum_ber) {}

  AcceptanceScenarioProfile profile_;
  contracts::SimTime run_started_at_;
  contracts::SimTime run_ended_at_;
  contracts::SimDuration simulation_duration_;
  std::size_t cycle_count_;
  contracts::SnapshotVersion final_snapshot_version_;
  std::size_t node_count_;
  std::size_t mobile_node_count_;
  std::size_t fusion_center_count_;
  std::uint64_t configured_rate_bits_per_second_;
  std::uint64_t effective_rate_bits_per_second_;
  std::size_t transmission_count_;
  std::size_t channel_signal_count_;
  std::size_t channel_no_arrival_count_;
  std::size_t reception_count_;
  std::size_t not_decoded_count_;
  std::size_t overheard_count_;
  std::size_t local_delivery_count_;
  std::size_t relay_enqueue_count_;
  std::size_t fusion_result_count_;
  std::optional<FusionResultSummary> first_fusion_result_;
  std::optional<FusionResultSummary> latest_fusion_result_;
  std::optional<std::size_t> minimum_observation_count_;
  std::optional<contracts::SimDuration> maximum_completed_fusion_period_;
  FusionDataLevel fusion_data_level_{FusionDataLevel::kFeatureLevel};
  std::size_t required_minimum_bearing_points_;
  contracts::SimDuration required_maximum_fusion_period_;
  double required_maximum_ber_;
};

enum class AcceptanceMetricStatus : std::uint8_t {
  kPass = 1,
  kFail = 2,
  kNotEvaluated = 3,
};

enum class AcceptanceOverallStatus : std::uint8_t {
  kPass = 1,
  kFail = 2,
  kNotFullyEvaluated = 3,
};

enum class AcceptanceEvidenceSource : std::uint8_t {
  kScenarioConfig = 1,
  kAppliedRateBasedTxPhyConfig = 2,
  kPhysicalRxBer = 3,
  kFusionWorkload = 4,
  kFusionObservationCount = 5,
  kFusionTimestamps = 6,
};

struct NetworkNodeCountAssessment final {
  std::size_t measured_node_count;
  std::size_t required_minimum{3};
  std::size_t required_maximum{4};
  AcceptanceMetricStatus status;
  AcceptanceEvidenceSource evidence{AcceptanceEvidenceSource::kScenarioConfig};

  auto operator==(const NetworkNodeCountAssessment&) const -> bool = default;
};

struct CommunicationRateAssessment final {
  std::uint64_t configured_bits_per_second;
  std::uint64_t effective_bits_per_second;
  std::uint64_t required_bits_per_second{60};
  AcceptanceMetricStatus status;
  AcceptanceEvidenceSource evidence{
      AcceptanceEvidenceSource::kAppliedRateBasedTxPhyConfig};

  auto operator==(const CommunicationRateAssessment&) const -> bool = default;
};

struct BitErrorRateAssessment final {
  std::optional<double> measured_ber;
  double required_maximum_ber;
  AcceptanceMetricStatus status{AcceptanceMetricStatus::kNotEvaluated};
  AcceptanceEvidenceSource evidence{AcceptanceEvidenceSource::kPhysicalRxBer};
  std::string_view reason;

  auto operator==(const BitErrorRateAssessment&) const -> bool = default;
};

struct FeatureLevelFusionAssessment final {
  FusionDataLevel measured_level;
  FusionDataLevel required_level{FusionDataLevel::kFeatureLevel};
  AcceptanceMetricStatus status;
  AcceptanceEvidenceSource evidence{AcceptanceEvidenceSource::kFusionWorkload};

  auto operator==(const FeatureLevelFusionAssessment&) const -> bool = default;
};

struct BearingPointCountAssessment final {
  std::optional<std::size_t> measured_minimum_per_result;
  std::size_t required_minimum_per_result;
  std::size_t fusion_result_count;
  AcceptanceMetricStatus status;
  AcceptanceEvidenceSource evidence{
      AcceptanceEvidenceSource::kFusionObservationCount};

  auto operator==(const BearingPointCountAssessment&) const -> bool = default;
};

struct FusionPeriodAssessment final {
  std::optional<contracts::SimDuration> measured_first_period;
  std::optional<contracts::SimDuration> measured_maximum_period;
  contracts::SimDuration required_maximum_period;
  std::size_t fusion_result_count;
  AcceptanceMetricStatus status;
  AcceptanceEvidenceSource evidence{
      AcceptanceEvidenceSource::kFusionTimestamps};

  auto operator==(const FusionPeriodAssessment&) const -> bool = default;
};

struct AcceptanceRunReport final {
  AcceptanceRunProjection projection;
  NetworkNodeCountAssessment network_node_count;
  CommunicationRateAssessment communication_rate;
  BitErrorRateAssessment bit_error_rate;
  FeatureLevelFusionAssessment feature_level_fusion;
  BearingPointCountAssessment bearing_point_count;
  FusionPeriodAssessment fusion_period;
  AcceptanceOverallStatus overall_status;

  auto operator==(const AcceptanceRunReport&) const -> bool = default;
};

[[nodiscard]] auto BuildAcceptanceRunReport(
    const AcceptanceRunProjection& projection)
    -> std::optional<AcceptanceRunReport>;

[[nodiscard]] auto FormatAcceptanceRunReport(
    const AcceptanceRunReport& report) -> std::string;

namespace acceptance_run_report_detail {

[[nodiscard]] inline auto ValidationError(std::string message)
    -> contracts::Error {
  return contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                          std::move(message)};
}

[[nodiscard]] inline auto Summarize(const FusionResult& result)
    -> FusionResultSummary {
  return FusionResultSummary{result.fusion_sequence,
                             result.started_at,
                             result.completed_at,
                             result.fusion_period,
                             result.observation_count,
                             result.estimated_target_x_meters,
                             result.estimated_target_y_meters};
}

[[nodiscard]] inline auto StatusText(AcceptanceMetricStatus status)
    -> std::string_view {
  switch(status) {
    case AcceptanceMetricStatus::kPass:
      return "PASS";
    case AcceptanceMetricStatus::kFail:
      return "FAIL";
    case AcceptanceMetricStatus::kNotEvaluated:
      return "NOT_EVALUATED";
  }
  return "UNKNOWN";
}

[[nodiscard]] inline auto OverallText(AcceptanceOverallStatus status)
    -> std::string_view {
  switch(status) {
    case AcceptanceOverallStatus::kPass:
      return "PASS";
    case AcceptanceOverallStatus::kFail:
      return "FAIL";
    case AcceptanceOverallStatus::kNotFullyEvaluated:
      return "NOT_FULLY_EVALUATED";
  }
  return "UNKNOWN";
}

[[nodiscard]] inline auto FormatDuration(contracts::SimDuration duration)
    -> std::string {
  constexpr auto kNanosecondsPerSecond = std::int64_t{1'000'000'000};
  const auto nanoseconds = duration.nanoseconds();
  const auto seconds = nanoseconds / kNanosecondsPerSecond;
  const auto remainder = nanoseconds % kNanosecondsPerSecond;
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << seconds << '.' << std::setw(9) << std::setfill('0') << remainder
         << " s";
  return output.str();
}

[[nodiscard]] inline auto FormatOptionalDuration(
    std::optional<contracts::SimDuration> duration) -> std::string {
  return duration ? FormatDuration(*duration) : "unavailable";
}

}  // namespace acceptance_run_report_detail

inline auto AcceptanceRunProjection::Build(
    const AcceptanceScenarioConfig& config,
    const phy::internal::RateBasedTxPhy& applied_tx_phy,
    std::span<const contracts::TraceEvent> trace_events,
    const FusionResultStore& fusion_result_store,
    const contracts::WorldSnapshot& final_snapshot)
    -> contracts::Result<AcceptanceRunProjection> {
  const auto initial_snapshot = config.InitialWorldSnapshot();
  if(!initial_snapshot) return std::unexpected(initial_snapshot.error());
  if(final_snapshot.nodes().size() != config.nodes().size()) {
    return std::unexpected(acceptance_run_report_detail::ValidationError(
        "Final snapshot node universe does not match acceptance config"));
  }
  for(std::size_t index = 0; index < config.nodes().size(); ++index) {
    if(final_snapshot.nodes()[index].node_id !=
       config.nodes()[index].initial_state.node_id) {
      return std::unexpected(acceptance_run_report_detail::ValidationError(
          "Final snapshot node identity does not match acceptance config"));
    }
  }
  const auto duration = contracts::CheckedSubtract(
      final_snapshot.committed_at(), initial_snapshot->committed_at());
  if(!duration || *duration < contracts::SimDuration::Zero()) {
    return std::unexpected(acceptance_run_report_detail::ValidationError(
        "Final snapshot precedes acceptance run start"));
  }
  for(std::size_t index = 1; index < trace_events.size(); ++index) {
    if(trace_events[index].occurred_at() <
       trace_events[index - 1].occurred_at()) {
      return std::unexpected(acceptance_run_report_detail::ValidationError(
          "Trace sequence is not in deterministic simulation-time order"));
    }
  }

  auto cycle_count = std::size_t{0};
  auto transmission_count = std::size_t{0};
  auto channel_signal_count = std::size_t{0};
  auto channel_no_arrival_count = std::size_t{0};
  auto reception_count = std::size_t{0};
  auto not_decoded_count = std::size_t{0};
  auto overheard_count = std::size_t{0};
  auto local_delivery_count = std::size_t{0};
  auto relay_enqueue_count = std::size_t{0};
  std::optional<contracts::CycleCommitTrace> latest_commit;
  for(const auto& event : trace_events) {
    if(const auto* commit =
           std::get_if<contracts::CycleCommitTrace>(&event.payload())) {
      ++cycle_count;
      latest_commit = *commit;
    } else if(std::holds_alternative<contracts::TransmissionTrace>(
                  event.payload())) {
      ++transmission_count;
    } else if(const auto* channel =
                  std::get_if<contracts::ChannelOutcomeTrace>(
                      &event.payload())) {
      if(std::holds_alternative<contracts::TraceSignalChannelOutcome>(
             channel->outcome)) {
        ++channel_signal_count;
      } else {
        ++channel_no_arrival_count;
      }
    } else if(const auto* reception =
                  std::get_if<contracts::ReceptionTrace>(&event.payload())) {
      ++reception_count;
      switch(reception->disposition) {
        case contracts::TraceReceptionDisposition::kNotDecoded:
          ++not_decoded_count;
          break;
        case contracts::TraceReceptionDisposition::kOverheard:
          ++overheard_count;
          break;
        case contracts::TraceReceptionDisposition::kLocalDelivery:
          ++local_delivery_count;
          break;
        case contracts::TraceReceptionDisposition::kRelayEnqueue:
          ++relay_enqueue_count;
          break;
      }
    }
  }
  if(!latest_commit ||
     latest_commit->committed_snapshot_version != final_snapshot.version() ||
     latest_commit->committed_at != final_snapshot.committed_at()) {
    return std::unexpected(acceptance_run_report_detail::ValidationError(
        "Latest CycleCommit trace does not identify the final snapshot"));
  }

  const auto fusion_results = fusion_result_store.results();
  std::optional<std::size_t> minimum_observation_count;
  std::optional<contracts::SimDuration> maximum_fusion_period;
  std::vector<ObservationIdentity> consumed_identities;
  for(const auto& result : fusion_results) {
    const auto computed_period =
        contracts::CheckedSubtract(result.completed_at, result.started_at);
    if(!computed_period || *computed_period != result.fusion_period ||
       result.observation_count != result.observation_identities.size() ||
       !std::is_sorted(result.observation_identities.begin(),
                       result.observation_identities.end()) ||
       std::adjacent_find(result.observation_identities.begin(),
                          result.observation_identities.end()) !=
           result.observation_identities.end()) {
      return std::unexpected(acceptance_run_report_detail::ValidationError(
          "Fusion result timing or observation provenance is invalid"));
    }
    minimum_observation_count =
        minimum_observation_count
            ? std::min(*minimum_observation_count, result.observation_count)
            : result.observation_count;
    maximum_fusion_period =
        maximum_fusion_period
            ? std::max(*maximum_fusion_period, result.fusion_period)
            : result.fusion_period;
    consumed_identities.insert(consumed_identities.end(),
                               result.observation_identities.begin(),
                               result.observation_identities.end());
  }
  std::sort(consumed_identities.begin(), consumed_identities.end());
  if(std::adjacent_find(consumed_identities.begin(),
                        consumed_identities.end()) !=
     consumed_identities.end()) {
    return std::unexpected(acceptance_run_report_detail::ValidationError(
        "Fusion windows reuse an observation identity"));
  }

  const auto first_fusion = fusion_results.empty()
                                ? std::optional<FusionResultSummary>{}
                                : acceptance_run_report_detail::Summarize(
                                      fusion_results.front());
  const auto latest_fusion = fusion_results.empty()
                                 ? std::optional<FusionResultSummary>{}
                                 : acceptance_run_report_detail::Summarize(
                                       fusion_results.back());
  return AcceptanceRunProjection{
      config.profile(),
      initial_snapshot->committed_at(),
      final_snapshot.committed_at(),
      *duration,
      cycle_count,
      final_snapshot.version(),
      config.nodes().size(),
      config.sensor_node_ids().size(),
      std::size_t{1},
      config.communication_rate_bits_per_second(),
      applied_tx_phy.config().bits_per_second,
      transmission_count,
      channel_signal_count,
      channel_no_arrival_count,
      reception_count,
      not_decoded_count,
      overheard_count,
      local_delivery_count,
      relay_enqueue_count,
      fusion_results.size(),
      first_fusion,
      latest_fusion,
      minimum_observation_count,
      maximum_fusion_period,
      config.minimum_bearing_points(),
      config.maximum_fusion_period(),
      config.ber_requirement()};
}

inline auto BuildAcceptanceRunReport(
    const AcceptanceRunProjection& projection)
    -> std::optional<AcceptanceRunReport> {
  if(projection.profile() !=
     AcceptanceScenarioProfile::kAcceptance4Node) {
    return std::nullopt;
  }
  const auto node_status =
      projection.node_count() >= 3 && projection.node_count() <= 4
          ? AcceptanceMetricStatus::kPass
          : AcceptanceMetricStatus::kFail;
  const auto rate_status =
      projection.effective_rate_bits_per_second() == 60
          ? AcceptanceMetricStatus::kPass
          : AcceptanceMetricStatus::kFail;
  const auto fusion_level_status =
      projection.fusion_data_level() == FusionDataLevel::kFeatureLevel
          ? AcceptanceMetricStatus::kPass
          : AcceptanceMetricStatus::kFail;
  const auto bearing_status =
      projection.minimum_observation_count() &&
              *projection.minimum_observation_count() >=
                  projection.required_minimum_bearing_points()
          ? AcceptanceMetricStatus::kPass
          : AcceptanceMetricStatus::kFail;
  const auto period_status =
      projection.maximum_completed_fusion_period() &&
              *projection.maximum_completed_fusion_period() <=
                  projection.required_maximum_fusion_period()
          ? AcceptanceMetricStatus::kPass
          : AcceptanceMetricStatus::kFail;

  const std::array statuses{node_status,
                            rate_status,
                            AcceptanceMetricStatus::kNotEvaluated,
                            fusion_level_status,
                            bearing_status,
                            period_status};
  auto overall = AcceptanceOverallStatus::kPass;
  if(std::ranges::find(statuses, AcceptanceMetricStatus::kFail) !=
     statuses.end()) {
    overall = AcceptanceOverallStatus::kFail;
  } else if(std::ranges::find(statuses,
                              AcceptanceMetricStatus::kNotEvaluated) !=
            statuses.end()) {
    overall = AcceptanceOverallStatus::kNotFullyEvaluated;
  }

  return AcceptanceRunReport{
      projection,
      NetworkNodeCountAssessment{projection.node_count(), 3, 4, node_status},
      CommunicationRateAssessment{
          projection.configured_rate_bits_per_second(),
          projection.effective_rate_bits_per_second(),
          60,
          rate_status},
      BitErrorRateAssessment{
          std::nullopt,
          projection.required_maximum_ber(),
          AcceptanceMetricStatus::kNotEvaluated,
          AcceptanceEvidenceSource::kPhysicalRxBer,
          "Physical Rx provider does not expose auditable BER yet."},
      FeatureLevelFusionAssessment{projection.fusion_data_level(),
                                   FusionDataLevel::kFeatureLevel,
                                   fusion_level_status},
      BearingPointCountAssessment{
          projection.minimum_observation_count(),
          projection.required_minimum_bearing_points(),
          projection.fusion_result_count(),
          bearing_status},
      FusionPeriodAssessment{
          projection.first_fusion_result()
              ? std::optional{projection.first_fusion_result()->fusion_period}
              : std::nullopt,
          projection.maximum_completed_fusion_period(),
          projection.required_maximum_fusion_period(),
          projection.fusion_result_count(),
          period_status},
      overall};
}

inline auto FormatAcceptanceRunReport(
    const AcceptanceRunReport& report) -> std::string {
  using acceptance_run_report_detail::FormatDuration;
  using acceptance_run_report_detail::OverallText;
  using acceptance_run_report_detail::StatusText;
  const auto& projection = report.projection;
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << "P0 Acceptance Run Report\n"
         << "Run: "
         << FormatDuration(contracts::SimDuration::FromNanoseconds(
                projection.run_started_at().nanoseconds()))
         << " -> "
         << FormatDuration(contracts::SimDuration::FromNanoseconds(
                projection.run_ended_at().nanoseconds()))
         << " (" << FormatDuration(projection.simulation_duration())
         << "), cycles=" << projection.cycle_count()
         << ", snapshot=" << projection.final_snapshot_version().value()
         << '\n'
         << "Runtime: transmissions=" << projection.transmission_count()
         << ", channel_signal=" << projection.channel_signal_count()
         << ", channel_no_arrival="
         << projection.channel_no_arrival_count()
         << ", receptions=" << projection.reception_count() << '\n'
         << "Delivery: not_decoded=" << projection.not_decoded_count()
         << ", overheard=" << projection.overheard_count()
         << ", local_delivery=" << projection.local_delivery_count()
         << ", relay_enqueue=" << projection.relay_enqueue_count() << '\n'
         << "Fusion: results=" << projection.fusion_result_count()
         << ", first_period="
         << acceptance_run_report_detail::FormatOptionalDuration(
                report.fusion_period.measured_first_period)
         << ", max_period="
         << acceptance_run_report_detail::FormatOptionalDuration(
                report.fusion_period.measured_maximum_period)
         << '\n'
         << "NetworkNodeCount: measured="
         << report.network_node_count.measured_node_count
         << ", requirement=3..4, status="
         << StatusText(report.network_node_count.status)
         << ", evidence=AcceptanceScenarioConfig\n"
         << "CommunicationRate: configured="
         << report.communication_rate.configured_bits_per_second
         << " bit/s, effective="
         << report.communication_rate.effective_bits_per_second
         << " bit/s, requirement=60 bit/s, status="
         << StatusText(report.communication_rate.status)
         << ", evidence=applied RateBasedTxPhy config\n"
         << "BitErrorRate: measured=unavailable, requirement<=0.0001, status="
         << StatusText(report.bit_error_rate.status)
         << ", evidence=" << report.bit_error_rate.reason << '\n'
         << "FeatureLevelFusion: measured=feature-level, "
            "requirement=feature-level, status="
         << StatusText(report.feature_level_fusion.status)
         << ", evidence=FusionResultStore workload\n"
         << "BearingPointCount: measured_min="
         << (report.bearing_point_count.measured_minimum_per_result
                 ? std::to_string(
                       *report.bearing_point_count.measured_minimum_per_result)
                 : std::string{"unavailable"})
         << ", requirement>=5 per result, status="
         << StatusText(report.bearing_point_count.status)
         << ", evidence=FusionResult.observation_count\n"
         << "FusionPeriod: measured_first="
         << acceptance_run_report_detail::FormatOptionalDuration(
                report.fusion_period.measured_first_period)
         << ", measured_max="
         << acceptance_run_report_detail::FormatOptionalDuration(
                report.fusion_period.measured_maximum_period)
         << ", requirement<="
         << FormatDuration(report.fusion_period.required_maximum_period)
         << ", status=" << StatusText(report.fusion_period.status)
         << ", evidence=FusionResult timestamps\n"
         << "Overall: " << OverallText(report.overall_status) << '\n';
  return output.str();
}

}  // namespace ns3_factory::assembly::internal
