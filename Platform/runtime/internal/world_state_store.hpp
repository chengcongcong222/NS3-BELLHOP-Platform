#pragma once

#include <optional>
#include <type_traits>
#include <utility>

#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/state.hpp>

namespace ns3_factory::runtime::internal {

class CommitService;

class WorldStateStore final {
 public:
  explicit WorldStateStore(contracts::WorldSnapshot initial_snapshot)
      : current_snapshot_(std::move(initial_snapshot)) {}

  WorldStateStore(const WorldStateStore&) = delete;
  auto operator=(const WorldStateStore&) -> WorldStateStore& = delete;
  WorldStateStore(WorldStateStore&&) = delete;
  auto operator=(WorldStateStore&&) -> WorldStateStore& = delete;

  // The returned reference remains valid until the next successful
  // CommitService::CommitCycle call for this store.
  [[nodiscard]] auto current_snapshot() const noexcept
      -> const contracts::WorldSnapshot& {
    return current_snapshot_;
  }

  [[nodiscard]] auto last_committed_cycle_id() const noexcept
      -> std::optional<contracts::PlanningCycleId> {
    return last_committed_cycle_id_;
  }

 private:
  friend class CommitService;

  auto CommitSnapshot(contracts::WorldSnapshot next_snapshot,
                      contracts::PlanningCycleId cycle_id) noexcept -> void {
    static_assert(std::is_nothrow_move_assignable_v<
                  contracts::WorldSnapshot>);
    static_assert(std::is_nothrow_assignable_v<
                  std::optional<contracts::PlanningCycleId>&,
                  contracts::PlanningCycleId>);

    current_snapshot_ = std::move(next_snapshot);
    last_committed_cycle_id_ = cycle_id;
  }

  contracts::WorldSnapshot current_snapshot_;
  std::optional<contracts::PlanningCycleId> last_committed_cycle_id_;
};

}  // namespace ns3_factory::runtime::internal
