#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <ns3_factory/contracts/destination.hpp>

#include "internal/rate_based_tx_phy.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::phy::internal;

namespace {

auto Packet(std::size_t payload_bytes) -> DigitalPacket {
  return DigitalPacket{PacketId{7},
                       NodeId{1},
                       UnicastDestination{NodeId{2}},
                       std::vector<std::byte>(payload_bytes)};
}

auto TestExactAndRoundedAirtime() -> bool {
  const auto exact = ComputePayloadAirtime(15, 60);
  const auto rounded = ComputePayloadAirtime(1, 3);
  const auto zero_rate = ComputePayloadAirtime(1, 0);
  const auto empty = ComputePayloadAirtime(0, 60);
  return exact && exact->nanoseconds() == 2'000'000'000 && rounded &&
         rounded->nanoseconds() == 2'666'666'667 && !zero_rate &&
         zero_rate.error().code == ErrorCode::kInvalidArgument && !empty &&
         empty.error().code == ErrorCode::kInvalidArgument;
}

auto TestProviderConfigurationAndIdentity() -> bool {
  const auto phy = RateBasedTxPhy::Create(
      RateBasedTxPhyConfig{60, 25'000.0, 4'000.0, 110.0});
  const auto zero_rate = RateBasedTxPhy::Create(
      RateBasedTxPhyConfig{0, 25'000.0, 4'000.0, 110.0});
  const auto invalid_band = RateBasedTxPhy::Create(
      RateBasedTxPhyConfig{60, 1'000.0, 4'000.0, 110.0});
  if(!phy || zero_rate || invalid_band) return false;

  const auto packet = Packet(15);
  const TxEncodeRequest request{TransmissionId{9},
                                NodeId{1},
                                UnicastTransmissionTarget{NodeId{2}},
                                SimTime::FromNanoseconds(5)};
  const auto emission = phy->Encode(packet, request);
  return emission && emission->duration().nanoseconds() == 2'000'000'000 &&
         emission->center_frequency_hz() == 25'000.0 &&
         emission->source_level_db_re_1upa_at_1m() == 110.0 &&
         ValidateTxEmissionIdentity(packet, request, *emission);
}

}  // namespace

auto main() -> int {
  return TestExactAndRoundedAirtime() &&
                 TestProviderConfigurationAndIdentity()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
