#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "target_packet_metrics.hpp"

namespace ns3_factory::application::internal {

struct PairedPacketRun final {
  std::uint64_t seed;
  TargetPacketMetrics scalar;
  TargetPacketMetrics waveform;
};

struct PacketMetricTotals final {
  std::size_t target_attempt_count = 0U;
  std::size_t packet_error_count = 0U;
  std::size_t no_arrival_count = 0U;
  std::size_t observed_payload_bit_count = 0U;
  std::size_t payload_bit_error_count = 0U;

  [[nodiscard]] auto PacketErrorRate() const -> std::optional<double> {
    if(target_attempt_count == 0U) return std::nullopt;
    return static_cast<double>(packet_error_count) /
           static_cast<double>(target_attempt_count);
  }

  [[nodiscard]] auto BitErrorRate() const -> std::optional<double> {
    if(observed_payload_bit_count == 0U) return std::nullopt;
    return static_cast<double>(payload_bit_error_count) /
           static_cast<double>(observed_payload_bit_count);
  }
};

struct PercentileInterval final {
  double lower;
  double upper;
};

struct PairedPacketComparison final {
  std::size_t run_count;
  std::size_t bootstrap_resamples;
  PacketMetricTotals scalar;
  PacketMetricTotals waveform;
  std::optional<double> waveform_minus_scalar_ber;
  std::optional<double> waveform_minus_scalar_per;
  std::optional<PercentileInterval> ber_difference_interval_95;
  std::optional<PercentileInterval> per_difference_interval_95;
};

namespace paired_packet_comparison_detail {

[[nodiscard]] inline auto AddChecked(std::size_t lhs, std::size_t rhs)
    -> contracts::Result<std::size_t> {
  if(rhs > std::numeric_limits<std::size_t>::max() - lhs) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kOverflow, "Comparison counter overflow"});
  }
  return lhs + rhs;
}

[[nodiscard]] inline auto Add(PacketMetricTotals& totals,
                              const TargetPacketMetrics& metrics)
    -> contracts::Status {
  if(metrics.target_attempt_count == 0U ||
     metrics.no_arrival_count > metrics.target_attempt_count ||
     metrics.evaluated_target_receptions !=
         metrics.target_attempt_count - metrics.no_arrival_count ||
     metrics.packet_error_count > metrics.target_attempt_count ||
     metrics.payload_bit_error_count > metrics.evaluated_payload_bit_count) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "Paired run has inconsistent target-packet counters"});
  }
  auto attempts =
      AddChecked(totals.target_attempt_count, metrics.target_attempt_count);
  auto errors =
      AddChecked(totals.packet_error_count, metrics.packet_error_count);
  auto missing =
      AddChecked(totals.no_arrival_count, metrics.no_arrival_count);
  auto bits = AddChecked(totals.observed_payload_bit_count,
                         metrics.evaluated_payload_bit_count);
  auto bit_errors = AddChecked(totals.payload_bit_error_count,
                               metrics.payload_bit_error_count);
  if(!attempts) return std::unexpected(attempts.error());
  if(!errors) return std::unexpected(errors.error());
  if(!missing) return std::unexpected(missing.error());
  if(!bits) return std::unexpected(bits.error());
  if(!bit_errors) return std::unexpected(bit_errors.error());
  totals = {*attempts, *errors, *missing, *bits, *bit_errors};
  return {};
}

[[nodiscard]] inline auto DrawIndex(std::mt19937_64& random,
                                    std::size_t count) -> std::size_t {
  const auto bound = static_cast<std::uint64_t>(count);
  const auto threshold = static_cast<std::uint64_t>(-bound) % bound;
  std::uint64_t value;
  do {
    value = random();
  } while(value < threshold);
  return static_cast<std::size_t>(value % bound);
}

[[nodiscard]] inline auto Quantile(std::vector<double>& values, double fraction)
    -> double {
  std::sort(values.begin(), values.end());
  const auto position = fraction * static_cast<double>(values.size() - 1U);
  const auto lower = static_cast<std::size_t>(std::floor(position));
  const auto upper = std::min(lower + 1U, values.size() - 1U);
  return values[lower] +
         (position - static_cast<double>(lower)) *
             (values[upper] - values[lower]);
}

}  // namespace paired_packet_comparison_detail

