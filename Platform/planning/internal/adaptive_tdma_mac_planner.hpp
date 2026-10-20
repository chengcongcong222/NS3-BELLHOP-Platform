#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "internal/adaptive_tdma_policy.hpp"
#include "internal/mac_planner.hpp"

namespace ns3_factory::planning::internal {

// Queue-demand round robin. Every active sender is visited before a sender
// receives another slot; canonical ties rotate with the formal cycle identity.
// Slot widths are derived from declared physical bounds, never user slots.
class AdaptiveTdmaMacPlanner final : public IMacPlanner {
 public:
  explicit AdaptiveTdmaMacPlanner(AdaptiveTdmaPolicy policy) : policy_(policy) {}

  auto Build(const contracts::WorldSnapshot&,
             const contracts::StructureSnapshot&) const
      -> contracts::Result<MacPlanningResult> override {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "Adaptive TDMA requires explicit cycle-boundary planning input"});
  }

  auto Build(const contracts::WorldSnapshot& world,
             const contracts::StructureSnapshot& structure,
             const contracts::PlanningInput& input) const
      -> contracts::Result<MacPlanningResult> override {
    using namespace contracts;
    const auto valid = ValidatePlanningInput(input, world, structure);
    if(!valid) return std::unexpected(valid.error());
    const auto close = CheckedAdd(world.committed_at(), policy_.frame_duration());
    if(!close) return std::unexpected(Error{ErrorCode::kOverflow,
        "Adaptive TDMA frame end overflows"});
    auto timing = CycleTiming::Create(structure.cycle_id(), world.version(),
                                     world.committed_at(), *close);
    if(!timing) return std::unexpected(timing.error());
    struct Demand {
      NodeId node_id;
      std::uint64_t remaining;
      SimDuration slot_width;
    };
    std::vector<Demand> active;
    for(const auto& row : input.demands()) {
      const auto count = row.queued_packet_count + row.ready_packet_count;
      if(count == 0) continue;
      const auto physical = CheckedAdd(row.maximum_packet_airtime,
                                      input.maximum_propagation_tail());
      if(!physical) return std::unexpected(Error{ErrorCode::kOverflow,
          "Adaptive TDMA physical occupancy overflows"});
      const auto width = CheckedAdd(*physical, policy_.safety_margin());
      if(!width) return std::unexpected(Error{ErrorCode::kOverflow,
          "Adaptive TDMA slot width overflows"});
      // Scheduling on an explicit device clock is not rounding channel samples.
      // Keep the default 1 ns grid identical to the historical planner.
      const auto quantum=input.emission_quantum().nanoseconds();
      const auto remainder=width->nanoseconds()%quantum;
      const auto aligned=CheckedAdd(*width,SimDuration::FromNanoseconds(remainder ? quantum-remainder : 0));
      if(!aligned)return std::unexpected(Error{ErrorCode::kOverflow,"Aligned TDMA slot width overflows"});
      active.push_back({row.node_id, count, *aligned});
    }
    std::vector<TxOpportunity> opportunities;
    auto slot_start = timing->starts_at();
    if(!active.empty()) {
      // Anchor to the complete fixed node universe so changes in the active
      // set cannot continually shift a backlogged node away from its turn.
      const auto all_demands = input.demands();
      const auto anchor = all_demands[static_cast<std::size_t>(
          input.cycle_id().value() % all_demands.size())].node_id;
      const auto first = std::lower_bound(active.begin(), active.end(), anchor,
          [](const Demand& row, NodeId id) { return row.node_id < id; });
      auto cursor = first == active.end() ? std::size_t{0}
          : static_cast<std::size_t>(first - active.begin());
      for(;;) {
        bool allocated = false;
        for(std::size_t step = 0; step < active.size(); ++step) {
          const auto index = (cursor + step) % active.size();
          auto& row = active[index];
          if(row.remaining == 0) continue;
          const auto remaining_time = CheckedSubtract(timing->closes_at(), slot_start);
          if(!remaining_time) return std::unexpected(Error{ErrorCode::kOverflow,
              "Adaptive TDMA remaining frame duration overflows"});
          if(row.slot_width > *remaining_time) continue;
          const auto next = CheckedAdd(slot_start, row.slot_width);
          if(!next) return std::unexpected(Error{ErrorCode::kOverflow,
              "Adaptive TDMA slot end overflows"});
          opportunities.push_back({row.node_id, slot_start});
          slot_start = *next;
          --row.remaining;
          cursor = (index + 1) % active.size();
          allocated = true;
          break;
        }
        if(!allocated) break;
      }
    }
    return MacPlanningResult::Create(*timing, std::move(opportunities));
  }

 private:
  AdaptiveTdmaPolicy policy_;
};

}  // namespace ns3_factory::planning::internal
