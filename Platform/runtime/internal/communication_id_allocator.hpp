#pragma once

#include <limits>
#include <optional>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>

namespace ns3_factory::runtime::internal {

class CommunicationIdAllocator final {
 public:
  explicit CommunicationIdAllocator(
      contracts::TransmissionId first_transmission_id) noexcept
      : next_transmission_id_(first_transmission_id.value()) {}

  CommunicationIdAllocator(
      contracts::TransmissionId first_transmission_id,
      contracts::ReceptionId first_reception_id) noexcept
      : next_transmission_id_(first_transmission_id.value()),
        next_reception_id_(first_reception_id.value()) {}

  CommunicationIdAllocator(const CommunicationIdAllocator&) = delete;
  auto operator=(const CommunicationIdAllocator&)
      -> CommunicationIdAllocator& = delete;
  CommunicationIdAllocator(CommunicationIdAllocator&&) = delete;
  auto operator=(CommunicationIdAllocator&&)
      -> CommunicationIdAllocator& = delete;

  [[nodiscard]] auto NextTransmissionId()
      -> contracts::Result<contracts::TransmissionId>;

  [[nodiscard]] auto NextReceptionId()
      -> contracts::Result<contracts::ReceptionId>;

 private:
  contracts::TransmissionId::value_type next_transmission_id_;
  bool transmission_ids_exhausted_{false};
  std::optional<contracts::ReceptionId::value_type> next_reception_id_;
  bool reception_ids_exhausted_{false};
};

inline auto CommunicationIdAllocator::NextTransmissionId()
    -> contracts::Result<contracts::TransmissionId> {
  if(transmission_ids_exhausted_) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "TransmissionId allocation exhausted"});
  }

  const auto allocated = contracts::TransmissionId{next_transmission_id_};
  constexpr auto kMaximum =
      std::numeric_limits<contracts::TransmissionId::value_type>::max();
  if(next_transmission_id_ == kMaximum) {
    transmission_ids_exhausted_ = true;
  } else {
    ++next_transmission_id_;
  }
  return allocated;
}

inline auto CommunicationIdAllocator::NextReceptionId()
    -> contracts::Result<contracts::ReceptionId> {
  if(!next_reception_id_) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "ReceptionId allocation was not configured"});
  }
  if(reception_ids_exhausted_) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "ReceptionId allocation exhausted"});
  }

  const auto allocated = contracts::ReceptionId{*next_reception_id_};
  constexpr auto kMaximum =
      std::numeric_limits<contracts::ReceptionId::value_type>::max();
  if(*next_reception_id_ == kMaximum) {
    reception_ids_exhausted_ = true;
  } else {
    ++*next_reception_id_;
  }
  return allocated;
}

}  // namespace ns3_factory::runtime::internal
