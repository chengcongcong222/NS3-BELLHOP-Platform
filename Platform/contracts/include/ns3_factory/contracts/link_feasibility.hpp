#pragma once

#include <cmath>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::contracts {

class LinkFeasibilityQuery final {
 public:
  [[nodiscard]] static auto Create(NodeId source_node_id,
                                   NodeId target_node_id,
                                   Position3d source_position,
                                   Position3d target_position,
                                   SimTime observed_at)
      -> Result<LinkFeasibilityQuery>;

  [[nodiscard]] constexpr auto source_node_id() const noexcept -> NodeId {
    return source_node_id_;
  }

  [[nodiscard]] constexpr auto target_node_id() const noexcept -> NodeId {
    return target_node_id_;
  }

  [[nodiscard]] constexpr auto source_position() const noexcept
      -> const Position3d& {
    return source_position_;
  }

  [[nodiscard]] constexpr auto target_position() const noexcept
      -> const Position3d& {
    return target_position_;
  }

  [[nodiscard]] constexpr auto observed_at() const noexcept -> SimTime {
    return observed_at_;
  }

  auto operator==(const LinkFeasibilityQuery&) const -> bool = default;

 private:
  constexpr LinkFeasibilityQuery(NodeId source_node_id,
                                 NodeId target_node_id,
                                 Position3d source_position,
                                 Position3d target_position,
                                 SimTime observed_at) noexcept
      : source_node_id_(source_node_id),
        target_node_id_(target_node_id),
        source_position_(source_position),
        target_position_(target_position),
        observed_at_(observed_at) {}

  NodeId source_node_id_;
  NodeId target_node_id_;
  Position3d source_position_;
  Position3d target_position_;
  SimTime observed_at_;
};

inline auto LinkFeasibilityQuery::Create(NodeId source_node_id,
                                         NodeId target_node_id,
                                         Position3d source_position,
                                         Position3d target_position,
                                         SimTime observed_at)
    -> Result<LinkFeasibilityQuery> {
  if(source_node_id == target_node_id) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "LinkFeasibilityQuery does not allow a self-link"});
  }
  const auto position_is_finite = [](const Position3d& position) {
    return std::isfinite(position.x_meters) &&
           std::isfinite(position.y_meters) &&
           std::isfinite(position.z_meters);
  };
  if(!position_is_finite(source_position) ||
     !position_is_finite(target_position)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "LinkFeasibilityQuery positions must be finite"});
  }

  return LinkFeasibilityQuery{source_node_id,
                              target_node_id,
                              source_position,
                              target_position,
                              observed_at};
}

class LinkFeasibilityEstimate final {
 public:
  [[nodiscard]] static auto Create(NodeId source_node_id,
                                   NodeId target_node_id,
                                   SimTime observed_at,
                                   double feasibility_score)
      -> Result<LinkFeasibilityEstimate>;

  [[nodiscard]] constexpr auto source_node_id() const noexcept -> NodeId {
    return source_node_id_;
  }

  [[nodiscard]] constexpr auto target_node_id() const noexcept -> NodeId {
    return target_node_id_;
  }

  [[nodiscard]] constexpr auto observed_at() const noexcept -> SimTime {
    return observed_at_;
  }

  // Dimensionless estimator-specific decision score. Larger is more
  // feasible for the paired policy. It is not SNR, PER, transmission loss,
  // or received level and is not required to be within [0, 1].
  [[nodiscard]] constexpr auto feasibility_score() const noexcept
      -> double {
    return feasibility_score_;
  }

  auto operator==(const LinkFeasibilityEstimate&) const -> bool = default;

 private:
  constexpr LinkFeasibilityEstimate(NodeId source_node_id,
                                    NodeId target_node_id,
                                    SimTime observed_at,
                                    double feasibility_score) noexcept
      : source_node_id_(source_node_id),
        target_node_id_(target_node_id),
        observed_at_(observed_at),
        feasibility_score_(feasibility_score) {}

  NodeId source_node_id_;
  NodeId target_node_id_;
  SimTime observed_at_;
  double feasibility_score_;
};

inline auto LinkFeasibilityEstimate::Create(NodeId source_node_id,
                                            NodeId target_node_id,
                                            SimTime observed_at,
                                            double feasibility_score)
    -> Result<LinkFeasibilityEstimate> {
  if(source_node_id == target_node_id) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "LinkFeasibilityEstimate does not allow a self-link"});
  }
  if(!std::isfinite(feasibility_score)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "LinkFeasibilityEstimate score must be finite"});
  }
  return LinkFeasibilityEstimate{
      source_node_id, target_node_id, observed_at, feasibility_score};
}

[[nodiscard]] inline auto ValidateLinkFeasibilityEstimate(
    const LinkFeasibilityQuery& query,
    const LinkFeasibilityEstimate& estimate) -> Status {
  if(query.source_node_id() != estimate.source_node_id() ||
     query.target_node_id() != estimate.target_node_id() ||
     query.observed_at() != estimate.observed_at()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "LinkFeasibilityEstimate provenance does not match query"});
  }
  if(!std::isfinite(estimate.feasibility_score())) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "LinkFeasibilityEstimate score must be finite"});
  }
  return {};
}

class ILinkFeasibilityEstimator {
 public:
  virtual ~ILinkFeasibilityEstimator() = default;

  // Implementations must be deterministic for the same query and immutable
  // estimator configuration. Randomness requires a future explicit,
  // deterministic RNG contract.
  [[nodiscard]] virtual auto Estimate(
      const LinkFeasibilityQuery& query) const
      -> Result<LinkFeasibilityEstimate> = 0;
};

}  // namespace ns3_factory::contracts
