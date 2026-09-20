#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/received_signal.hpp>
#include <ns3_factory/contracts/receiver_window.hpp>

#include "internal/sampled_scalar_phy.hpp"

namespace {
using namespace ns3_factory::contracts;
using namespace ns3_factory::phy::internal;

auto Check(bool value, std::string_view label) -> bool {
  if(!value) std::cerr << "FAILED: " << label << '\n';
  return value;
}

struct Observer final : ISampledScalarReceptionObserver {
  void Observe(const SampledScalarReceptionSummary& summary) noexcept override {
    rows.push_back(summary);
  }
  std::vector<SampledScalarReceptionSummary> rows;
};

auto MakeRequest(const TxEmission& emission, double loss, double noise_db)
    -> Result<RxDecodeRequest> {
  auto response = ChannelFieldResponse::Create(
      emission.transmission_id(), NodeId{2}, loss,
      SimDuration::FromNanoseconds(1'000'000), {});
  if(!response) return std::unexpected(response.error());
  auto signal = ReceivedSignal::Create(emission, *response);
  if(!signal) return std::unexpected(signal.error());
  auto window = ReceiverWindow::Create(NodeId{2}, *signal, {});
  if(!window) return std::unexpected(window.error());
  auto noise = NoiseObservation::Create(
      NodeId{2}, signal->first_arrival_at(), signal->last_effect_end_at(),
      signal->lower_frequency_hz(), signal->upper_frequency_hz(), noise_db);
  if(!noise) return std::unexpected(noise.error());
  return RxDecodeRequest::Create(*window, *noise);
}

auto TestPureSampling() -> bool {
  const std::vector<std::byte> payload{std::byte{0xa5}, std::byte{0x5a}};
  auto frame = AddWaveformCrc(payload);
  if(!frame) return Check(false, "CRC fixture");
  const auto clean = SampleScalarFrameBits(
      *frame, 0.0, 42U, TransmissionId{11}, NodeId{2});
  const auto all_flipped = SampleScalarFrameBits(
      *frame, 1.0, 42U, TransmissionId{11}, NodeId{2});
  const auto first = SampleScalarFrameBits(
      *frame, 0.5, 42U, TransmissionId{11}, NodeId{2});
  const auto repeat = SampleScalarFrameBits(
      *frame, 0.5, 42U, TransmissionId{11}, NodeId{2});
  const auto other_receiver = SampleScalarFrameBits(
      *frame, 0.5, 42U, TransmissionId{11}, NodeId{3});
  const auto invalid = SampleScalarFrameBits(
      *frame, 1.1, 42U, TransmissionId{11}, NodeId{2});
  if(!clean || !all_flipped || !first || !repeat || !other_receiver) {
    return Check(false, "Sampling fixtures");
  }
  return Check(clean->bytes == *frame &&
                   clean->payload_bit_errors == 0U &&
                   CheckWaveformCrc(clean->bytes),
               "Zero BER keeps original frame") &&
         Check(all_flipped->payload_bit_errors == 16U &&
                   all_flipped->bytes != *frame,
               "Unit BER flips every payload bit") &&
         Check(first->bytes == repeat->bytes &&
                   first->payload_bit_errors == repeat->payload_bit_errors,
               "Equal seed and identities reproduce exact bit flips") &&
         Check(first->bytes != other_receiver->bytes,
               "Receiver identity gives an independent stream") &&
         Check(!invalid, "Out-of-range BER is rejected");
}

auto TestProviderRoundTripAndFailure() -> bool {
  ScalarFrameStore store;
  Observer observer;
  const auto tx = SampledScalarTxPhy::Create(
      RateBasedTxPhyConfig{60U, 25'000.0, 4'000.0, 110.0}, store);
  const auto rx = SampledScalarRxPhy::Create(60U, 91U, store, observer);
  if(!tx || !rx) return Check(false, "Sampled PHY construction");
  const DigitalPacket packet{
      PacketId{7}, NodeId{1}, UnicastDestination{NodeId{2}},
      std::vector<std::byte>(15U, std::byte{0xa5})};
  const TxEncodeRequest encode{
      TransmissionId{9}, NodeId{1},
      UnicastTransmissionTarget{NodeId{2}}, SimTime::Zero()};
  const auto emission = tx->Encode(packet, encode);
  if(!emission) return Check(false, "Sampled Tx encoding");
  const auto good = MakeRequest(*emission, 10.0, -100.0);
  const auto bad = MakeRequest(*emission, 300.0, 45.0);
  if(!good || !bad) return Check(false, "Sampled Rx requests");
  const auto recovered = rx->Decode(*good);
  const auto failed = rx->Decode(*bad);
  const auto repeat = rx->Decode(*bad);
  if(!recovered || !failed || !repeat) {
    return Check(false, "Sampled Rx execution");
  }
  const auto expected_airtime =
      ComputePayloadAirtime(packet.payload.size() + kWaveformCrcBytes, 60U);
  if(!Check(expected_airtime &&
                emission->duration() == *expected_airtime,
            "Scalar comparison uses payload plus CRC airtime") ||
     !Check(recovered->outcome() == DecodeOutcome::kDecoded &&
                recovered->recovered_payload() &&
                *recovered->recovered_payload() == packet.payload &&
                observer.rows[0].bit_error_count == 0U,
            "No sampled errors deliver recovered payload") ||
     !Check(failed->outcome() == DecodeOutcome::kNotDecoded &&
                !failed->recovered_payload() &&
                observer.rows[1].bit_error_count > 0U &&
                !observer.rows[1].crc_ok,
            "Sampled payload errors cause packet failure") ||
     !Check(*failed == *repeat &&
                observer.rows[1].bit_error_count ==
                    observer.rows[2].bit_error_count,
            "Repeated Decode is deterministic")) {
    return false;
  }
  store.ReleaseCycleArtifacts();
  return Check(store.size() == 0U && !rx->Decode(*good),
               "Cycle cleanup removes scalar frame artifact");
}

}  // namespace

int main() {
  return TestPureSampling() && TestProviderRoundTripAndFailure()
             ? EXIT_SUCCESS : EXIT_FAILURE;
}
