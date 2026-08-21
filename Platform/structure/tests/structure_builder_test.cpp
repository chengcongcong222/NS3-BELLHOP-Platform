#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/link_feasibility.hpp>
#include <ns3_factory/contracts/node_capability.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/topology.hpp>

#include "internal/all_feasible_links_topology_policy.hpp"
#include "internal/configured_role_assignment_policy.hpp"
#include "internal/connectivity_decision_policy.hpp"
#include "internal/single_sink_star_topology_policy.hpp"
#include "internal/structure_builder.hpp"

using ns3_factory::contracts::ConnectivityGraph;
using ns3_factory::contracts::DirectedLink;
using ns3_factory::contracts::DuplexMode;
using ns3_factory::contracts::Error;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::ILinkFeasibilityEstimator;
using ns3_factory::contracts::LinkFeasibilityEstimate;
using ns3_factory::contracts::LinkFeasibilityQuery;
using ns3_factory::contracts::LogicalLink;
using ns3_factory::contracts::LogicalTopology;
using ns3_factory::contracts::MotionState;
using ns3_factory::contracts::NodeCapabilityProfile;
using ns3_factory::contracts::NodeCommittedState;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PlanningCycleId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::ProtocolRole;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::RoleBinding;
using ns3_factory::contracts::RoleTable;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::SnapshotVersion;
using ns3_factory::contracts::Velocity3d;
using ns3_factory::contracts::WorldSnapshot;
using ns3_factory::structure::internal::AllFeasibleLinksTopologyPolicy;
using ns3_factory::structure::internal::ConfiguredRoleAssignmentPolicy;
using ns3_factory::structure::internal::ConnectivityDecisionPolicy;
using ns3_factory::structure::internal::ILogicalTopologyPolicy;
using ns3_factory::structure::internal::IRoleAssignmentPolicy;
using ns3_factory::structure::internal::SingleSinkStarTopologyPolicy;
using ns3_factory::structure::internal::StructureBuilder;
using ns3_factory::structure::internal::StructureBuildRequest;

namespace {

constexpr auto MakeNode(std::uint64_t id,
                        bool can_transmit = true,
                        bool can_receive = true) -> NodeCommittedState {
  return NodeCommittedState{
      NodeId{id},
      NodeCapabilityProfile{
          can_transmit, can_receive, DuplexMode::kHalfDuplex},
      MotionState{Position3d{static_cast<double>(id), 0.0, 0.0},
                  Velocity3d{0.0, 0.0, 0.0}}};
}

auto MakeSnapshot(std::vector<NodeCommittedState> nodes,
                  SnapshotVersion version = SnapshotVersion{11}) {
  return WorldSnapshot::Create(version,
                               SimTime::FromNanoseconds(500),
                               std::move(nodes));
}

struct ScoreEntry final {
  NodeId source_node_id;
  NodeId target_node_id;
  double score;
};

class TestEstimator final : public ILinkFeasibilityEstimator {
 public:
  explicit TestEstimator(double default_score,
                         std::vector<ScoreEntry> scores = {},
                         bool fail_first = false) noexcept
      : default_score_(default_score),
        scores_(std::move(scores)),
        fail_first_(fail_first) {}

  [[nodiscard]] auto Estimate(const LinkFeasibilityQuery& query) const
      -> Result<LinkFeasibilityEstimate> override {
    queries_.push_back(query);
    if(fail_first_ && queries_.size() == 1) {
      return std::unexpected(
          Error{ErrorCode::kInternal, "test estimator failure"});
    }
    const auto entry = std::find_if(
        scores_.begin(), scores_.end(), [&](const ScoreEntry& candidate) {
          return candidate.source_node_id == query.source_node_id() &&
                 candidate.target_node_id == query.target_node_id();
        });
    const auto score = entry == scores_.end() ? default_score_
                                               : entry->score;
    return LinkFeasibilityEstimate::Create(query.source_node_id(),
                                           query.target_node_id(),
                                           query.observed_at(),
                                           score);
  }

  [[nodiscard]] auto queries() const noexcept
      -> std::span<const LinkFeasibilityQuery> {
    return queries_;
  }

