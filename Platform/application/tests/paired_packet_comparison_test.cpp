#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

#include "internal/paired_packet_comparison.hpp"

namespace {

using namespace ns3_factory::application::internal;
using namespace ns3_factory::contracts;

auto Check(bool value, std::string_view label) -> bool {
  if(!value) std::cerr << "FAILED: " << label << '\n';
  return value;
}

auto MakeMetrics(std::size_t bit_errors, std::size_t packet_errors)
    -> TargetPacketMetrics {
  return TargetPacketMetrics{
      {{TransmissionId{1}, PacketId{11}, NodeId{2}, 100U},
       {TransmissionId{2}, PacketId{12}, NodeId{2}, 100U}},
      2U, 0U, 2U, packet_errors, 200U, bit_errors,
      static_cast<double>(bit_errors) / 200.0,
      static_cast<double>(packet_errors) / 2.0};
}

auto TestPairedBootstrap() -> bool {
  const std::vector<PairedPacketRun> runs{
      {101U, MakeMetrics(20U, 1U), MakeMetrics(10U, 1U)},
      {102U, MakeMetrics(30U, 2U), MakeMetrics(5U, 0U)}};
  const auto first = ComparePairedPacketRuns(runs, 1000U, 77U);
  const auto repeated = ComparePairedPacketRuns(runs, 1000U, 77U);
  if(!Check(first && repeated, "Paired bootstrap computes")) return false;
  return Check(first->scalar.target_attempt_count == 4U &&
                   first->waveform.target_attempt_count == 4U &&
                   first->scalar.observed_payload_bit_count == 400U &&
                   first->waveform.observed_payload_bit_count == 400U &&
                   first->waveform_minus_scalar_ber &&
                   std::abs(*first->waveform_minus_scalar_ber + 0.0875) < 1e-15 &&
                   first->waveform_minus_scalar_per &&
                   std::abs(*first->waveform_minus_scalar_per + 0.5) < 1e-15,
               "Pooled BER and PER use exact target denominators") &&
         Check(first->ber_difference_interval_95 &&
                   first->per_difference_interval_95 &&
                   first->ber_difference_interval_95->lower <=
                       *first->waveform_minus_scalar_ber &&
                   first->ber_difference_interval_95->upper >=
                       *first->waveform_minus_scalar_ber &&
                   first->per_difference_interval_95->lower <=
                       *first->waveform_minus_scalar_per &&
                   first->per_difference_interval_95->upper >=
                       *first->waveform_minus_scalar_per &&
                   first->ber_difference_interval_95->lower ==
                       repeated->ber_difference_interval_95->lower &&
                   first->per_difference_interval_95->upper ==
                       repeated->per_difference_interval_95->upper,
               "Paired bootstrap intervals reproduce with explicit seed");
}

auto TestInvalidAndMissingCoverage() -> bool {
  const std::vector<PairedPacketRun> duplicate_seed{
      {7U, MakeMetrics(0U, 0U), MakeMetrics(0U, 0U)},
      {7U, MakeMetrics(0U, 0U), MakeMetrics(0U, 0U)}};
  auto wrong_packet = MakeMetrics(0U, 0U);
  wrong_packet.attempts[0].packet_id = PacketId{999};
  const std::vector<PairedPacketRun> mismatched{
      {7U, MakeMetrics(0U, 0U), wrong_packet},
      {8U, MakeMetrics(0U, 0U), MakeMetrics(0U, 0U)}};
  auto absent_scalar = MakeMetrics(0U, 2U);
  absent_scalar.no_arrival_count = 2U;
  absent_scalar.evaluated_target_receptions = 0U;
  absent_scalar.evaluated_payload_bit_count = 0U;
  absent_scalar.bit_error_rate = std::nullopt;
  const std::vector<PairedPacketRun> no_bits{
      {7U, absent_scalar, MakeMetrics(0U, 0U)},
      {8U, absent_scalar, MakeMetrics(0U, 0U)}};
  const auto summary = ComparePairedPacketRuns(no_bits, 100U, 1U);
  return Check(!ComparePairedPacketRuns(duplicate_seed, 100U, 1U),
               "Duplicate seeds rejected") &&
         Check(!ComparePairedPacketRuns(mismatched, 100U, 1U),
               "Mismatched packet set rejected") &&
         Check(!ComparePairedPacketRuns(duplicate_seed, 99U, 1U),
               "Too few bootstrap samples rejected") &&
         Check(summary && !summary->waveform_minus_scalar_ber &&
                   !summary->ber_difference_interval_95 &&
                   summary->waveform_minus_scalar_per &&
                   summary->per_difference_interval_95,
               "No-arrival leaves BER undefined but PER measured");
}

}  // namespace

int main() {
  return TestPairedBootstrap() && TestInvalidAndMissingCoverage()
             ? EXIT_SUCCESS : EXIT_FAILURE;
}
