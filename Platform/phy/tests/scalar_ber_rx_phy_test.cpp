#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/received_signal.hpp>
#include <ns3_factory/contracts/receiver_window.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/scalar_ber_rx_phy.hpp"

namespace {

using namespace ns3_factory::contracts;
using namespace ns3_factory::phy::internal;

class FixedOutcomeRxPhy final : public IRxPhy {
 public:
  explicit FixedOutcomeRxPhy(DecodeOutcome outcome) noexcept
      : outcome_(outcome) {}

  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    ++decode_count;
    const auto& signal = request.receiver_window().desired_signal();
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  outcome_);
  }

  mutable std::size_t decode_count{0};

 private:
  DecodeOutcome outcome_;
};

auto MakeRequest(double source_level_db,
                 double transmission_loss_db,
                 double noise_power_db,
                 double bandwidth_hz = 4'000.0,
                 bool include_overlapping_signal = false)
    -> Result<RxDecodeRequest> {
  auto emission = TxEmission::Create(
      TransmissionId{1},
      PacketId{2},
      NodeId{3},
      SimTime::Zero(),
      SimDuration::FromNanoseconds(2'000'000'000),
      25'000.0,
      bandwidth_hz,
      source_level_db);
  if(!emission) return std::unexpected(emission.error());
  auto response = ChannelFieldResponse::Create(
      TransmissionId{1},
      NodeId{4},
      transmission_loss_db,
      SimDuration::Zero(),
      {});
  if(!response) return std::unexpected(response.error());
  auto signal = ReceivedSignal::Create(*emission, *response);
  if(!signal) return std::unexpected(signal.error());
  std::vector<ReceivedSignal> overlapping_signals;
  if(include_overlapping_signal) {
    auto overlapping_emission = TxEmission::Create(
        TransmissionId{5},
        PacketId{6},
        NodeId{7},
        SimTime::Zero(),
        SimDuration::FromNanoseconds(2'000'000'000),
        25'000.0,
        bandwidth_hz,
        source_level_db);
    if(!overlapping_emission) {
      return std::unexpected(overlapping_emission.error());
    }
    auto overlapping_response = ChannelFieldResponse::Create(
        TransmissionId{5},
        NodeId{4},
        transmission_loss_db,
        SimDuration::Zero(),
        {});
    if(!overlapping_response) {
      return std::unexpected(overlapping_response.error());
    }
    auto overlapping_signal =
        ReceivedSignal::Create(*overlapping_emission, *overlapping_response);
    if(!overlapping_signal) {
      return std::unexpected(overlapping_signal.error());
    }
    overlapping_signals.push_back(std::move(*overlapping_signal));
  }
  auto window = ReceiverWindow::Create(
      NodeId{4}, *signal, std::move(overlapping_signals));
  if(!window) return std::unexpected(window.error());
  auto noise = NoiseObservation::Create(
      NodeId{4},
      signal->first_arrival_at(),
      signal->last_effect_end_at(),
      signal->lower_frequency_hz(),
      signal->upper_frequency_hz(),
      noise_power_db);
  if(!noise) return std::unexpected(noise.error());
  return RxDecodeRequest::Create(*window, *noise);
}

auto Quality(double source_level_db,
             double transmission_loss_db,
             double noise_power_db,
             std::uint64_t bit_rate = 60)
    -> Result<RxQualityEvidence> {
  const auto request = MakeRequest(
      source_level_db, transmission_loss_db, noise_power_db);
  if(!request) return std::unexpected(request.error());
  return ComputeP0ModeledBpskAwgnQuality(*request, bit_rate);
}

auto TestFormulaAndEvidenceSource() -> bool {
  const auto request = MakeRequest(110.0, 70.0, 45.0);
  if(!request) return false;
  const auto quality = ComputeP0ModeledBpskAwgnQuality(*request, 60);
  if(!quality) return false;
  const auto expected_snr = -5.0;
  const auto expected_eb_n0 =
      expected_snr + 10.0 * std::log10(4'000.0 / 60.0);
  const auto expected_ber =
      0.5 * std::erfc(std::sqrt(std::pow(10.0, expected_eb_n0 / 10.0)));
  return std::abs(quality->signal_to_noise_ratio_db() - expected_snr) <
             1.0e-12 &&
         std::abs(quality->eb_n0_db() - expected_eb_n0) < 1.0e-12 &&
         std::abs(quality->bit_error_rate() - expected_ber) < 1.0e-15 &&
         quality->bit_error_rate() < 1.0e-4 &&
         quality->source() == RxQualityEvidenceSource::kModeled;
}

auto TestMonotonicity() -> bool {
  const auto baseline = Quality(110.0, 70.0, 45.0, 60);
  const auto more_noise = Quality(110.0, 70.0, 50.0, 60);
  const auto more_loss = Quality(110.0, 75.0, 45.0, 60);
  const auto more_source = Quality(115.0, 70.0, 45.0, 60);
  const auto more_rate = Quality(110.0, 70.0, 45.0, 120);
  return baseline && more_noise && more_loss && more_source && more_rate &&
         more_noise->signal_to_noise_ratio_db() <
             baseline->signal_to_noise_ratio_db() &&
         more_noise->eb_n0_db() < baseline->eb_n0_db() &&
         more_noise->bit_error_rate() > baseline->bit_error_rate() &&
         more_loss->bit_error_rate() > baseline->bit_error_rate() &&
         more_source->bit_error_rate() < baseline->bit_error_rate() &&
         more_rate->eb_n0_db() < baseline->eb_n0_db() &&
         more_rate->bit_error_rate() > baseline->bit_error_rate();
}

auto TestNumericalLimits() -> bool {
  const auto high = Quality(300.0, 0.0, 0.0);
  const auto low = Quality(0.0, 300.0, 0.0);
  return high && low && std::isfinite(high->bit_error_rate()) &&
         std::isfinite(low->bit_error_rate()) &&
         high->bit_error_rate() >= 0.0 && high->bit_error_rate() < 1.0e-20 &&
         low->bit_error_rate() <= 0.5 && low->bit_error_rate() > 0.4999;
}

auto TestDecoratorPreservesDecodeOutcome() -> bool {
  const auto request = MakeRequest(110.0, 70.0, 45.0);
  if(!request) return false;
  FixedOutcomeRxPhy decoded_inner{DecodeOutcome::kDecoded};
  FixedOutcomeRxPhy not_decoded_inner{DecodeOutcome::kNotDecoded};
  auto decoded = ScalarBerRxPhyDecorator::Create(decoded_inner, 60);
  auto not_decoded = ScalarBerRxPhyDecorator::Create(not_decoded_inner, 60);
  if(!decoded || !not_decoded ||
     ScalarBerRxPhyDecorator::Create(decoded_inner, 0)) {
    return false;
  }
  const auto decoded_result = decoded->Decode(*request);
  const auto not_decoded_result = not_decoded->Decode(*request);
  return decoded_result && not_decoded_result &&
         decoded_result->outcome() == DecodeOutcome::kDecoded &&
         not_decoded_result->outcome() == DecodeOutcome::kNotDecoded &&
         decoded_result->quality_evidence() &&
         not_decoded_result->quality_evidence() &&
         *decoded_result->quality_evidence() ==
             *not_decoded_result->quality_evidence() &&
         decoded_inner.decode_count == 1 &&
         not_decoded_inner.decode_count == 1;
}

auto TestOverlapAndQualityFailureRemainNonCausal() -> bool {
  const auto overlap_request =
      MakeRequest(110.0, 70.0, 45.0, 4'000.0, true);
  const auto failed_quality_request = MakeRequest(
      110.0,
      70.0,
      -std::numeric_limits<double>::max());
  if(!overlap_request || !failed_quality_request) return false;
  FixedOutcomeRxPhy inner{DecodeOutcome::kDecoded};
  auto observer = ScalarBerRxPhyDecorator::Create(inner, 60);
  if(!observer) return false;
  const auto overlap = observer->Decode(*overlap_request);
  const auto failed_quality = observer->Decode(*failed_quality_request);
  return overlap && failed_quality &&
         overlap->outcome() == DecodeOutcome::kDecoded &&
         failed_quality->outcome() == DecodeOutcome::kDecoded &&
         !overlap->quality_evidence() &&
         !failed_quality->quality_evidence() && inner.decode_count == 2;
}

}  // namespace

auto main() -> int {
  return TestFormulaAndEvidenceSource() && TestMonotonicity() &&
                 TestNumericalLimits() &&
                 TestDecoratorPreservesDecodeOutcome() &&
                 TestOverlapAndQualityFailureRemainNonCausal()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