// A paired cluster is one full simulation run, not a bit or a packet.
// Percentile intervals describe seed-to-seed variability conditional on this
// fixed scenario. They do not quantify uncertainty over other sea states.
[[nodiscard]] inline auto ComparePairedPacketRuns(
    std::span<const PairedPacketRun> runs,
    std::size_t bootstrap_resamples,
    std::uint64_t bootstrap_seed)
    -> contracts::Result<PairedPacketComparison> {
  using namespace paired_packet_comparison_detail;
  if(runs.size() < 2U || bootstrap_resamples < 100U ||
     runs.size() > std::numeric_limits<std::uint64_t>::max()) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "Comparison requires >=2 runs and >=100 bootstrap resamples"});
  }

  PacketMetricTotals scalar;
  PacketMetricTotals waveform;
  for(std::size_t index = 0U; index < runs.size(); ++index) {
    if(index != 0U && runs[index].seed <= runs[index - 1U].seed) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Comparison seeds must be unique and strictly ascending"});
    }
    auto same = ValidateSameTargetAttempts(runs[index].scalar,
                                           runs[index].waveform);
    if(!same) return std::unexpected(same.error());
    if(runs[index].scalar.target_attempt_count !=
       runs[index].scalar.attempts.size()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Attempt count does not match attempt identities"});
    }
    if(index != 0U &&
       runs[index].scalar.attempts != runs[0].scalar.attempts) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kFailedPrecondition,
          "Packet attempts vary across run seeds"});
    }
    auto scalar_added = Add(scalar, runs[index].scalar);
    auto waveform_added = Add(waveform, runs[index].waveform);
    if(!scalar_added) return std::unexpected(scalar_added.error());
    if(!waveform_added) return std::unexpected(waveform_added.error());
  }

  const auto ber_a = scalar.BitErrorRate();
  const auto ber_b = waveform.BitErrorRate();
  const auto per_a = scalar.PacketErrorRate();
  const auto per_b = waveform.PacketErrorRate();
  std::optional<double> ber_delta;
  if(ber_a && ber_b) ber_delta = *ber_b - *ber_a;
  std::optional<double> per_delta;
  if(per_a && per_b) per_delta = *per_b - *per_a;

  std::mt19937_64 random{bootstrap_seed};
  std::vector<double> ber_differences;
  std::vector<double> per_differences;
  ber_differences.reserve(bootstrap_resamples);
  per_differences.reserve(bootstrap_resamples);
  for(std::size_t resample = 0U; resample < bootstrap_resamples; ++resample) {
    PacketMetricTotals sampled_scalar;
    PacketMetricTotals sampled_waveform;
    for(std::size_t draw = 0U; draw < runs.size(); ++draw) {
      const auto& selected = runs[DrawIndex(random, runs.size())];
      auto scalar_added = Add(sampled_scalar, selected.scalar);
      auto waveform_added = Add(sampled_waveform, selected.waveform);
      if(!scalar_added) return std::unexpected(scalar_added.error());
      if(!waveform_added) return std::unexpected(waveform_added.error());
    }
    const auto sampled_ber_a = sampled_scalar.BitErrorRate();
    const auto sampled_ber_b = sampled_waveform.BitErrorRate();
    if(sampled_ber_a && sampled_ber_b) {
      ber_differences.push_back(*sampled_ber_b - *sampled_ber_a);
    }
    const auto sampled_per_a = sampled_scalar.PacketErrorRate();
    const auto sampled_per_b = sampled_waveform.PacketErrorRate();
    if(sampled_per_a && sampled_per_b) {
      per_differences.push_back(*sampled_per_b - *sampled_per_a);
    }
  }
  std::optional<PercentileInterval> ber_interval;
  std::optional<PercentileInterval> per_interval;
  if(ber_differences.size() == bootstrap_resamples) {
    ber_interval = PercentileInterval{
        Quantile(ber_differences, 0.025),
        Quantile(ber_differences, 0.975)};
  }
  if(per_differences.size() == bootstrap_resamples) {
    per_interval = PercentileInterval{
        Quantile(per_differences, 0.025),
        Quantile(per_differences, 0.975)};
  }
  return PairedPacketComparison{
      runs.size(), bootstrap_resamples, scalar, waveform,
      ber_delta, per_delta, ber_interval, per_interval};
}

}  // namespace ns3_factory::application::internal
