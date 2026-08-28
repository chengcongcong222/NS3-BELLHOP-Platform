#include <cmath>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include <ns3_factory/contracts/rx_quality.hpp>

namespace {

using namespace ns3_factory::contracts;

static_assert(!std::is_default_constructible_v<RxQualityEvidence>);
static_assert(std::is_enum_v<RxQualityEvidenceSource>);
static_assert(static_cast<std::uint8_t>(RxQualityEvidenceSource::kModeled) ==
              1);
static_assert(static_cast<std::uint8_t>(RxQualityEvidenceSource::kMeasured) ==
              2);
static_assert(static_cast<std::uint8_t>(RxQualityEvidenceSource::kExternal) ==
              3);

auto TestValidSourcesAndValues() -> bool {
  const auto modeled = RxQualityEvidence::Create(
      10.0, 20.0, 1.0e-5, RxQualityEvidenceSource::kModeled);
  const auto measured = RxQualityEvidence::Create(
      -3.0, 4.0, 0.5, RxQualityEvidenceSource::kMeasured);
  const auto external = RxQualityEvidence::Create(
      0.0, 0.0, 0.0, RxQualityEvidenceSource::kExternal);
  return modeled && measured && external &&
         modeled->signal_to_noise_ratio_db() == 10.0 &&
         modeled->eb_n0_db() == 20.0 &&
         modeled->bit_error_rate() == 1.0e-5 &&
         modeled->source() == RxQualityEvidenceSource::kModeled;
}

auto TestInvalidValues() -> bool {
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto infinity = std::numeric_limits<double>::infinity();
  return !RxQualityEvidence::Create(
             nan, 0.0, 0.0, RxQualityEvidenceSource::kModeled) &&
         !RxQualityEvidence::Create(
             0.0, infinity, 0.0, RxQualityEvidenceSource::kModeled) &&
         !RxQualityEvidence::Create(
             0.0, 0.0, nan, RxQualityEvidenceSource::kModeled) &&
         !RxQualityEvidence::Create(
             0.0, 0.0, -0.1, RxQualityEvidenceSource::kModeled) &&
         !RxQualityEvidence::Create(
             0.0, 0.0, 1.1, RxQualityEvidenceSource::kModeled) &&
         !RxQualityEvidence::Create(
             0.0,
             0.0,
             0.0,
             static_cast<RxQualityEvidenceSource>(0));
}

}  // namespace

auto main() -> int {
  return TestValidSourcesAndValues() && TestInvalidValues() ? EXIT_SUCCESS
                                                            : EXIT_FAILURE;
}
