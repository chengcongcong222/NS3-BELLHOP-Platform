#include <cstdlib>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/protocol_cycle_plan.hpp>

using ns3_factory::contracts::CycleTiming;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::ProtocolCyclePlan;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::TxOpportunity;

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

static_assert(std::is_same_v<
              decltype(std::declval<TxOpportunity>().sender_node_id),
              NodeId>);
static_assert(std::is_same_v<
              decltype(std::declval<TxOpportunity>().eligible_at),
              SimTime>);

auto TestTimingValidation() -> bool {
  const auto valid = CycleTiming::Create(
      PlanningCycleId{0}, SnapshotVersion{0}, At(0), At(10));
  const auto equal = CycleTiming::Create(
      PlanningCycleId{0}, SnapshotVersion{0}, At(10), At(10));
  const auto reversed = CycleTiming::Create(
      PlanningCycleId{0}, SnapshotVersion{0}, At(11), At(10));

  return valid && valid->cycle_id() == PlanningCycleId{0} &&
         valid->base_snapshot_version() == SnapshotVersion{0} &&
         valid->starts_at() == At(0) && valid->closes_at() == At(10) &&
         !equal && equal.error().code == ErrorCode::kInvalidArgument &&
         !reversed &&
         reversed.error().code == ErrorCode::kInvalidArgument;
}

auto TestCanonicalization() -> bool {
  const auto timing = CycleTiming::Create(
      PlanningCycleId{7}, SnapshotVersion{9}, At(0), At(100));
  if(!timing) {
    return false;
  }

  const std::vector<TxOpportunity> first_input{
      TxOpportunity{NodeId{9}, At(50)},
      TxOpportunity{NodeId{3}, At(20)},
      TxOpportunity{NodeId{0}, At(20)},
      TxOpportunity{NodeId{1}, At(80)}};
  const std::vector<TxOpportunity> second_input{
      first_input[2], first_input[3], first_input[0], first_input[1]};
  const auto first = ProtocolCyclePlan::Create(*timing, first_input);
  const auto second = ProtocolCyclePlan::Create(*timing, second_input);
  if(!first || !second || *first != *second) {
    return false;
  }

  const auto opportunities = first->mac_plan().tx_opportunities();
  return opportunities.size() == 4 &&
         opportunities[0] == TxOpportunity{NodeId{0}, At(20)} &&
         opportunities[1] == TxOpportunity{NodeId{3}, At(20)} &&
         opportunities[2] == TxOpportunity{NodeId{9}, At(50)} &&
         opportunities[3] == TxOpportunity{NodeId{1}, At(80)};
}

auto TestOpportunityRangeAndDuplicateValidation() -> bool {
  const auto timing = CycleTiming::Create(
      PlanningCycleId{1}, SnapshotVersion{2}, At(10), At(20));
  if(!timing) {
    return false;
  }

  const auto at_start = ProtocolCyclePlan::Create(
      *timing, {TxOpportunity{NodeId{0}, At(10)}});
  const auto before = ProtocolCyclePlan::Create(
      *timing, {TxOpportunity{NodeId{0}, At(9)}});
  const auto at_close = ProtocolCyclePlan::Create(
      *timing, {TxOpportunity{NodeId{0}, At(20)}});
  const auto after = ProtocolCyclePlan::Create(
      *timing, {TxOpportunity{NodeId{0}, At(21)}});
  const auto duplicate = ProtocolCyclePlan::Create(
      *timing,
      {TxOpportunity{NodeId{0}, At(15)},
       TxOpportunity{NodeId{0}, At(15)}});
  const auto same_time_different_sender = ProtocolCyclePlan::Create(
      *timing,
      {TxOpportunity{NodeId{1}, At(15)},
       TxOpportunity{NodeId{0}, At(15)}});
  const auto empty = ProtocolCyclePlan::Create(*timing, {});

  return at_start && !before &&
         before.error().code == ErrorCode::kOutOfRange && !at_close &&
         at_close.error().code == ErrorCode::kOutOfRange && !after &&
         after.error().code == ErrorCode::kOutOfRange && !duplicate &&
         duplicate.error().code == ErrorCode::kAlreadyExists &&
         same_time_different_sender && empty;
}

}  // namespace

auto main() -> int {
  if(!TestTimingValidation() || !TestCanonicalization() ||
     !TestOpportunityRangeAndDuplicateValidation()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
