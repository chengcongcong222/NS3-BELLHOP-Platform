#pragma once

#include <cmath>
#include <cstdint>

#include <ns3_factory/contracts/errors.hpp>

namespace ns3_factory::contracts {

enum class RxQualityEvidenceSource : std::uint8_t {
  kModeled = 1,
  kMeasured = 2,
  kExternal = 3,
};

class RxQualityEvidence final {
 public:
  [[nodiscard]] static auto Create(double signal_to_noise_ratio_db,
                                   double eb_n0_db,
                                   double bit_error_rate,
                                   RxQualityEvidenceSource source)
      -> Result<RxQualityEvidence>;

  [[nodiscard]] constexpr auto signal_to_noise_ratio_db() const noexcept
      -> double {
    return signal_to_noise_ratio_db_;
  }

  [[nodiscard]] constexpr auto eb_n0_db() const noexcept -> double {
    return eb_n0_db_;
  }

  [[nodiscard]] constexpr auto bit_error_rate() const noexcept -> double {
    return bit_error_rate_;
  }

  [[nodiscard]] constexpr auto source() const noexcept
      -> RxQualityEvidenceSource {
    return source_;
  }

  constexpr auto operator==(const RxQualityEvidence&) const noexcept
      -> bool = default;

 private:
  constexpr RxQualityEvidence(double signal_to_noise_ratio_db,
                              double eb_n0_db,
                              double bit_error_rate,
                              RxQualityEvidenceSource source) noexcept
      : signal_to_noise_ratio_db_(signal_to_noise_ratio_db),
        eb_n0_db_(eb_n0_db),
        bit_error_rate_(bit_error_rate),
        source_(source) {}

  double signal_to_noise_ratio_db_;
  double eb_n0_db_;
  double bit_error_rate_;
  RxQualityEvidenceSource source_;
};

inline auto RxQualityEvidence::Create(double signal_to_noise_ratio_db,
                                      double eb_n0_db,
                                      double bit_error_rate,
                                      RxQualityEvidenceSource source)
    -> Result<RxQualityEvidence> {
  if(!std::isfinite(signal_to_noise_ratio_db) ||
     !std::isfinite(eb_n0_db) || !std::isfinite(bit_error_rate)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "Rx quality scalar values must be finite"});
  }
  if(bit_error_rate < 0.0 || bit_error_rate > 1.0) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "Rx quality bit error rate must be within [0, 1]"});
  }
  switch(source) {
    case RxQualityEvidenceSource::kModeled:
    case RxQualityEvidenceSource::kMeasured:
    case RxQualityEvidenceSource::kExternal:
      break;
    default:
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "Rx quality evidence source is invalid"});
  }
  return RxQualityEvidence{
      signal_to_noise_ratio_db, eb_n0_db, bit_error_rate, source};
}

}  // namespace ns3_factory::contracts
