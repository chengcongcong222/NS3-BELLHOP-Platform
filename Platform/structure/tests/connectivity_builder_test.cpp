#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/link_feasibility.hpp>
#include <ns3_factory/contracts/node_capability.hpp>
#include <ns3_factory/contracts/state.hpp>

#include "internal/connectivity_builder.hpp"
#include "internal/connectivity_decision_policy.hpp"

using ns3_factory::contracts::ConnectivityGraph;
using ns3_factory::contracts::DirectedLink;
using ns3_factory::contracts::DuplexMode;
using ns3_factory::contracts::Error;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::ILinkFeasibilityEstimator;
using ns3_factory::contracts::LinkFeasibilityEstimate;
using ns3_factory::contracts::LinkFeasibilityQuery;
using ns3_factory::contracts::MotionState;
using ns3_factory::contracts::NodeCapabilityProfile;
using ns3_factory::contracts::NodeCommittedState;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::Velocity3d;
using ns3_factory::contracts::WorldSnapshot;
using ns3_factory::structure::internal::ConnectivityBuilder;
using ns3_factory::structure::internal::ConnectivityDecisionPolicy;

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

constexpr auto MakeNode(std::uint64_t id,
                        Position3d position,
                        bool can_transmit = true,
                        bool can_receive = true) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{
          can_transmit, can_receive, DuplexMode::kHalfDuplex},
      MotionState{position, Velocity3d{0.0, 0.0, 0.0}}};
}

auto MakeSnapshot(std::vector<NodeCommittedState> nodes,
                  SimTime committed_at = At(100))
    -> Result<WorldSnapshot> {
  return WorldSnapshot::Create(
      SnapshotVersion{5}, committed_at, std::move(nodes));
}

auto MakePolicy(std::optional<double> max_range = std::nullopt)
    -> Result<ConnectivityDecisionPolicy> {
  return ConnectivityDecisionPolicy::Create(max_range, 0.8, 0.6);
}

struct ScoreEntry final {
  NodeId source_node_id;
  NodeId target_node_id;
  double score;
};

enum class EstimateMutation {
  kNone,
  kWrongSource,
  kWrongTarget,
  kWrongTime,
};

class RecordingEstimator final : public ILinkFeasibilityEstimator {
 public:
  explicit RecordingEstimator(
      double default_score,
      std::vector<ScoreEntry> scores = {},
      std::optional<std::size_t> fail_call = std::nullopt,
      EstimateMutation mutation = EstimateMutation::kNone) noexcept
      : default_score_(default_score),
        scores_(std::move(scores)),
        fail_call_(fail_call),
        mutation_(mutation) {}

  [[nodiscard]] auto Estimate(const LinkFeasibilityQuery& query) const
      -> Result<LinkFeasibilityEstimate> override {
    queries_.push_back(query);
    if(fail_call_ && queries_.size() == *fail_call_) {
      return std::unexpected(
          Error{ErrorCode::kInternal, "test estimator failure"});
    }

    auto source = query.source_node_id();
    auto target = query.target_node_id();
    auto observed_at = query.observed_at();
    if(mutation_ == EstimateMutation::kWrongSource) {
      source = NodeId{999};
    } else if(mutation_ == EstimateMutation::kWrongTarget) {
      target = NodeId{999};
    } else if(mutation_ == EstimateMutation::kWrongTime) {
      observed_at = At(query.observed_at().nanoseconds() + 1);
    }

    auto score = default_score_;
    const auto configured = std::find_if(
        scores_.begin(), scores_.end(), [&](const ScoreEntry& entry) {
          return entry.source_node_id == query.source_node_id() &&
                 entry.target_node_id == query.target_node_id();
        });
    if(configured != scores_.end()) {
      score = configured->score;
    }
    return LinkFeasibilityEstimate::Create(
        source, target, observed_at, score);
  }

  [[nodiscard]] auto queries() const noexcept
      -> std::span<const LinkFeasibilityQuery> {
    return std::span<const LinkFeasibilityQuery>{queries_};
  }

 private:
  double default_score_;
  std::vector<ScoreEntry> scores_;
  std::optional<std::size_t> fail_call_;
  EstimateMutation mutation_;
  mutable std::vector<LinkFeasibilityQuery> queries_;
};

