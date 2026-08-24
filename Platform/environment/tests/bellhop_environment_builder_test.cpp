#include <cstdlib>
#include <string>
#include <utility>

#include "internal/bellhop_environment_builder.hpp"

using namespace ns3_factory;

namespace {

auto Check(bool condition) -> void {
  if(!condition) {
    std::abort();
  }
}

auto Configuration() -> environment::BellhopRunConfiguration {
  return {"builder fixture", "SVW", "A", 0.0, 1600.0, 1.8, 0.8,
          501, 0, -80.0, 80.0, 0.0};
}

}  // namespace

int main() {
  auto sound_speed = environment::SoundSpeedProfile::Create(
      {{0.0, 1540.0}, {100.0, 1505.0}, {200.0, 1500.0}});
  Check(sound_speed.has_value());
  auto bathymetry = environment::BathymetryProfile::Create(
      {0.0, 5000.0, 10000.0}, {200.0, 180.0, 200.0});
  Check(bathymetry.has_value());
  environment::BellhopEnvironmentBuildRequest request{
      12'000.0,
      30.0,
      {10.0, 30.0, 90.0},
      10'000.0,
      200.0,
      std::move(*sound_speed),
      std::move(*bathymetry),
      {0.0, 10'000.0},
      {0.0, 0.0},
      Configuration()};

  environment::BellhopAsciiEnvironmentBuilder builder;
  const auto output = builder.Build(request);
  Check(output.has_value());
  const std::string expected_environment =
      "'builder fixture'\n"
      "12000.0\n"
      "1\n"
      "'SVW'\n"
      "  3  0.0  200.0\n"
      "  0.0  1540.00  /\n"
      "  100.0  1505.00  /\n"
      "  200.0  1500.00  /\n"
      "'A'  0.0\n"
      "  200.0  1600.0  1.80  0.80  /\n"
      "  1\n"
      "  30.0  /\n"
      "  3\n"
      "  10.0  30.0  90.0  /\n"
      "  501\n"
      "  0.0  10.0000  /\n"
      "'A'\n"
      "  0\n"
      "  -80.0  80.0  /\n"
      "  0.0  200.0  10.0000\n";
  Check(output->environment_ascii == expected_environment);
  Check(output->bathymetry_ascii ==
        "'L'\n"
        "  3\n"
        "  0.000000  200.00\n"
        "  5.000000  180.00\n"
        "  10.000000  200.00\n");
  Check(output->surface_ascii ==
        "'L'\n"
        "  2\n"
        "  0.000000  0.00\n"
        "  10.000000  0.00\n");

  auto short_sound_speed = environment::SoundSpeedProfile::Create(
      {{0.0, 1540.0}, {100.0, 1505.0}});
  Check(short_sound_speed.has_value());
  request.sound_speed_profile = std::move(*short_sound_speed);
  const auto incomplete = builder.Build(request);
  Check(!incomplete.has_value());
  Check(incomplete.error().code ==
        contracts::ErrorCode::kFailedPrecondition);

  return EXIT_SUCCESS;
}
