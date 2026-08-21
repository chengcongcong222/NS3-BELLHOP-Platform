#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/routing.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/topology.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>
#include <ns3_factory/contracts/tx_opportunity.hpp>

#include "internal/fifo_packet_selector.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/packet_selector.hpp"
#include "internal/transmission_target_resolver.hpp"
#include "internal/tx_preparation.hpp"

using ns3_factory::contracts::BroadcastDestination;
using ns3_factory::contracts::BroadcastTransmissionTarget;
using ns3_factory::contracts::ConnectivityGraph;
using ns3_factory::contracts::CycleTiming;
using ns3_factory::contracts::DigitalPacket;
using ns3_factory::contracts::DirectedLink;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::LogicalLink;
using ns3_factory::contracts::LogicalTopology;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PacketDestination;
using ns3_factory::contracts::PacketId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::ProtocolCyclePlan;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::RoleTable;
using ns3_factory::contracts::RouteEntry;
using ns3_factory::contracts::RoutingPlan;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::StructureSnapshot;
using ns3_factory::contracts::TransmissionTarget;
using ns3_factory::contracts::TxOpportunity;
using ns3_factory::contracts::UnicastDestination;
using ns3_factory::contracts::UnicastTransmissionTarget;
using ns3_factory::runtime::internal::FifoPacketSelector;
using ns3_factory::runtime::internal::IPacketSelector;
using ns3_factory::runtime::internal::NoPacketTxPreparation;
using ns3_factory::runtime::internal::NoRouteResolution;
using ns3_factory::runtime::internal::NoRouteTxPreparation;
using ns3_factory::runtime::internal::PacketQueueStore;
using ns3_factory::runtime::internal::ReadyTxPreparation;
using ns3_factory::runtime::internal::ResolvedTransmissionTarget;
using ns3_factory::runtime::internal::SelectedPacket;
using ns3_factory::runtime::internal::TransmissionTargetResolver;
using ns3_factory::runtime::internal::TxPreparationService;

static_assert(std::derived_from<FifoPacketSelector, IPacketSelector>);
static_assert(std::has_virtual_destructor_v<IPacketSelector>);
static_assert(std::copy_constructible<SelectedPacket>);
static_assert(std::copy_constructible<ReadyTxPreparation>);

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

auto MakePacket(std::uint64_t packet_id,
                std::uint64_t source_node_id,
                PacketDestination destination) -> DigitalPacket {
  return DigitalPacket{
      PacketId{packet_id},
      NodeId{source_node_id},
      std::move(destination),
      {std::byte{static_cast<unsigned char>(packet_id)}}};
}

auto MakeRoutingPlan(std::vector<RouteEntry> entries)
    -> Result<RoutingPlan> {
  const std::vector<NodeId> node_ids{
      NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}};
  std::vector<DirectedLink> directed_links;
  std::vector<LogicalLink> logical_links;
  directed_links.reserve(entries.size());
  logical_links.reserve(entries.size());
  for(const auto& entry : entries) {
    directed_links.push_back(
        DirectedLink{entry.forwarding_node_id, entry.next_hop_node_id});
    logical_links.push_back(
        LogicalLink{entry.forwarding_node_id, entry.next_hop_node_id});
  }

  auto connectivity = ConnectivityGraph::Create(
      std::move(directed_links), node_ids);
  if(!connectivity) {
    return std::unexpected(connectivity.error());
  }
  auto topology = LogicalTopology::Create(
      std::move(logical_links), node_ids, *connectivity);
  if(!topology) {
    return std::unexpected(topology.error());
  }
  auto roles = RoleTable::Create({}, node_ids);
  if(!roles) {
    return std::unexpected(roles.error());
  }
  auto structure = StructureSnapshot::Create(
      PlanningCycleId{7},
      SnapshotVersion{5},
      std::move(*roles),
      std::move(*connectivity),
      std::move(*topology));
  if(!structure) {
    return std::unexpected(structure.error());
  }
  return RoutingPlan::Create(std::move(entries), *structure);
}

