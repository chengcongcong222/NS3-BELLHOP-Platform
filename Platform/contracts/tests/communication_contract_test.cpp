#include <array>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/reception.hpp>
#include <ns3_factory/contracts/transmission.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

using ns3_factory::contracts::BroadcastDestination;
using ns3_factory::contracts::BroadcastTransmissionTarget;
using ns3_factory::contracts::DigitalPacket;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PacketDestination;
using ns3_factory::contracts::PacketId;
using ns3_factory::contracts::Reception;
using ns3_factory::contracts::ReceptionId;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::Transmission;
using ns3_factory::contracts::TransmissionId;
using ns3_factory::contracts::TransmissionTarget;
using ns3_factory::contracts::TxOpportunity;
using ns3_factory::contracts::UnicastDestination;
using ns3_factory::contracts::UnicastTransmissionTarget;

template <typename T>
concept HasReceiverIdMember = requires(T value) { value.receiver_node_id; };

template <typename T>
concept HasDestinationMember = requires(T value) { value.destination; };

template <typename T>
concept HasTargetMember = requires(T value) { value.target; };

template <typename T>
concept HasReceiverListMember = requires(T value) { value.receivers; } ||
                                requires(T value) { value.receiver_ids; } ||
                                requires(T value) {
                                  value.receiver_node_ids;
                                };

template <typename T>
concept HasCommitMethod = requires(T value) { value.Commit(); };

template <typename T>
concept HasApplyMethod = requires(T value) { value.Apply(); };

template <typename T>
concept HasScheduleMethod = requires(T value) { value.Schedule(); };

template <typename T>
concept PassiveValueDto =
    std::copy_constructible<T> && std::move_constructible<T> &&
    !HasCommitMethod<T> && !HasApplyMethod<T> && !HasScheduleMethod<T>;

static_assert(!std::same_as<PacketId, TransmissionId>);
static_assert(!std::same_as<TransmissionId, ReceptionId>);
static_assert(!std::constructible_from<PacketId, TransmissionId>);
static_assert(!std::constructible_from<TransmissionId, ReceptionId>);
static_assert(!std::constructible_from<ReceptionId, PacketId>);
static_assert(!std::same_as<PacketDestination, TransmissionTarget>);
static_assert(!std::constructible_from<TransmissionTarget,
                                       PacketDestination>);
static_assert(!std::constructible_from<PacketDestination,
                                       TransmissionTarget>);

static_assert(HasDestinationMember<DigitalPacket>);
static_assert(!HasDestinationMember<TxOpportunity>);
static_assert(!HasDestinationMember<Transmission>);
static_assert(!HasTargetMember<DigitalPacket>);
static_assert(!HasTargetMember<TxOpportunity>);
static_assert(HasTargetMember<Transmission>);
static_assert(!HasReceiverIdMember<TxOpportunity>);
static_assert(!HasReceiverIdMember<Transmission>);
static_assert(!HasReceiverListMember<Transmission>);
static_assert(HasReceiverIdMember<Reception>);
static_assert(std::same_as<decltype(std::declval<TxOpportunity>().eligible_at),
                           SimTime>);
static_assert(std::same_as<decltype(std::declval<Transmission>().started_at),
                           SimTime>);
static_assert(std::same_as<decltype(std::declval<Transmission>().ended_at),
                           SimTime>);
static_assert(std::same_as<decltype(std::declval<Reception>().arrival_at),
                           SimTime>);

static_assert(PassiveValueDto<DigitalPacket>);
static_assert(PassiveValueDto<TxOpportunity>);
static_assert(PassiveValueDto<Transmission>);
static_assert(PassiveValueDto<Reception>);
static_assert(!std::default_initializable<PacketDestination>);
static_assert(!std::default_initializable<TransmissionTarget>);
static_assert(std::is_empty_v<BroadcastDestination>);
static_assert(std::is_empty_v<BroadcastTransmissionTarget>);

constexpr PacketDestination kNodeZeroDestination =
    UnicastDestination{NodeId{0}};
constexpr PacketDestination kMaximumNodeDestination = UnicastDestination{
    NodeId{std::numeric_limits<NodeId::value_type>::max()}};
