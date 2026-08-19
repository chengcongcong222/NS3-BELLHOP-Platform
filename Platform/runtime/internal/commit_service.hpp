#pragma once

#include <limits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/delta.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/state.hpp>

#include "internal/world_state_store.hpp"

namespace ns3_factory::runtime::internal {

class CommitService final {
 public:
  explicit CommitService(WorldStateStore& store) noexcept : store_(store) {}

  [[nodiscard]] auto CommitCycle(
      contracts::SnapshotVersion expected_version,
      const contracts::DeltaSet& delta) -> contracts::Status;

 private:
  [[nodiscard]] static auto ValidateFullReplacement(
      const contracts::WorldSnapshot& current_snapshot,
      const contracts::DeltaSet& delta) -> contracts::Status;

  WorldStateStore& store_;
};

inline auto CommitService::CommitCycle(
    contracts::SnapshotVersion expected_version,
    const contracts::DeltaSet& delta) -> contracts::Status {
  const auto& current = store_.current_snapshot();
  if(expected_version != current.version()) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "CommitCycle expected_version does not match the current snapshot"});
  }
  if(delta.base_version != expected_version) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "CommitCycle delta base_version does not match expected_version"});
  }
  if(delta.effective_at < current.committed_at()) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "CommitCycle delta effective_at precedes the current snapshot"});
  }

  constexpr auto kMaxVersion =
      std::numeric_limits<contracts::SnapshotVersion::value_type>::max();
  if(current.version().value() == kMaxVersion) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kOverflow,
        "CommitCycle cannot increment the maximum SnapshotVersion"});
  }

  const auto last_cycle = store_.last_committed_cycle_id();
  if(last_cycle && delta.cycle_id == *last_cycle) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kAlreadyExists,
        "CommitCycle rejected a duplicate PlanningCycleId"});
  }
  if(last_cycle && delta.cycle_id < *last_cycle) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "CommitCycle PlanningCycleId regressed"});
  }

  const auto replacement_status = ValidateFullReplacement(current, delta);
  if(!replacement_status) {
    return replacement_status;
  }

  std::vector<contracts::NodeCommittedState> candidate_nodes;
  candidate_nodes.reserve(delta.node_replacements.size());
  for(const auto& replacement : delta.node_replacements) {
    candidate_nodes.push_back(replacement.state);
  }

  const auto next_version =
      contracts::SnapshotVersion{current.version().value() + 1};
  auto candidate = contracts::WorldSnapshot::Create(
      next_version, delta.effective_at, std::move(candidate_nodes));
  if(!candidate) {
    return std::unexpected(candidate.error());
  }

  store_.CommitSnapshot(std::move(*candidate), delta.cycle_id);
  return {};
}

inline auto CommitService::ValidateFullReplacement(
    const contracts::WorldSnapshot& current_snapshot,
    const contracts::DeltaSet& delta) -> contracts::Status {
  const auto& replacements = delta.node_replacements;
  for(std::size_t index = 1; index < replacements.size(); ++index) {
    const auto previous_id = replacements[index - 1].state.node_id;
    const auto current_id = replacements[index].state.node_id;
    if(previous_id == current_id) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kAlreadyExists,
          "CommitCycle delta contains a duplicate replacement NodeId"});
    }
    if(previous_id > current_id) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "CommitCycle replacements are not in canonical NodeId order"});
    }
  }

  const auto current_nodes = current_snapshot.nodes();
  if(replacements.size() < current_nodes.size()) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "CommitCycle delta is missing an existing NodeId"});
  }
  if(replacements.size() > current_nodes.size()) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "CommitCycle delta contains an unknown NodeId"});
  }

  for(std::size_t index = 0; index < current_nodes.size(); ++index) {
    const auto replacement_id = replacements[index].state.node_id;
    const auto current_id = current_nodes[index].node_id;
    if(replacement_id < current_id) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "CommitCycle delta contains an unknown NodeId"});
    }
    if(replacement_id > current_id) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kFailedPrecondition,
          "CommitCycle delta is missing an existing NodeId"});
    }
  }

  return {};
}

}  // namespace ns3_factory::runtime::internal