 private:
  double default_score_;
  std::vector<ScoreEntry> scores_;
  bool fail_first_;
  mutable std::vector<LinkFeasibilityQuery> queries_;
};

class FailOnceRolePolicy final : public IRoleAssignmentPolicy {
 public:
  explicit FailOnceRolePolicy(
      std::vector<RoleBinding> bindings) noexcept
      : delegate_(std::move(bindings)) {}

  [[nodiscard]] auto Assign(
      const WorldSnapshot& snapshot,
      const ConnectivityGraph& graph,
      PlanningCycleId cycle_id) const -> Result<RoleTable> override {
    if(calls_++ == 0) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable, "test role failure"});
    }
    return delegate_.Assign(snapshot, graph, cycle_id);
  }

 private:
  ConfiguredRoleAssignmentPolicy delegate_;
  mutable std::size_t calls_{0};
};

class FailOnceTopologyPolicy final : public ILogicalTopologyPolicy {
 public:
  [[nodiscard]] auto Build(
      const WorldSnapshot& snapshot,
      const RoleTable& roles,
      const ConnectivityGraph& graph) const
      -> Result<LogicalTopology> override {
    if(calls_++ == 0) {
      return std::unexpected(
          Error{ErrorCode::kUnavailable, "test topology failure"});
    }
    return delegate_.Build(snapshot, roles, graph);
  }

 private:
  AllFeasibleLinksTopologyPolicy delegate_;
  mutable std::size_t calls_{0};
};

auto MakeDecisionPolicy() {
  return ConnectivityDecisionPolicy::Create(std::nullopt, 0.8, 0.6);
}

auto TestEndToEndStarBuildAndDeterminism() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(2), MakeNode(0), MakeNode(1)});
  const auto decision_policy = MakeDecisionPolicy();
  if(!snapshot || !decision_policy) {
    return false;
  }

  const std::vector<ScoreEntry> scores{
      ScoreEntry{NodeId{0}, NodeId{2}, 0.9},
      ScoreEntry{NodeId{1}, NodeId{2}, 0.9},
      ScoreEntry{NodeId{2}, NodeId{0}, 0.9}};
  TestEstimator estimator{0.0, scores};
  ConfiguredRoleAssignmentPolicy roles{
      {RoleBinding{NodeId{2}, ProtocolRole::kController},
       RoleBinding{NodeId{1}, ProtocolRole::kMember},
       RoleBinding{NodeId{2}, ProtocolRole::kSink},
       RoleBinding{NodeId{0}, ProtocolRole::kMember}}};
  SingleSinkStarTopologyPolicy topology;
  const StructureBuilder builder{
      *decision_policy, estimator, roles, topology};

  const StructureBuildRequest request{PlanningCycleId{4}, *snapshot};
  const auto first = builder.Build(request);
  const auto second = builder.Build(request);
  if(!first || !second || *first != *second) {
    return false;
  }

  const auto& graph = first->connectivity_graph();
  const auto& role_table = first->role_table();
  const auto& logical = first->logical_topology();
  return first->cycle_id() == PlanningCycleId{4} &&
         first->base_snapshot_version() == snapshot->version() &&
         graph.edges().size() == 3 &&
         graph.HasEdge(NodeId{0}, NodeId{2}) &&
         graph.HasEdge(NodeId{1}, NodeId{2}) &&
         graph.HasEdge(NodeId{2}, NodeId{0}) &&
         role_table.HasRole(NodeId{0}, ProtocolRole::kMember) &&
         role_table.HasRole(NodeId{1}, ProtocolRole::kMember) &&
         role_table.HasRole(NodeId{2}, ProtocolRole::kSink) &&
         role_table.HasRole(NodeId{2}, ProtocolRole::kController) &&
         logical.links().size() == 3 &&
         logical.HasLink(NodeId{0}, NodeId{2}) &&
         logical.HasLink(NodeId{1}, NodeId{2}) &&
         logical.HasLink(NodeId{2}, NodeId{0}) &&
         estimator.queries().size() == 12 &&
         std::all_of(estimator.queries().begin(),
                     estimator.queries().end(),
                     [&](const LinkFeasibilityQuery& query) {
                       return query.observed_at() ==
                              snapshot->committed_at();
                     });
}