auto TestDeterministicEnumerationAndAsymmetry() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(2, Position3d{20.0, 0.0, 0.0}),
       MakeNode(0, Position3d{0.0, 0.0, 0.0}),
       MakeNode(1, Position3d{10.0, 0.0, 0.0})});
  const auto policy = MakePolicy();
  if(!snapshot || !policy) {
    return false;
  }

  const std::vector<ScoreEntry> scores{
      ScoreEntry{NodeId{0}, NodeId{1}, 0.9},
      ScoreEntry{NodeId{1}, NodeId{0}, 0.1}};
  RecordingEstimator first_estimator{0.0, scores};
  RecordingEstimator second_estimator{0.0, scores};
  const auto first =
      ConnectivityBuilder::Build(*snapshot, *policy, first_estimator);
  const auto second =
      ConnectivityBuilder::Build(*snapshot, *policy, second_estimator);
  const auto first_queries = first_estimator.queries();
  const auto second_queries = second_estimator.queries();
  const bool same_query_sequence =
      first_queries.size() == second_queries.size() &&
      std::equal(first_queries.begin(),
                 first_queries.end(),
                 second_queries.begin());
  if(!first || !second || *first != *second ||
     first_queries.size() != 6 || !same_query_sequence) {
    return false;
  }

  const std::vector<DirectedLink> expected_queries{
      DirectedLink{NodeId{0}, NodeId{1}},
      DirectedLink{NodeId{0}, NodeId{2}},
      DirectedLink{NodeId{1}, NodeId{0}},
      DirectedLink{NodeId{1}, NodeId{2}},
      DirectedLink{NodeId{2}, NodeId{0}},
      DirectedLink{NodeId{2}, NodeId{1}}};
  for(std::size_t index = 0; index < expected_queries.size(); ++index) {
    const auto& query = first_estimator.queries()[index];
    if(query.source_node_id() != expected_queries[index].source_node_id ||
       query.target_node_id() != expected_queries[index].target_node_id ||
       query.observed_at() != snapshot->committed_at()) {
      return false;
    }
  }

  return first->edges().size() == 1 &&
         first->HasEdge(NodeId{0}, NodeId{1}) &&
         !first->HasEdge(NodeId{1}, NodeId{0});
}

auto TestCapabilityGatePrecedesEstimatorAndHysteresis() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(0, Position3d{0.0, 0.0, 0.0}, false, true),
       MakeNode(1, Position3d{1.0, 0.0, 0.0}, true, true)});
  const auto policy = MakePolicy();
  const auto previous = ConnectivityGraph::Create(
      {DirectedLink{NodeId{0}, NodeId{1}}}, {NodeId{0}, NodeId{1}});
  if(!snapshot || !policy || !previous) {
    return false;
  }

  RecordingEstimator estimator{1.0};
  const auto graph = ConnectivityBuilder::Build(
      *snapshot,
      *policy,
      estimator,
      std::optional{std::cref(*previous)});
  return graph && estimator.queries().size() == 1 &&
         estimator.queries()[0].source_node_id() == NodeId{1} &&
         estimator.queries()[0].target_node_id() == NodeId{0} &&
         !graph->HasEdge(NodeId{0}, NodeId{1}) &&
         graph->HasEdge(NodeId{1}, NodeId{0});
}

auto TestCoarseRangeIsRejectOnly() -> bool {
  const auto outside_snapshot = MakeSnapshot(
      {MakeNode(0, Position3d{0.0, 0.0, 0.0}, true, false),
       MakeNode(1, Position3d{0.0, 0.0, 101.0}, false, true)});
  const auto boundary_snapshot = MakeSnapshot(
      {MakeNode(0, Position3d{0.0, 0.0, 0.0}, true, false),
       MakeNode(1, Position3d{60.0, 0.0, 80.0}, false, true)});
  const auto policy = MakePolicy(100.0);
  const auto previous = ConnectivityGraph::Create(
      {DirectedLink{NodeId{0}, NodeId{1}}}, {NodeId{0}, NodeId{1}});
  if(!outside_snapshot || !boundary_snapshot || !policy || !previous) {
    return false;
  }

  RecordingEstimator outside_estimator{1.0};
  const auto outside = ConnectivityBuilder::Build(
      *outside_snapshot,
      *policy,
      outside_estimator,
      std::optional{std::cref(*previous)});
  RecordingEstimator boundary_estimator{0.7};
  const auto boundary = ConnectivityBuilder::Build(
      *boundary_snapshot, *policy, boundary_estimator);

  return outside && outside->edges().empty() &&
         outside_estimator.queries().empty() && boundary &&
         boundary->edges().empty() &&
         boundary_estimator.queries().size() == 1;
}

