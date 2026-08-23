#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/time.hpp>

#include "acoustic_field_asset.hpp"
#include "discrete_frequency_selector.hpp"
#include "environment_coordinate_frame.hpp"
#include "grid_axis_lookup.hpp"

namespace ns3_factory::environment::internal {

class AcousticFieldChannelProvider final
    : public contracts::IChannelFieldProvider {
 public:
  [[nodiscard]] static auto Create(
      std::shared_ptr<const AcousticFieldAsset> asset,
      DiscreteFrequencySelectionPolicy frequency_policy)
      -> contracts::Result<AcousticFieldChannelProvider> {
    if(!asset) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Acoustic field provider requires a non-null "
                           "asset"});
    }
    return AcousticFieldChannelProvider{std::move(asset), frequency_policy};
  }

  [[nodiscard]] auto Query(const contracts::ChannelQuery& query) const
      -> contracts::Result<contracts::ChannelFieldOutcome> override;

  [[nodiscard]] constexpr auto asset() const noexcept
      -> const AcousticFieldAsset& {
    return *asset_;
  }

 private:
  AcousticFieldChannelProvider(
      std::shared_ptr<const AcousticFieldAsset> asset,
      DiscreteFrequencySelectionPolicy frequency_policy) noexcept
      : asset_(std::move(asset)),
        frequency_policy_(frequency_policy) {}

  std::shared_ptr<const AcousticFieldAsset> asset_;
  DiscreteFrequencySelectionPolicy frequency_policy_;
};

namespace detail {

struct AxisWeights final {
  std::array<std::size_t, 2U> indices;
  std::array<long double, 2U> weights;
  std::size_t count;
};

[[nodiscard]] inline auto BuildAxisWeights(const GridAxisLookup& lookup)
    -> AxisWeights {
  if(lookup.lower_index == lookup.upper_index) {
    return AxisWeights{{lookup.lower_index, lookup.lower_index},
                       {1.0L, 0.0L},
                       1U};
  }
  return AxisWeights{{lookup.lower_index, lookup.upper_index},
                     {1.0L - lookup.upper_weight, lookup.upper_weight},
                     2U};
}

}  // namespace detail

inline auto AcousticFieldChannelProvider::Query(
    const contracts::ChannelQuery& query) const
    -> contracts::Result<contracts::ChannelFieldOutcome> {
  const auto source_depth =
      asset_->coordinate_frame().DepthMeters(query.tx_position());
  if(!source_depth) return std::unexpected(source_depth.error());
  const auto receiver_depth =
      asset_->coordinate_frame().DepthMeters(query.rx_position());
  if(!receiver_depth) return std::unexpected(receiver_depth.error());
  const auto horizontal_range =
      HorizontalRangeMeters(query.tx_position(), query.rx_position());
  if(!horizontal_range) return std::unexpected(horizontal_range.error());

  const auto frequency_index = frequency_policy_.Select(
      asset_->frequency_hz(), query.center_frequency_hz());
  if(!frequency_index) return std::unexpected(frequency_index.error());
  const auto source_lookup =
      ResolveGridAxis(asset_->source_depth_m(), *source_depth);
  if(!source_lookup) return std::unexpected(source_lookup.error());
  const auto receiver_lookup =
      ResolveGridAxis(asset_->receiver_depth_m(), *receiver_depth);
  if(!receiver_lookup) return std::unexpected(receiver_lookup.error());
  const auto range_lookup =
      ResolveGridAxis(asset_->horizontal_range_m(), *horizontal_range);
  if(!range_lookup) return std::unexpected(range_lookup.error());

  const auto source_weights = detail::BuildAxisWeights(*source_lookup);
  const auto receiver_weights = detail::BuildAxisWeights(*receiver_lookup);
  const auto range_weights = detail::BuildAxisWeights(*range_lookup);
  long double transmission_loss_db = 0.0L;
  long double first_arrival_nanoseconds = 0.0L;
  for(std::size_t source = 0U; source < source_weights.count; ++source) {
    for(std::size_t receiver = 0U; receiver < receiver_weights.count;
        ++receiver) {
      for(std::size_t range = 0U; range < range_weights.count; ++range) {
        const auto weight = source_weights.weights[source] *
                            receiver_weights.weights[receiver] *
                            range_weights.weights[range];
        const auto& cell = asset_->cell(
            *frequency_index,
            source_weights.indices[source],
            receiver_weights.indices[receiver],
            range_weights.indices[range]);
        transmission_loss_db +=
            weight * static_cast<long double>(
                         cell.aggregate_transmission_loss_db);
        first_arrival_nanoseconds +=
            weight * static_cast<long double>(
                         cell.first_arrival_delay.nanoseconds());
      }
    }
  }

  const auto interpolated_loss =
      static_cast<double>(transmission_loss_db);
  if(!std::isfinite(interpolated_loss)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Interpolated acoustic transmission loss is not "
                         "finite"});
  }
  const auto rounded_nanoseconds = std::round(first_arrival_nanoseconds);
  constexpr auto kMaximumNanoseconds =
      std::numeric_limits<contracts::NanosecondCount>::max();
  if(rounded_nanoseconds < 0.0L ||
     rounded_nanoseconds >
         static_cast<long double>(kMaximumNanoseconds)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Interpolated acoustic arrival delay is outside "
                         "int64 nanoseconds"});
  }

  const auto& nearest_cell = asset_->cell(
      *frequency_index,
      source_lookup->nearest_index,
      receiver_lookup->nearest_index,
      range_lookup->nearest_index);
  auto response = contracts::ChannelFieldResponse::Create(
      query.transmission_id(),
      query.receiver_node_id(),
      interpolated_loss,
      contracts::SimDuration::FromNanoseconds(
          static_cast<contracts::NanosecondCount>(rounded_nanoseconds)),
      nearest_cell.paths);
  if(!response) return std::unexpected(response.error());
  contracts::ChannelFieldOutcome outcome{std::move(*response)};
  const auto identity =
      contracts::ValidateChannelFieldOutcomeIdentity(query, outcome);
  if(!identity) return std::unexpected(identity.error());
  return outcome;
}

}  // namespace ns3_factory::environment::internal