auto TestPreviousConnectivityHysteresisHandoff() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(0, true, false), MakeNode(1, false, true)},
      SnapshotVersion{12});
  const auto decision_policy = MakeDecisionPolicy();
  const auto previous = ConnectivityGraph::Create(
      {DirectedLink{NodeId{0}, NodeId{1}}}, {NodeId{0}, NodeId{1}});
  if(!snapshot || !decision_policy || !previous) {
    return false;
  }

  TestEstimator estimator{0.7};
  ConfiguredRoleAssignmentPolicy roles{
      {RoleBinding{NodeId{0}, ProtocolRole::kMember},
       RoleBinding{NodeId{1}, ProtocolRole::kSink}}};
  SingleSinkStarTopologyPolicy topology;
  const StructureBuilder builder{
      *decision_policy, estimator, roles, topology};

  const auto without_previous = builder.Build(
      StructureBuildRequest{PlanningCycleId{7}, *snapshot});
  const auto with_previous = builder.Build(
      StructureBuildRequest{PlanningCycleId{8},
                            *snapshot,
                            std::optional{std::cref(*previous)}});
  return without_previous &&
         without_previous->connectivity_graph().edges().empty() &&
         without_previous->logical_topology().links().empty() &&
         with_previous &&
         with_previous->connectivity_graph().HasEdge(NodeId{0},
                                                      NodeId{1}) &&
         with_previous->logical_topology().HasLink(NodeId{0}, NodeId{1});
}

auto TestEstimatorFailureThenRetry() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(0, true, false), MakeNode(1, false, true)});
  const auto decision_policy = MakeDecisionPolicy();
  if(!snapshot || !decision_policy) {
    return false;
  }
  TestEstimator estimator{1.0, {}, true};
  ConfiguredRoleAssignmentPolicy roles{
      {RoleBinding{NodeId{1}, ProtocolRole::kSink}}};
  AllFeasibleLinksTopologyPolicy topology;
  const StructureBuilder builder{
      *decision_policy, estimator, roles, topology};
  const StructureBuildRequest request{PlanningCycleId{20}, *snapshot};
  const auto failed = builder.Build(request);
  const auto retried = builder.Build(request);
  return !failed && failed.error().code == ErrorCode::kInternal &&
         retried && retried->connectivity_graph().edges().size() == 1;
}

auto TestRoleFailureThenRetry() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(0, true, false), MakeNode(1, false, true)});
  const auto decision_policy = MakeDecisionPolicy();
  if(!snapshot || !decision_policy) {
    return false;
  }
  TestEstimator estimator{1.0};
  FailOnceRolePolicy roles{
      {RoleBinding{NodeId{1}, ProtocolRole::kSink}}};
  AllFeasibleLinksTopologyPolicy topology;
  const StructureBuilder builder{
      *decision_policy, estimator, roles, topology};
  const StructureBuildRequest request{PlanningCycleId{21}, *snapshot};
  const auto failed = builder.Build(request);
  const auto retried = builder.Build(request);
  return !failed && failed.error().code == ErrorCode::kUnavailable &&
         retried && retried->role_table().HasRole(
                        NodeId{1}, ProtocolRole::kSink);
}

auto TestTopologyFailureThenRetry() -> bool {
  const auto snapshot = MakeSnapshot(
      {MakeNode(0, true, false), MakeNode(1, false, true)});
  const auto decision_policy = MakeDecisionPolicy();
  if(!snapshot || !decision_policy) {
    return false;
  }
  TestEstimator estimator{1.0};
  ConfiguredRoleAssignmentPolicy roles{
      {RoleBinding{NodeId{1}, ProtocolRole::kSink}}};
  FailOnceTopologyPolicy topology;
  const StructureBuilder builder{
      *decision_policy, estimator, roles, topology};
  const StructureBuildRequest request{PlanningCycleId{22}, *snapshot};
  const auto failed = builder.Build(request);
  const auto retried = builder.Build(request);
  return !failed && failed.error().code == ErrorCode::kUnavailable &&
         retried && retried->logical_topology().links().size() == 1;
}

}  // namespace

auto main() -> int {
  return TestEndToEndStarBuildAndDeterminism() &&
                 TestPreviousConnectivityHysteresisHandoff() &&
                 TestEstimatorFailureThenRetry() &&
                 TestRoleFailureThenRetry() &&
                 TestTopologyFailureThenRetry()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