auto BuildSingleEdge(double score,
                     const WorldSnapshot& snapshot,
                     const ConnectivityDecisionPolicy& policy,
                     const ConnectivityGraph* previous = nullptr)
    -> Result<ConnectivityGraph> {
  RecordingEstimator estimator{score};
  if(previous) {
    return ConnectivityBuilder::Build(
        snapshot,
        policy,
        estimator,
        std::optional{std::cref(*previous)});
  }
  return ConnectivityBuilder::Build(snapshot, policy, estimator);
}

auto TestInclusiveHysteresisBoundaries() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(0, Position3d{0.0, 0.0, 0.0}, true, false),
       MakeNode(1, Position3d{1.0, 0.0, 0.0}, false, true)});
  const auto policy = MakePolicy();
  const auto previous = ConnectivityGraph::Create(
      {DirectedLink{NodeId{0}, NodeId{1}}}, {NodeId{0}, NodeId{1}});
  if(!snapshot || !policy || !previous) {
    return false;
  }

  const auto new_below = BuildSingleEdge(0.70, *snapshot, *policy);
  const auto new_at = BuildSingleEdge(0.80, *snapshot, *policy);
  const auto existing_band =
      BuildSingleEdge(0.70, *snapshot, *policy, &*previous);
  const auto existing_at =
      BuildSingleEdge(0.60, *snapshot, *policy, &*previous);
  const auto existing_below =
      BuildSingleEdge(0.59, *snapshot, *policy, &*previous);

  return new_below && new_below->edges().empty() && new_at &&
         new_at->HasEdge(NodeId{0}, NodeId{1}) && existing_band &&
         existing_band->HasEdge(NodeId{0}, NodeId{1}) && existing_at &&
         existing_at->HasEdge(NodeId{0}, NodeId{1}) && existing_below &&
         existing_below->edges().empty();
}

auto TestProviderErrorIsAtomicAndRetryable() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(0, Position3d{0.0, 0.0, 0.0}),
       MakeNode(1, Position3d{1.0, 0.0, 0.0}),
       MakeNode(2, Position3d{2.0, 0.0, 0.0})});
  const auto policy = MakePolicy();
  if(!snapshot || !policy) {
    return false;
  }

  RecordingEstimator failing{1.0, {}, 3};
  const auto failed =
      ConnectivityBuilder::Build(*snapshot, *policy, failing);
  RecordingEstimator retry{1.0};
  const auto recovered =
      ConnectivityBuilder::Build(*snapshot, *policy, retry);

  return !failed && failed.error().code == ErrorCode::kInternal &&
         failing.queries().size() == 3 && recovered &&
         recovered->edges().size() == 6 && retry.queries().size() == 6;
}

auto TestEstimateProvenanceMismatch() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(0, Position3d{0.0, 0.0, 0.0}, true, false),
       MakeNode(1, Position3d{1.0, 0.0, 0.0}, false, true)});
  const auto policy = MakePolicy();
  if(!snapshot || !policy) {
    return false;
  }

  RecordingEstimator wrong_source{
      1.0, {}, std::nullopt, EstimateMutation::kWrongSource};
  RecordingEstimator wrong_target{
      1.0, {}, std::nullopt, EstimateMutation::kWrongTarget};
  RecordingEstimator wrong_time{
      1.0, {}, std::nullopt, EstimateMutation::kWrongTime};
  const auto source_result =
      ConnectivityBuilder::Build(*snapshot, *policy, wrong_source);
  const auto target_result =
      ConnectivityBuilder::Build(*snapshot, *policy, wrong_target);
  const auto time_result =
      ConnectivityBuilder::Build(*snapshot, *policy, wrong_time);

  return !source_result &&
         source_result.error().code == ErrorCode::kFailedPrecondition &&
         !target_result &&
         target_result.error().code == ErrorCode::kFailedPrecondition &&
         !time_result &&
         time_result.error().code == ErrorCode::kFailedPrecondition;
}

