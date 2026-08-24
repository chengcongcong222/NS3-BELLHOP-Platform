#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>

#include "internal/acoustic_field_channel_provider.hpp"

using namespace ns3_factory::contracts;
using namespace ns3_factory::environment::internal;

namespace {

auto Path(double gain, double phase = 0.0) -> PropagationPath {
  return *PropagationPath::Create(SimDuration::Zero(), gain, phase);
}

auto MakeAffineAsset(std::vector<double> ranges = {0.0, 10.0})
    -> std::shared_ptr<const AcousticFieldAsset> {
  const std::vector<double> frequencies{20'000.0, 30'000.0};
  const std::vector<double> source_depths{10.0, 20.0};
  const std::vector<double> receiver_depths{30.0, 50.0};
  std::vector<AcousticFieldCell> cells;
  for(std::size_t f = 0U; f < frequencies.size(); ++f) {
    for(std::size_t s = 0U; s < source_depths.size(); ++s) {
      for(std::size_t receiver = 0U;
          receiver < receiver_depths.size();
          ++receiver) {
        for(std::size_t range = 0U; range < ranges.size(); ++range) {
          const auto loss = 100.0 + 1000.0 * static_cast<double>(f) +
                            2.0 * source_depths[s] +
                            3.0 * receiver_depths[receiver] +
                            4.0 * ranges[range];
          const auto delay = static_cast<NanosecondCount>(
              1'000 + 10'000 * f + 10.0 * source_depths[s] +
              20.0 * receiver_depths[receiver] +
              30.0 * ranges[range]);
          const auto identity = static_cast<double>(
              1U + 100U * f + 20U * s + 5U * receiver + range);
          cells.push_back(AcousticFieldSignalCell{
              loss,
              SimDuration::FromNanoseconds(delay),
              {Path(identity, -identity)}});
        }
      }
    }
  }
  const auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!frame) return {};
  auto asset = AcousticFieldAsset::Create(1U,
                                          "affine test field",
                                          *frame,
                                          frequencies,
                                          source_depths,
                                          receiver_depths,
                                          std::move(ranges),
                                          std::move(cells));
  if(!asset) return {};
  return std::make_shared<const AcousticFieldAsset>(std::move(*asset));
}

auto MakeProvider(std::shared_ptr<const AcousticFieldAsset> asset,
                  double max_offset_hz = 5'000.0)
    -> Result<AcousticFieldChannelProvider> {
  auto frequency =
      DiscreteFrequencySelectionPolicy::Create(max_offset_hz);
  if(!frequency) return std::unexpected(frequency.error());
  return AcousticFieldChannelProvider::Create(
      std::move(asset), *frequency);
}

auto MakeMixedCoverageAsset()
    -> std::shared_ptr<const AcousticFieldAsset> {
  const auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!frame) return {};
  auto asset = AcousticFieldAsset::Create(
      1U,
      "mixed coverage fixture",
      *frame,
      {20'000.0},
      {10.0},
      {30.0},
      {0.0, 10.0},
      {AcousticFieldNoArrivalCell{},
       AcousticFieldSignalCell{
           91.0,
           SimDuration::FromNanoseconds(123),
           {Path(0.25, 0.75)}}});
  if(!asset) return {};
  return std::make_shared<const AcousticFieldAsset>(std::move(*asset));
}

auto Query(TransmissionId transmission_id,
           NodeId receiver_id,
           double source_depth,
           double receiver_depth,
           double horizontal_range,
           double frequency_hz = 20'000.0,
           double bandwidth_hz = 2'000.0,
           SimTime emitted_at = SimTime::Zero()) -> Result<ChannelQuery> {
  return ChannelQuery::Create(transmission_id,
                              NodeId{0},
                              receiver_id,
                              Position3d{0.0, 0.0, -source_depth},
                              Position3d{horizontal_range,
                                         0.0,
                                         -receiver_depth},
                              emitted_at,
                              frequency_hz,
                              bandwidth_hz);
}

auto QueryResponse(const AcousticFieldChannelProvider& provider,
                   const ChannelQuery& query)
    -> Result<ChannelFieldResponse> {
  auto outcome = provider.Query(query);
  if(!outcome) return std::unexpected(outcome.error());
  const auto* response = std::get_if<ChannelFieldResponse>(&*outcome);
  if(response == nullptr) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Normalized acoustic field unexpectedly returned no-arrival"});
  }
  return *response;
}

auto PhysicalEqual(const ChannelFieldResponse& lhs,
                   const ChannelFieldResponse& rhs) -> bool {
  return lhs.aggregate_transmission_loss_db() ==
             rhs.aggregate_transmission_loss_db() &&
         lhs.first_arrival_delay() == rhs.first_arrival_delay() &&
         std::ranges::equal(lhs.paths(), rhs.paths());
}

