#include <cstddef>
#include <cstdlib>

#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/configured_tx_phy.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::phy::internal;

namespace {

auto TestFrozenTxContractAndZeroIdentities() -> bool {
  auto tx_phy = ConfiguredTxPhy::Create(
      ConfiguredTxPhyConfig{
          ModulationConfig{ModulationScheme::kBpsk,
                           8'000.0,
                           1'000.0,
                           1.0e6,
                           12'000.0,
                           0.0},
          SimDuration::FromNanoseconds(125'000),
          4'000.0,
          120.0});
  if(!tx_phy) return false;

  const DigitalPacket packet{PacketId{0},
                             NodeId{0},
                             UnicastDestination{NodeId{1}},
                             {std::byte{0xA5}, std::byte{0x03}}};
  const TxEncodeRequest request{TransmissionId{0},
                                NodeId{0},
                                UnicastTransmissionTarget{NodeId{1}},
                                SimTime::Zero()};
  const auto first = tx_phy->Encode(packet, request);
  const auto second = tx_phy->Encode(packet, request);
  return first && second && *first == *second &&
         ValidateTxEmissionIdentity(packet, request, *first) &&
         first->transmission_id() == TransmissionId{0} &&
         first->packet_id() == PacketId{0} &&
         first->sender_node_id() == NodeId{0} &&
         first->started_at() == SimTime::Zero() &&
         first->duration() == SimDuration::FromNanoseconds(16'000'000) &&
         first->center_frequency_hz() == 12'000.0 &&
         first->bandwidth_hz() == 4'000.0 &&
         first->source_level_db_re_1upa_at_1m() == 120.0;
}

auto TestConfigurationAndPayloadFailures() -> bool {
  const auto mismatched_period = ConfiguredTxPhy::Create(
      ConfiguredTxPhyConfig{
          ModulationConfig{ModulationScheme::kBpsk,
                           8'000.0,
                           1'000.0,
                           1.0e6,
                           12'000.0,
                           0.0},
          SimDuration::FromNanoseconds(125'001),
          4'000.0,
          120.0});
  const auto invalid_band = ConfiguredTxPhy::Create(
      ConfiguredTxPhyConfig{
          ModulationConfig{ModulationScheme::kBpsk,
                           8'000.0,
                           1'000.0,
                           1.0e6,
                           12'000.0,
                           0.0},
          SimDuration::FromNanoseconds(125'000),
          25'000.0,
          120.0});
  auto tx_phy = ConfiguredTxPhy::Create(
      ConfiguredTxPhyConfig{
          ModulationConfig{ModulationScheme::kBpsk,
                           8'000.0,
                           1'000.0,
                           1.0e6,
                           12'000.0,
                           0.0},
          SimDuration::FromNanoseconds(125'000),
          4'000.0,
          120.0});
  if(mismatched_period || invalid_band || !tx_phy) return false;
  const DigitalPacket empty{PacketId{1},
                            NodeId{0},
                            BroadcastDestination{},
                            {}};
  const TxEncodeRequest request{TransmissionId{1},
                                NodeId{0},
                                BroadcastTransmissionTarget{},
                                SimTime::Zero()};
  return !tx_phy->Encode(empty, request);
}

}  // namespace

auto main() -> int {
  return TestFrozenTxContractAndZeroIdentities() &&
                 TestConfigurationAndPayloadFailures()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