constexpr PacketDestination kBroadcastDestination = BroadcastDestination{};
static_assert(kNodeZeroDestination != kBroadcastDestination);
static_assert(kMaximumNodeDestination != kBroadcastDestination);
static_assert(std::get<UnicastDestination>(kNodeZeroDestination).node_id ==
              NodeId{0});
static_assert(std::holds_alternative<BroadcastDestination>(
    kBroadcastDestination));

constexpr TransmissionTarget kNodeZeroTarget =
    UnicastTransmissionTarget{NodeId{0}};
constexpr TransmissionTarget kBroadcastTarget =
    BroadcastTransmissionTarget{};
static_assert(kNodeZeroTarget != kBroadcastTarget);
static_assert(std::get<UnicastTransmissionTarget>(kNodeZeroTarget).node_id ==
              NodeId{0});
static_assert(std::holds_alternative<BroadcastTransmissionTarget>(
    kBroadcastTarget));

auto main() -> int {
  const DigitalPacket broadcast_packet{
      PacketId{1},
      NodeId{0},
      BroadcastDestination{},
      std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}};

  const TxOpportunity opportunity{NodeId{0},
                                  SimTime::FromNanoseconds(100)};
  if(broadcast_packet.source_node_id != NodeId{0} ||
     opportunity.sender_node_id != NodeId{0}) {
    return EXIT_FAILURE;
  }

  const Transmission broadcast_transmission{
      TransmissionId{10},
      broadcast_packet.packet_id,
      opportunity.sender_node_id,
      BroadcastTransmissionTarget{},
      SimTime::FromNanoseconds(100),
      SimTime::FromNanoseconds(200)};

  const DigitalPacket multihop_packet{
      PacketId{2},
      NodeId{0},
      UnicastDestination{NodeId{9}},
      std::vector<std::byte>{std::byte{0x03}}};
  const std::array hops{
      Transmission{TransmissionId{20},
                   multihop_packet.packet_id,
                   NodeId{0},
                   UnicastTransmissionTarget{NodeId{3}},
                   SimTime::FromNanoseconds(300),
                   SimTime::FromNanoseconds(400)},
      Transmission{TransmissionId{21},
                   multihop_packet.packet_id,
                   NodeId{3},
                   UnicastTransmissionTarget{NodeId{6}},
                   SimTime::FromNanoseconds(500),
                   SimTime::FromNanoseconds(600)},
      Transmission{TransmissionId{22},
                   multihop_packet.packet_id,
                   NodeId{6},
                   UnicastTransmissionTarget{NodeId{9}},
                   SimTime::FromNanoseconds(700),
                   SimTime::FromNanoseconds(800)}};
  if(std::get<UnicastDestination>(multihop_packet.destination).node_id !=
         NodeId{9} ||
     std::get<UnicastTransmissionTarget>(hops[0].target).node_id !=
         NodeId{3} ||
     std::get<UnicastTransmissionTarget>(hops[1].target).node_id !=
         NodeId{6} ||
     std::get<UnicastTransmissionTarget>(hops[2].target).node_id !=
         NodeId{9} ||
     hops[0].packet_id != multihop_packet.packet_id ||
     hops[1].packet_id != multihop_packet.packet_id ||
     hops[2].packet_id != multihop_packet.packet_id ||
     hops[0].transmission_id == hops[1].transmission_id ||
     hops[1].transmission_id == hops[2].transmission_id ||
     hops[0].transmission_id == hops[2].transmission_id) {
    return EXIT_FAILURE;
  }

  const std::array receptions{
      Reception{ReceptionId{100},
                broadcast_transmission.transmission_id,
                NodeId{1},
                SimTime::FromNanoseconds(210)},
      Reception{ReceptionId{101},
                broadcast_transmission.transmission_id,
                NodeId{3},
                SimTime::FromNanoseconds(220)},
      Reception{ReceptionId{102},
                broadcast_transmission.transmission_id,
                NodeId{4},
                SimTime::FromNanoseconds(230)}};

  for(const auto& reception : receptions) {
    if(reception.transmission_id !=
       broadcast_transmission.transmission_id) {
      return EXIT_FAILURE;
    }
  }

  return receptions[0].reception_id != receptions[1].reception_id &&
                 receptions[1].reception_id != receptions[2].reception_id &&
                 receptions[0].reception_id != receptions[2].reception_id
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
