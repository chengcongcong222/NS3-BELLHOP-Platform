#pragma once

#include <ns3_factory/contracts/errors.hpp>

#include "internal/transmission_session.hpp"

namespace ns3_factory::runtime::internal {

// Implemented at an assembly/test boundary. Runtime remains scheduler-free,
// while queue consumption can wait for complete signal eventization.
class ITransmissionSessionEventSink {
 public:
  virtual ~ITransmissionSessionEventSink() = default;

  [[nodiscard]] virtual auto Publish(
      const TransmissionSession& session) -> contracts::Status = 0;
};

}  // namespace ns3_factory::runtime::internal
