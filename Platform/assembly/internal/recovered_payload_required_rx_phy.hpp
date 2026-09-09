#pragma once

#include <ns3_factory/contracts/rx_phy.hpp>

namespace ns3_factory::assembly::internal {

// Waveform composition requires receiver-owned bytes. The wrapped provider
// must outlive this adapter; scalar composition deliberately does not use it.
class RecoveredPayloadRequiredRxPhy final : public contracts::IRxPhy {
 public:
  explicit RecoveredPayloadRequiredRxPhy(const contracts::IRxPhy& inner) noexcept
      : inner_(inner) {}

  [[nodiscard]] auto Decode(const contracts::RxDecodeRequest& request) const
      -> contracts::Result<contracts::RxDecodeResult> override {
    auto result = inner_.Decode(request);
    if(result && result->outcome() == contracts::DecodeOutcome::kDecoded &&
       (!result->recovered_payload() || result->recovered_payload()->empty())) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kFailedPrecondition,
          "Waveform decoded result requires nonempty recovered payload"});
    }
    return result;
  }

 private:
  const contracts::IRxPhy& inner_;
};

}  // namespace ns3_factory::assembly::internal
