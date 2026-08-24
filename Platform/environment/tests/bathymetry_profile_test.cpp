#include <cmath>
#include <cstdlib>

#include "internal/bathymetry_profile.hpp"

using ns3_factory::contracts::ErrorCode;
using ns3_factory::environment::BathymetryProfile;

namespace {
auto Check(bool condition) -> void {
  if(!condition) {
    std::abort();
  }
}
}  // namespace

int main() {
  auto profile = BathymetryProfile::Create({0.0, 100.0, 200.0},
                                           {80.0, 100.0, 60.0});
  Check(profile.has_value());
  const auto interpolated = profile->DepthAt(50.0);
  Check(interpolated.has_value());
  Check(std::abs(*interpolated - 90.0) < 1e-12);

  const auto outside = profile->DepthAt(201.0);
  Check(!outside.has_value());
  Check(outside.error().code == ErrorCode::kOutOfRange);

  const auto clear = profile->IsGeometricallyBlocked(200.0, 10.0, 10.0, 20);
  Check(clear.has_value() && !*clear);
  const auto blocked =
      profile->IsGeometricallyBlocked(200.0, 70.0, 70.0, 20);
  Check(blocked.has_value() && *blocked);

  const auto invalid = BathymetryProfile::Create({0.0, 0.0}, {10.0, 20.0});
  Check(!invalid.has_value());
  return EXIT_SUCCESS;
}
