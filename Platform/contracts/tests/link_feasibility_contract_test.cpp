#include <cmath>
#include <concepts>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include <ns3_factory/contracts/link_feasibility.hpp>

using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::ILinkFeasibilityEstimator;
using ns3_factory::contracts::LinkFeasibilityEstimate;
using ns3_factory::contracts::LinkFeasibilityQuery;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::Status;
using ns3_factory::contracts::ValidateLinkFeasibilityEstimate;

static_assert(!std::is_default_constructible_v<LinkFeasibilityQuery>);
static_assert(!std::is_default_constructible_v<LinkFeasibilityEstimate>);
static_assert(!std::is_aggregate_v<LinkFeasibilityQuery>);
static_assert(!std::is_aggregate_v<LinkFeasibilityEstimate>);
static_assert(std::has_virtual_destructor_v<ILinkFeasibilityEstimator>);
static_assert(requires(const ILinkFeasibilityEstimator& estimator,
                       const LinkFeasibilityQuery& query) {
  { estimator.Estimate(query) } ->
      std::same_as<Result<LinkFeasibilityEstimate>>;
});
static_assert(requires(const LinkFeasibilityQuery& query,
                       const LinkFeasibilityEstimate& estimate) {
  { ValidateLinkFeasibilityEstimate(query, estimate) } ->
      std::same_as<Status>;
});
static_assert(std::same_as<
              decltype(std::declval<const LinkFeasibilityQuery&>()
                           .source_position()),
              const Position3d&>);

namespace {

constexpr auto At(std::int64_t nanoseconds) -> SimTime {
  return SimTime::FromNanoseconds(nanoseconds);
}

class DeterministicEstimator final : public ILinkFeasibilityEstimator {
 public:
  [[nodiscard]] auto Estimate(const LinkFeasibilityQuery& query) const
      -> Result<LinkFeasibilityEstimate> override {
    const auto score = static_cast<double>(query.source_node_id().value()) -
                       static_cast<double>(query.target_node_id().value());
    return LinkFeasibilityEstimate::Create(query.source_node_id(),
                                           query.target_node_id(),
                                           query.observed_at(),
                                           score);
  }
};

auto TestQueryValidationAndNodeZero() -> bool {
  const auto query = LinkFeasibilityQuery::Create(
      NodeId{0},
      NodeId{1},
      Position3d{0.0, 1.0, -2.0},
      Position3d{3.0, 4.0, -5.0},
      At(9));
  const auto self = LinkFeasibilityQuery::Create(
      NodeId{0},
      NodeId{0},
      Position3d{0.0, 0.0, 0.0},
      Position3d{1.0, 0.0, 0.0},
      At(9));
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto infinity = std::numeric_limits<double>::infinity();
  const auto bad_source = LinkFeasibilityQuery::Create(
      NodeId{0},
      NodeId{1},
      Position3d{nan, 0.0, 0.0},
      Position3d{1.0, 0.0, 0.0},
      At(9));
  const auto bad_target = LinkFeasibilityQuery::Create(
      NodeId{0},
      NodeId{1},
      Position3d{0.0, 0.0, 0.0},
      Position3d{1.0, infinity, 0.0},
      At(9));

  return query && query->source_node_id() == NodeId{0} &&
         query->target_node_id() == NodeId{1} &&
         query->source_position() == Position3d{0.0, 1.0, -2.0} &&
         query->target_position() == Position3d{3.0, 4.0, -5.0} &&
         query->observed_at() == At(9) && !self &&
         self.error().code == ErrorCode::kInvalidArgument &&
         !bad_source &&
         bad_source.error().code == ErrorCode::kInvalidArgument &&
         !bad_target &&
         bad_target.error().code == ErrorCode::kInvalidArgument;
}

auto TestEstimateScoreAndValidation() -> bool {
  const auto query = LinkFeasibilityQuery::Create(
      NodeId{0},
      NodeId{1},
      Position3d{0.0, 0.0, 0.0},
      Position3d{1.0, 0.0, 0.0},
      At(12));
  const auto estimate = LinkFeasibilityEstimate::Create(
      NodeId{0}, NodeId{1}, At(12), 42.5);
  if(!query || !estimate) {
    return false;
  }

  const auto valid = ValidateLinkFeasibilityEstimate(*query, *estimate);
  const auto wrong_source = LinkFeasibilityEstimate::Create(
      NodeId{2}, NodeId{1}, At(12), 100.0);
  const auto wrong_target = LinkFeasibilityEstimate::Create(
      NodeId{0}, NodeId{2}, At(12), 100.0);
  const auto wrong_time = LinkFeasibilityEstimate::Create(
      NodeId{0}, NodeId{1}, At(13), 100.0);
  const auto nan = LinkFeasibilityEstimate::Create(
      NodeId{0},
      NodeId{1},
      At(12),
      std::numeric_limits<double>::quiet_NaN());
  const auto positive_infinity = LinkFeasibilityEstimate::Create(
      NodeId{0},
      NodeId{1},
      At(12),
      std::numeric_limits<double>::infinity());
  const auto negative_infinity = LinkFeasibilityEstimate::Create(
      NodeId{0},
      NodeId{1},
      At(12),
      -std::numeric_limits<double>::infinity());

  return valid && estimate->feasibility_score() == 42.5 &&
         wrong_source &&
         !ValidateLinkFeasibilityEstimate(*query, *wrong_source) &&
         wrong_target &&
         !ValidateLinkFeasibilityEstimate(*query, *wrong_target) &&
         wrong_time &&
         !ValidateLinkFeasibilityEstimate(*query, *wrong_time) && !nan &&
         nan.error().code == ErrorCode::kInvalidArgument &&
         !positive_infinity &&
         positive_infinity.error().code == ErrorCode::kInvalidArgument &&
         !negative_infinity &&
         negative_infinity.error().code == ErrorCode::kInvalidArgument;
}

auto TestDeterministicEstimatorContract() -> bool {
  const auto query = LinkFeasibilityQuery::Create(
      NodeId{2},
      NodeId{0},
      Position3d{1.0, 2.0, 3.0},
      Position3d{4.0, 5.0, 6.0},
      At(20));
  if(!query) {
    return false;
  }
  const DeterministicEstimator estimator;
  const auto first = estimator.Estimate(*query);
  const auto second = estimator.Estimate(*query);
  return first && second && *first == *second &&
         first->feasibility_score() == 2.0;
}

}  // namespace

auto main() -> int {
  if(!TestQueryValidationAndNodeZero() ||
     !TestEstimateScoreAndValidation() ||
     !TestDeterministicEstimatorContract()) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
