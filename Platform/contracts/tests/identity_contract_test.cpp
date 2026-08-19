#include <concepts>
#include <cstdlib>
#include <type_traits>

#include <ns3_factory/contracts/identity.hpp>

using ns3_factory::contracts::IdentityValue;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PacketId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::ReceptionId;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::TransmissionId;

template <typename Lhs, typename Rhs>
concept EqualityComparableWith = requires(Lhs lhs, Rhs rhs) {
  { lhs == rhs } -> std::same_as<bool>;
};

static_assert(std::is_same_v<NodeId::value_type, std::uint64_t>);
static_assert(std::totally_ordered<NodeId>);
static_assert(std::totally_ordered<PacketId>);

static_assert(!std::is_default_constructible_v<NodeId>);
static_assert(!std::is_convertible_v<IdentityValue, NodeId>);
static_assert(!std::is_constructible_v<NodeId, PacketId>);
static_assert(!std::is_constructible_v<PacketId, TransmissionId>);
static_assert(!std::is_constructible_v<TransmissionId, ReceptionId>);
static_assert(!EqualityComparableWith<PacketId, TransmissionId>);
static_assert(!EqualityComparableWith<TransmissionId, ReceptionId>);

static_assert(NodeId{0}.value() == 0);
static_assert(NodeId{1} > NodeId{0});
static_assert(PacketId{7} == PacketId{7});
static_assert(TransmissionId{7}.value() == ReceptionId{7}.value());
static_assert(PlanningCycleId{0}.value() == 0);
static_assert(SnapshotVersion{0}.value() == 0);

int main() {
  const NodeId first{0};
  const NodeId second{1};
  return first < second ? EXIT_SUCCESS : EXIT_FAILURE;
}