auto TestCoordinateMappingAndHorizontalRange() -> bool {
  auto up = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  auto down = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveDown);
  if(!up || !down) return false;
  const auto up_depth = up->DepthMeters(Position3d{0.0, 0.0, -10.0});
  const auto down_depth = down->DepthMeters(Position3d{0.0, 0.0, 10.0});
  const auto wrong_up = up->DepthMeters(Position3d{0.0, 0.0, 1.0});
  const auto wrong_down = down->DepthMeters(Position3d{0.0, 0.0, -1.0});
  const auto range = HorizontalRangeMeters(Position3d{0.0, 0.0, -10.0},
                                           Position3d{3.0, 4.0, -100.0});
  return up_depth && *up_depth == 10.0 && down_depth &&
         *down_depth == 10.0 && !wrong_up && !wrong_down && range &&
         *range == 5.0;
}

auto TestFrequencySelectionAndBandwidthIndependence() -> bool {
  auto provider = MakeProvider(MakeAffineAsset());
  if(!provider) return false;
  const auto exact = Query(TransmissionId{1}, NodeId{1}, 10, 30, 0,
                           20'000.0);
  const auto near_low = Query(TransmissionId{2}, NodeId{1}, 10, 30, 0,
                              21'000.0);
  const auto near_high = Query(TransmissionId{3}, NodeId{1}, 10, 30, 0,
                               29'000.0);
  const auto tie = Query(TransmissionId{4}, NodeId{1}, 10, 30, 0,
                         25'000.0);
  const auto out = Query(TransmissionId{5}, NodeId{1}, 10, 30, 0,
                         35'001.0);
  const auto wide = Query(TransmissionId{6}, NodeId{1}, 10, 30, 0,
                          21'000.0, 19'000.0);
  if(!exact || !near_low || !near_high || !tie || !out || !wide) {
    return false;
  }
  const auto exact_response = QueryResponse(*provider, *exact);
  const auto low_response = QueryResponse(*provider, *near_low);
  const auto high_response = QueryResponse(*provider, *near_high);
  const auto tie_response = QueryResponse(*provider, *tie);
  const auto out_response = provider->Query(*out);
  const auto wide_response = QueryResponse(*provider, *wide);
  return exact_response && low_response && high_response && tie_response &&
         !out_response && wide_response &&
         exact_response->aggregate_transmission_loss_db() == 210.0 &&
         low_response->aggregate_transmission_loss_db() == 210.0 &&
         tie_response->aggregate_transmission_loss_db() == 210.0 &&
         high_response->aggregate_transmission_loss_db() == 1210.0 &&
         wide_response->aggregate_transmission_loss_db() == 210.0;
}

auto ExpectedLoss(double source_depth,
                  double receiver_depth,
                  double range) -> double {
  return 100.0 + 2.0 * source_depth + 3.0 * receiver_depth +
         4.0 * range;
}

auto ExpectedDelay(double source_depth,
                   double receiver_depth,
                   double range) -> SimDuration {
  return SimDuration::FromNanoseconds(static_cast<NanosecondCount>(
      1'000 + 10.0 * source_depth + 20.0 * receiver_depth +
      30.0 * range));
}

auto InterpolationMatches(AcousticFieldChannelProvider& provider,
                          std::uint64_t id,
                          double source_depth,
                          double receiver_depth,
                          double range) -> bool {
  const auto query = Query(TransmissionId{id}, NodeId{id}, source_depth,
                           receiver_depth, range);
  if(!query) return false;
  const auto response = QueryResponse(provider, *query);
  return response &&
         response->aggregate_transmission_loss_db() ==
             ExpectedLoss(source_depth, receiver_depth, range) &&
         response->first_arrival_delay() ==
             ExpectedDelay(source_depth, receiver_depth, range);
}

auto TestScalarInterpolationAtCornerEdgeFaceAndInterior() -> bool {
  auto provider = MakeProvider(MakeAffineAsset());
  return provider && InterpolationMatches(*provider, 10, 10, 30, 0) &&
         InterpolationMatches(*provider, 11, 15, 30, 0) &&
         InterpolationMatches(*provider, 12, 15, 40, 0) &&
         InterpolationMatches(*provider, 13, 15, 40, 5);
}

auto TestDelayRoundingToNearestNanosecond() -> bool {
  auto asset = AcousticFieldAsset::Create(
      1U,
      "rounding fixture",
      *EnvironmentCoordinateFrame::Create(
          0.0, VerticalAxisDirection::kPositiveUp),
      {20'000.0},
      {10.0},
      {30.0},
      {0.0, 2.0},
      {AcousticFieldSignalCell{1.0, SimDuration::Zero(), {}},
       AcousticFieldSignalCell{
           1.0, SimDuration::FromNanoseconds(1), {}}});
  if(!asset) return false;
  auto provider = MakeProvider(
      std::make_shared<const AcousticFieldAsset>(std::move(*asset)));
  const auto query = Query(TransmissionId{20}, NodeId{1}, 10, 30, 1);
  if(!provider || !query) return false;
  const auto response = QueryResponse(*provider, *query);
  return response &&
         response->first_arrival_delay() ==
             SimDuration::FromNanoseconds(1);
}

auto TestSingletonSpatialAxesRequireExactCoordinates() -> bool {
  const auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  if(!frame) return false;
  auto asset = AcousticFieldAsset::Create(
      1U,
      "singleton spatial axes fixture",
      *frame,
      {20'000.0},
      {10.0},
      {30.0},
      {5.0},
      {AcousticFieldSignalCell{
          42.0, SimDuration::FromNanoseconds(123), {}}});
  if(!asset) return false;
  auto provider = MakeProvider(
      std::make_shared<const AcousticFieldAsset>(std::move(*asset)));
  if(!provider) return false;

  const auto exact = Query(TransmissionId{21}, NodeId{1}, 10, 30, 5);
  const auto different_source =
      Query(TransmissionId{22}, NodeId{1}, 11, 30, 5);
  const auto different_receiver =
      Query(TransmissionId{23}, NodeId{1}, 10, 31, 5);
  const auto different_range =
      Query(TransmissionId{24}, NodeId{1}, 10, 30, 6);
  if(!exact || !different_source || !different_receiver ||
     !different_range) {
    return false;
  }
  const auto exact_response = QueryResponse(*provider, *exact);
  return exact_response &&
         exact_response->aggregate_transmission_loss_db() == 42.0 &&
         exact_response->first_arrival_delay() ==
             SimDuration::FromNanoseconds(123) &&
         !provider->Query(*different_source) &&
         !provider->Query(*different_receiver) &&
         !provider->Query(*different_range);
}

auto TestOutOfDomainDoesNotClampOrExtrapolate() -> bool {
  auto provider = MakeProvider(MakeAffineAsset({5.0, 10.0}));
  if(!provider) return false;
  const std::vector<Result<ChannelQuery>> queries{
      Query(TransmissionId{30}, NodeId{1}, 9, 30, 5),
      Query(TransmissionId{31}, NodeId{1}, 21, 30, 5),
      Query(TransmissionId{32}, NodeId{1}, 10, 29, 5),
      Query(TransmissionId{33}, NodeId{1}, 10, 51, 5),
      Query(TransmissionId{34}, NodeId{1}, 10, 30, 4),
      Query(TransmissionId{35}, NodeId{1}, 10, 30, 11)};
  for(const auto& query : queries) {
    if(!query || provider->Query(*query)) return false;
  }
  return true;
}

auto TestNearestCellPathsAndLowerTie() -> bool {
  auto provider = MakeProvider(MakeAffineAsset());
  if(!provider) return false;
  const auto tie = Query(TransmissionId{40}, NodeId{1}, 15, 40, 5);
  const auto upper = Query(TransmissionId{41}, NodeId{1}, 16, 41, 6);
  if(!tie || !upper) return false;
  const auto tie_response = QueryResponse(*provider, *tie);
  const auto upper_response = QueryResponse(*provider, *upper);
  return tie_response && upper_response && tie_response->paths().size() == 1U &&
         upper_response->paths().size() == 1U &&
         tie_response->paths().front().pressure_gain_linear() == 1.0 &&
         upper_response->paths().front().pressure_gain_linear() == 27.0;
}

auto TestStaticTimeAndIdentitySemantics() -> bool {
  auto provider = MakeProvider(MakeAffineAsset());
  if(!provider) return false;
  const auto at_t0 = Query(TransmissionId{50}, NodeId{7}, 15, 40, 5,
                           20'000.0, 2'000.0, SimTime::Zero());
  const auto at_t1 = Query(
      TransmissionId{51}, NodeId{8}, 15, 40, 5, 20'000.0, 2'000.0,
      SimTime::FromNanoseconds(9'000'000'000));
  if(!at_t0 || !at_t1) return false;
  const auto first = QueryResponse(*provider, *at_t0);
  const auto second = QueryResponse(*provider, *at_t1);
  return first && second && PhysicalEqual(*first, *second) &&
         first->transmission_id() == TransmissionId{50} &&
         first->receiver_node_id() == NodeId{7} &&
         second->transmission_id() == TransmissionId{51} &&
         second->receiver_node_id() == NodeId{8} &&
         ValidateChannelFieldResponseIdentity(*at_t0, *first) &&
         ValidateChannelFieldResponseIdentity(*at_t1, *second);
}

auto TestCoverageClassificationAndMixedStencilPolicy() -> bool {
  auto provider = MakeProvider(MakeMixedCoverageAsset());
  if(!provider) return false;
  const auto exact_no_arrival =
      Query(TransmissionId{60}, NodeId{0}, 10, 30, 0);
  const auto exact_signal =
      Query(TransmissionId{61}, NodeId{1}, 10, 30, 10);
  const auto nearest_no_arrival =
      Query(TransmissionId{62}, NodeId{2}, 10, 30, 4);
  const auto lower_tie_no_arrival =
      Query(TransmissionId{63}, NodeId{3}, 10, 30, 5);
  const auto nearest_signal =
      Query(TransmissionId{64}, NodeId{4}, 10, 30, 6);
  const auto time_independent = Query(
      TransmissionId{65},
      NodeId{5},
      10,
      30,
      0,
      20'000.0,
      2'000.0,
      SimTime::FromNanoseconds(9'000'000'000));
  if(!exact_no_arrival || !exact_signal || !nearest_no_arrival ||
     !lower_tie_no_arrival || !nearest_signal || !time_independent) {
    return false;
  }

  const auto no_arrival = provider->Query(*exact_no_arrival);
  const auto signal = provider->Query(*exact_signal);
  const auto nearest_none = provider->Query(*nearest_no_arrival);
  const auto tie_none = provider->Query(*lower_tie_no_arrival);
  const auto nearest_response = provider->Query(*nearest_signal);
  const auto later_none = provider->Query(*time_independent);
  if(!no_arrival || !signal || !nearest_none || !tie_none ||
     !nearest_response || !later_none) {
    return false;
  }
  const auto* exact_none = std::get_if<ChannelNoArrival>(&*no_arrival);
  const auto* exact_response = std::get_if<ChannelFieldResponse>(&*signal);
  const auto* near_none = std::get_if<ChannelNoArrival>(&*nearest_none);
  const auto* tie = std::get_if<ChannelNoArrival>(&*tie_none);
  const auto* near_response =
      std::get_if<ChannelFieldResponse>(&*nearest_response);
  const auto* later = std::get_if<ChannelNoArrival>(&*later_none);
  return exact_none != nullptr && exact_response != nullptr &&
         near_none != nullptr && tie != nullptr &&
         near_response != nullptr && later != nullptr &&
         exact_none->transmission_id() == TransmissionId{60} &&
         exact_none->receiver_node_id() == NodeId{0} &&
         exact_response->aggregate_transmission_loss_db() == 91.0 &&
         exact_response->first_arrival_delay() ==
             SimDuration::FromNanoseconds(123) &&
         near_response->aggregate_transmission_loss_db() == 91.0 &&
         near_response->first_arrival_delay() ==
             SimDuration::FromNanoseconds(123) &&
         near_response->paths().size() == 1U &&
         near_response->paths().front().pressure_gain_linear() == 0.25 &&
         later->transmission_id() == TransmissionId{65} &&
         later->receiver_node_id() == NodeId{5};
}

auto TestProviderOwnershipAndConfigurationValidation() -> bool {
  auto frame = EnvironmentCoordinateFrame::Create(
      0.0, VerticalAxisDirection::kPositiveUp);
  auto policy = DiscreteFrequencySelectionPolicy::Create(0.0);
  if(!frame || !policy) return false;
  const auto null_provider = AcousticFieldChannelProvider::Create(
      {}, *policy);
  const auto negative_policy =
      DiscreteFrequencySelectionPolicy::Create(-1.0);
  return !null_provider && !negative_policy;
}

}  // namespace

auto main() -> int {
  return TestCoordinateMappingAndHorizontalRange() &&
                 TestFrequencySelectionAndBandwidthIndependence() &&
                 TestScalarInterpolationAtCornerEdgeFaceAndInterior() &&
                 TestDelayRoundingToNearestNanosecond() &&
                 TestSingletonSpatialAxesRequireExactCoordinates() &&
                 TestOutOfDomainDoesNotClampOrExtrapolate() &&
                 TestNearestCellPathsAndLowerTie() &&
                 TestStaticTimeAndIdentitySemantics() &&
                 TestCoverageClassificationAndMixedStencilPolicy() &&
                 TestProviderOwnershipAndConfigurationValidation()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