auto MakeStore() -> Result<PacketQueueStore> {
  return PacketQueueStore::Create(
      {NodeId{0}, NodeId{1}, NodeId{2}, NodeId{3}});
}

auto TestNoPacketIsNormalAndUnknownSenderIsError() -> bool {
  const auto store = MakeStore();
  if(!store) {
    return false;
  }
  const FifoPacketSelector selector;
  const TxPreparationService preparation{selector};
  const auto no_packet = preparation.Prepare(
      TxOpportunity{NodeId{0}, At(10)}, *store, std::nullopt);
  const auto unknown_sender = preparation.Prepare(
      TxOpportunity{NodeId{9}, At(10)}, *store, std::nullopt);
  return no_packet &&
         std::holds_alternative<NoPacketTxPreparation>(*no_packet) &&
         !unknown_sender &&
         unknown_sender.error().code == ErrorCode::kNotFound;
}

auto TestBroadcastScheduleOnlyAndReadyOwnership() -> bool {
  auto store = MakeStore();
  const auto timing = CycleTiming::Create(
      PlanningCycleId{7}, SnapshotVersion{5}, At(10), At(20));
  const auto schedule_only = timing
                                 ? ProtocolCyclePlan::Create(
                                       *timing,
                                       {TxOpportunity{NodeId{0}, At(10)}})
                                 : Result<ProtocolCyclePlan>{
                                       std::unexpected(timing.error())};
  if(!store || !timing || !schedule_only) {
    return false;
  }
  const auto enqueue = store->Enqueue(
      NodeId{0}, MakePacket(1, 0, BroadcastDestination{}));
  if(!enqueue || schedule_only->routing_plan()) {
    return false;
  }

  const FifoPacketSelector selector;
  const TxPreparationService preparation{selector};
  auto outcome = preparation.Prepare(
      TxOpportunity{NodeId{0}, At(10)},
      *store,
      schedule_only->routing_plan());
  const auto size_before_consume = store->size(NodeId{0});
  if(!outcome ||
     !std::holds_alternative<ReadyTxPreparation>(*outcome) ||
     !size_before_consume || *size_before_consume != 1) {
    return false;
  }

  const auto consume = store->ConsumeFront(NodeId{0}, PacketId{1});
  const auto& ready = std::get<ReadyTxPreparation>(*outcome);
  const auto size_after_consume = store->size(NodeId{0});
  return consume && size_after_consume && *size_after_consume == 0 &&
         ready.selected_packet.queue_owner == NodeId{0} &&
         ready.selected_packet.packet.packet_id == PacketId{1} &&
         std::holds_alternative<BroadcastTransmissionTarget>(ready.target);
}

auto TestUnicastNextHopAndDirectTarget() -> bool {
  auto routed_store = MakeStore();
  auto direct_store = MakeStore();
  const auto routed = MakeRoutingPlan(
      {RouteEntry{NodeId{0}, NodeId{3}, NodeId{1}}});
  const auto direct = MakeRoutingPlan(
      {RouteEntry{NodeId{0}, NodeId{3}, NodeId{3}}});
  if(!routed_store || !direct_store || !routed || !direct) {
    return false;
  }
  const auto routed_enqueue = routed_store->Enqueue(
      NodeId{0}, MakePacket(2, 0, UnicastDestination{NodeId{3}}));
  const auto direct_enqueue = direct_store->Enqueue(
      NodeId{0}, MakePacket(3, 0, UnicastDestination{NodeId{3}}));
  if(!routed_enqueue || !direct_enqueue) {
    return false;
  }

  const FifoPacketSelector selector;
  const TxPreparationService preparation{selector};
  const auto routed_outcome = preparation.Prepare(
      TxOpportunity{NodeId{0}, At(10)}, *routed_store, *routed);
  const auto direct_outcome = preparation.Prepare(
      TxOpportunity{NodeId{0}, At(10)}, *direct_store, *direct);
  if(!routed_outcome || !direct_outcome ||
     !std::holds_alternative<ReadyTxPreparation>(*routed_outcome) ||
     !std::holds_alternative<ReadyTxPreparation>(*direct_outcome)) {
    return false;
  }

  const auto& routed_target =
      std::get<ReadyTxPreparation>(*routed_outcome).target;
  const auto& direct_target =
      std::get<ReadyTxPreparation>(*direct_outcome).target;
  return std::get<UnicastTransmissionTarget>(routed_target).node_id ==
             NodeId{1} &&
         std::get<UnicastTransmissionTarget>(direct_target).node_id ==
             NodeId{3} &&
         *routed_store->size(NodeId{0}) == 1 &&
         *direct_store->size(NodeId{0}) == 1;
}