auto TestNonfiniteAndPolicyValidation() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(0, Position3d{0.0, 0.0, 0.0}, true, false),
       MakeNode(1, Position3d{1.0, 0.0, 0.0}, false, true)});
  const auto policy = MakePolicy();
  if(!snapshot || !policy) {
    return false;
  }

  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto infinity = std::numeric_limits<double>::infinity();
  RecordingEstimator nan_estimator{nan};
  RecordingEstimator positive_infinity{infinity};
  RecordingEstimator negative_infinity{-infinity};
  const auto nan_result =
      ConnectivityBuilder::Build(*snapshot, *policy, nan_estimator);
  const auto positive_result =
      ConnectivityBuilder::Build(*snapshot, *policy, positive_infinity);
  const auto negative_result =
      ConnectivityBuilder::Build(*snapshot, *policy, negative_infinity);

  const auto nan_enter =
      ConnectivityDecisionPolicy::Create(std::nullopt, nan, 0.6);
  const auto infinite_keep =
      ConnectivityDecisionPolicy::Create(std::nullopt, 0.8, infinity);
  const auto nan_range =
      ConnectivityDecisionPolicy::Create(nan, 0.8, 0.6);
  const auto zero_range =
      ConnectivityDecisionPolicy::Create(0.0, 0.8, 0.6);
  const auto negative_range =
      ConnectivityDecisionPolicy::Create(-1.0, 0.8, 0.6);
  const auto reversed_thresholds =
      ConnectivityDecisionPolicy::Create(std::nullopt, 0.5, 0.6);

  return !nan_result &&
         nan_result.error().code == ErrorCode::kInvalidArgument &&
         !positive_result &&
         positive_result.error().code == ErrorCode::kInvalidArgument &&
         !negative_result &&
         negative_result.error().code == ErrorCode::kInvalidArgument &&
         !nan_enter && !infinite_keep && !nan_range && !zero_range &&
         zero_range.error().code == ErrorCode::kOutOfRange &&
         !negative_range &&
         negative_range.error().code == ErrorCode::kOutOfRange &&
         !reversed_thresholds &&
         reversed_thresholds.error().code == ErrorCode::kInvalidArgument;
}

auto TestUniverseAndInvalidPositionValidation() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(0, Position3d{0.0, 0.0, 0.0}),
       MakeNode(1, Position3d{1.0, 0.0, 0.0})});
  const auto invalid_position_snapshot = MakeSnapshot(
      {MakeNode(0,
                Position3d{
                    std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0},
                true,
                false),
       MakeNode(1, Position3d{1.0, 0.0, 0.0}, false, true)});
  const auto previous = ConnectivityGraph::Create(
      {}, {NodeId{0}, NodeId{1}, NodeId{2}});
  const auto policy = MakePolicy();
  if(!snapshot || !invalid_position_snapshot || !previous || !policy) {
    return false;
  }

  RecordingEstimator universe_estimator{1.0};
  const auto mismatch = ConnectivityBuilder::Build(
      *snapshot,
      *policy,
      universe_estimator,
      std::optional{std::cref(*previous)});
  RecordingEstimator position_estimator{1.0};
  const auto invalid_position = ConnectivityBuilder::Build(
      *invalid_position_snapshot, *policy, position_estimator);

  return !mismatch &&
         mismatch.error().code == ErrorCode::kFailedPrecondition &&
         universe_estimator.queries().empty() && !invalid_position &&
         invalid_position.error().code == ErrorCode::kInvalidArgument &&
         position_estimator.queries().empty();
}

auto TestEmptyAndSingleNodeSnapshots() -> bool {
  const auto empty_snapshot = MakeSnapshot({});
  const auto single_snapshot = MakeSnapshot(
      {MakeNode(0, Position3d{0.0, 0.0, 0.0})});
  const auto policy = MakePolicy();
  if(!empty_snapshot || !single_snapshot || !policy) {
    return false;
  }

  RecordingEstimator empty_estimator{1.0};
  RecordingEstimator single_estimator{1.0};
  const auto empty = ConnectivityBuilder::Build(
      *empty_snapshot, *policy, empty_estimator);
  const auto single = ConnectivityBuilder::Build(
      *single_snapshot, *policy, single_estimator);

  return empty && empty->edges().empty() && empty->node_ids().empty() &&
         empty_estimator.queries().empty() && single &&
         single->edges().empty() && single->node_ids().size() == 1 &&
         single->node_ids()[0] == NodeId{0} &&
         single_estimator.queries().empty();
}

}  // namespace

auto main() -> int {
  if(!TestDeterministicEnumerationAndAsymmetry() ||
     !TestCapabilityGatePrecedesEstimatorAndHysteresis() ||
     !TestCoarseRangeIsRejectOnly() ||
     !TestInclusiveHysteresisBoundaries() ||
     !TestProviderErrorIsAtomicAndRetryable() ||
     !TestEstimateProvenanceMismatch() ||
     !TestNonfiniteAndPolicyValidation() ||
     !TestUniverseAndInvalidPositionValidation() ||
     !TestEmptyAndSingleNodeSnapshots()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
