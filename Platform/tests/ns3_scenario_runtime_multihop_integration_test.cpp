#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <variant>
#include <vector>

#include "scenario_runtime_test_support.hpp"

using namespace ns3_factory::assembly::internal;
using namespace ns3_factory::assembly::test;

namespace {

auto TestProductionThreeCycleMultihop() -> bool {
  auto fixture = RuntimeFixture::Create(
      PlanningCycleId{0}, {NodeId{0}, NodeId{1}, NodeId{2}});
  const auto packet = TestPacket();
  if(!fixture || !fixture->Enqueue(packet)) return false;

  const auto run = fixture->runtime.RunCycles(3);
  const auto now = fixture->gateway.PlatformNow();
  if(!run || fixture->runtime.state() != ScenarioRuntimeState::kCompleted ||
     !now || *now != SimTime::Zero() ||
     fixture->planner.build_count != 3 ||
     fixture->estimator.observed_at.size() != 36 ||
     fixture->tx_phy.audit.size() != 3 ||
     fixture->channel.receiver_audit.size() != 9 ||
     fixture->noise.count != 9 || fixture->rx_phy.count != 9 ||
     fixture->world.current_snapshot().version() != SnapshotVersion{3} ||
     fixture->world.current_snapshot().committed_at() != Seconds(30) ||
     fixture->world.last_committed_cycle_id() != PlanningCycleId{2} ||
     !fixture->QueueHasOnly(std::nullopt, packet) ||
     fixture->deliveries.size() != 1) {
    return false;
  }

  const std::vector<PlanningCycleId> expected_cycles{
      PlanningCycleId{0}, PlanningCycleId{1}, PlanningCycleId{2}};
  const std::vector<SnapshotVersion> expected_versions{
      SnapshotVersion{0}, SnapshotVersion{1}, SnapshotVersion{2}};
  const std::vector<SimTime> expected_boundaries{
      Seconds(0), Seconds(10), Seconds(20)};
  if(fixture->planner.cycle_ids != expected_cycles ||
     fixture->planner.base_versions != expected_versions ||
     fixture->planner.kernel_times != expected_boundaries ||
     fixture->planner.timings.size() != 3) {
    return false;
  }
  for(std::size_t cycle = 0; cycle < 3; ++cycle) {
    if(fixture->planner.timings[cycle].starts_at() !=
           expected_boundaries[cycle] ||
       fixture->planner.timings[cycle].closes_at() !=
           Seconds(static_cast<std::int64_t>(cycle + 1) * 10)) {
      return false;
    }
    const auto begin = fixture->estimator.observed_at.begin() +
                       static_cast<std::ptrdiff_t>(cycle * 12);
    if(!std::all_of(begin,
                    begin + 12,
                    [&](SimTime time) {
                      return time == expected_boundaries[cycle];
                    })) {
      return false;
    }
  }

  const std::vector<NodeId> senders{NodeId{0}, NodeId{1}, NodeId{2}};
  const std::vector<NodeId> targets{NodeId{1}, NodeId{2}, NodeId{3}};
  for(std::size_t hop = 0; hop < 3; ++hop) {
    const auto& audit = fixture->tx_phy.audit[hop];
    const auto* target =
        std::get_if<UnicastTransmissionTarget>(&audit.request.target);
    if(audit.packet != packet || target == nullptr ||
       audit.request.transmission_id != TransmissionId{100 + hop} ||
       audit.request.sender_node_id != senders[hop] ||
       target->node_id != targets[hop] ||
       audit.request.started_at != expected_boundaries[hop]) {
      return false;
    }
  }

  const auto& delivery = fixture->deliveries.deliveries().front();
  const auto& previous = fixture->runtime.previous_connectivity();
  return delivery.receiver_node_id == NodeId{3} &&
         delivery.packet == packet && previous &&
         previous->edges().size() == 3 &&
         previous->HasEdge(NodeId{0}, NodeId{1}) &&
         previous->HasEdge(NodeId{1}, NodeId{2}) &&
         previous->HasEdge(NodeId{2}, NodeId{3});
}

}  // namespace

auto main() -> int {
  return TestProductionThreeCycleMultihop() ? EXIT_SUCCESS : EXIT_FAILURE;
}