auto TestAbsentRoutingAndNormalNoRoute() -> bool {
  auto absent_store = MakeStore();
  auto no_route_store = MakeStore();
  const auto route_for_second_packet = MakeRoutingPlan(
      {RouteEntry{NodeId{0}, NodeId{2}, NodeId{2}}});
  if(!absent_store || !no_route_store || !route_for_second_packet) {
    return false;
  }
  const auto absent_enqueue = absent_store->Enqueue(
      NodeId{0}, MakePacket(4, 0, UnicastDestination{NodeId{3}}));
  const auto first_enqueue = no_route_store->Enqueue(
      NodeId{0}, MakePacket(5, 0, UnicastDestination{NodeId{3}}));
  const auto second_enqueue = no_route_store->Enqueue(
      NodeId{0}, MakePacket(6, 0, UnicastDestination{NodeId{2}}));
  if(!absent_enqueue || !first_enqueue || !second_enqueue) {
    return false;
  }

  const FifoPacketSelector selector;
  const TxPreparationService preparation{selector};
  const auto absent = preparation.Prepare(
      TxOpportunity{NodeId{0}, At(10)}, *absent_store, std::nullopt);
  const auto no_route = preparation.Prepare(
      TxOpportunity{NodeId{0}, At(10)},
      *no_route_store,
      *route_for_second_packet);
  const auto front_after_no_route =
      no_route_store->PeekFront(NodeId{0});
  const auto size_after_no_route = no_route_store->size(NodeId{0});
  return !absent &&
         absent.error().code == ErrorCode::kFailedPrecondition &&
         *absent_store->size(NodeId{0}) == 1 && no_route &&
         std::holds_alternative<NoRouteTxPreparation>(*no_route) &&
         front_after_no_route && *front_after_no_route &&
         (*front_after_no_route)->packet_id == PacketId{5} &&
         size_after_no_route && *size_after_no_route == 2;
}

auto TestRelaySourceIsIndependentFromForwardingSender() -> bool {
  auto store = MakeStore();
  const auto routing = MakeRoutingPlan(
      {RouteEntry{NodeId{1}, NodeId{3}, NodeId{2}}});
  if(!store || !routing) {
    return false;
  }
  const auto enqueue = store->Enqueue(
      NodeId{1}, MakePacket(7, 0, UnicastDestination{NodeId{3}}));
  if(!enqueue) {
    return false;
  }

  const FifoPacketSelector selector;
  const TxPreparationService preparation{selector};
  const auto outcome = preparation.Prepare(
      TxOpportunity{NodeId{1}, At(10)}, *store, *routing);
  if(!outcome ||
     !std::holds_alternative<ReadyTxPreparation>(*outcome)) {
    return false;
  }
  const auto& ready = std::get<ReadyTxPreparation>(*outcome);
  return ready.selected_packet.queue_owner == NodeId{1} &&
         ready.selected_packet.packet.source_node_id == NodeId{0} &&
         std::get<UnicastTransmissionTarget>(ready.target).node_id ==
             NodeId{2} &&
         *store->size(NodeId{1}) == 1;
}

