#include <cmath>
#include <cstdlib>

#include "internal/bathymetry_profile.hpp"
#include "internal/environment_profile.hpp"

using namespace ns3_factory;

namespace {

auto Check(bool condition) -> void {
  if(!condition) {
    std::abort();
  }
}

}  // namespace

int main() {
  auto region = environment::GeographicRegion::Create(-5.0, 170.0,
                                                       5.0, -170.0);
  Check(region.has_value());
  Check(region->Contains(0.0, 175.0));
  Check(region->Contains(0.0, -175.0));
  Check(!region->Contains(0.0, 0.0));

  auto hydrographic = environment::HydrographicProfile::Create(
      {{0.0, 10.0, 35.0}, {100.0, 8.0, 34.0}});
  Check(hydrographic.has_value());
  environment::MackenzieSoundSpeedModel model;
  auto sound_speed = model.Compute(*hydrographic);
  Check(sound_speed.has_value());
  Check(sound_speed->samples().size() == 2);
  // Golden value from the legacy Mackenzie implementation at T=10 C,
  // S=35 PSU, depth=0 m.
  Check(std::abs(sound_speed->samples().front().speed_meters_per_second -
                 1489.8034) < 1e-9);

  auto query = environment::EnvironmentSourceQuery::Create(
      0.0, 175.0, "fixture-annual");
  Check(query.has_value());
  auto source = environment::ManualEnvironmentSource::Create(
      *region, "fixture-annual", *hydrographic);
  Check(source.has_value());
  Check(source->LoadHydrographicProfile(*query).has_value());
  auto outside_query = environment::EnvironmentSourceQuery::Create(
      0.0, 0.0, "fixture-annual");
  Check(outside_query.has_value());
  const auto outside = source->LoadHydrographicProfile(*outside_query);
  Check(!outside.has_value());
  Check(outside.error().code == contracts::ErrorCode::kOutOfRange);

  auto bathymetry = environment::BathymetryProfile::Create(
      {0.0, 1000.0}, {200.0, 250.0});
  Check(bathymetry.has_value());
  auto bathymetry_source = environment::ManualBathymetrySource::Create(
      *region, "fixture-annual", *bathymetry);
  Check(bathymetry_source.has_value());
  Check(bathymetry_source->LoadBathymetryProfile(*query).has_value());

  auto wrong_time_query = environment::EnvironmentSourceQuery::Create(
      0.0, 175.0, "fixture-winter");
  Check(wrong_time_query.has_value());
  const auto wrong_time =
      bathymetry_source->LoadBathymetryProfile(*wrong_time_query);
  Check(!wrong_time.has_value());
  Check(wrong_time.error().code == contracts::ErrorCode::kNotFound);

  return EXIT_SUCCESS;
}