auto TestResolverContractViolations() -> bool {
  const TransmissionTargetResolver resolver;
  const SelectedPacket wrong_owner{
      NodeId{2},
      MakePacket(8, 0, BroadcastDestination{})};
  const auto owner_mismatch = resolver.Resolve(
      TxOpportunity{NodeId{1}, At(10)}, wrong_owner, std::nullopt);

  const SelectedPacket local_destination{
      NodeId{2},
      MakePacket(9, 0, UnicastDestination{NodeId{2}})};
  const auto local = resolver.Resolve(
      TxOpportunity{NodeId{2}, At(10)}, local_destination, std::nullopt);
  return !owner_mismatch &&
         owner_mismatch.error().code == ErrorCode::kFailedPrecondition &&
         !local && local.error().code == ErrorCode::kFailedPrecondition;
}

auto TestNodeZeroDestinationAndNextHop() -> bool {
  auto destination_zero_store = MakeStore();
  auto next_hop_zero_store = MakeStore();
  const auto destination_zero_route = MakeRoutingPlan(
      {RouteEntry{NodeId{1}, NodeId{0}, NodeId{0}}});
  const auto next_hop_zero_route = MakeRoutingPlan(
      {RouteEntry{NodeId{1}, NodeId{3}, NodeId{0}}});
  if(!destination_zero_store || !next_hop_zero_store ||
     !destination_zero_route || !next_hop_zero_route) {
    return false;
  }
  const auto destination_enqueue = destination_zero_store->Enqueue(
      NodeId{1}, MakePacket(10, 1, UnicastDestination{NodeId{0}}));
  const auto next_hop_enqueue = next_hop_zero_store->Enqueue(
      NodeId{1}, MakePacket(11, 1, UnicastDestination{NodeId{3}}));
  if(!destination_enqueue || !next_hop_enqueue) {
    return false;
  }

  const FifoPacketSelector selector;
  const TxPreparationService preparation{selector};
  const auto destination_outcome = preparation.Prepare(
      TxOpportunity{NodeId{1}, At(10)},
      *destination_zero_store,
      *destination_zero_route);
  const auto next_hop_outcome = preparation.Prepare(
      TxOpportunity{NodeId{1}, At(10)},
      *next_hop_zero_store,
      *next_hop_zero_route);
  if(!destination_outcome || !next_hop_outcome ||
     !std::holds_alternative<ReadyTxPreparation>(*destination_outcome) ||
     !std::holds_alternative<ReadyTxPreparation>(*next_hop_outcome)) {
    return false;
  }

  return std::get<UnicastTransmissionTarget>(
             std::get<ReadyTxPreparation>(*destination_outcome).target)
                 .node_id == NodeId{0} &&
         std::get<UnicastTransmissionTarget>(
             std::get<ReadyTxPreparation>(*next_hop_outcome).target)
                 .node_id == NodeId{0};
}

auto TestResolverNormalNoRouteType() -> bool {
  const auto routing = MakeRoutingPlan(
      {RouteEntry{NodeId{0}, NodeId{2}, NodeId{2}}});
  if(!routing) {
    return false;
  }
  const SelectedPacket selected{
      NodeId{0},
      MakePacket(12, 0, UnicastDestination{NodeId{3}})};
  const auto resolution = TransmissionTargetResolver{}.Resolve(
      TxOpportunity{NodeId{0}, At(10)}, selected, *routing);
  return resolution &&
         std::holds_alternative<NoRouteResolution>(*resolution);
}

}  // namespace

auto main() -> int {
  return TestNoPacketIsNormalAndUnknownSenderIsError() &&
                 TestBroadcastScheduleOnlyAndReadyOwnership() &&
                 TestUnicastNextHopAndDirectTarget() &&
                 TestAbsentRoutingAndNormalNoRoute() &&
                 TestRelaySourceIsIndependentFromForwardingSender() &&
                 TestResolverContractViolations() &&
                 TestNodeZeroDestinationAndNextHop() &&
                 TestResolverNormalNoRouteType()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
