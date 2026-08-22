#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>

#include "internal/transmission_record.hpp"

namespace ns3_factory::runtime::internal {

class TransmissionRecordStore final {
 public:
  [[nodiscard]] auto Register(TransmissionRecord record)
      -> contracts::Status;

  // The returned reference is for synchronous use only and must not outlive
  // this store or be retained across Register calls.
  [[nodiscard]] auto Find(contracts::TransmissionId transmission_id) const
      -> contracts::Result<std::reference_wrapper<const TransmissionRecord>>;

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return records_.size();
  }

  auto ClearForCycleClose() noexcept -> void { records_.clear(); }

 private:
  std::vector<TransmissionRecord> records_;
};

inline auto TransmissionRecordStore::Register(TransmissionRecord record)
    -> contracts::Status {
  const auto transmission_id = record.transmission_id();
  const auto position = std::lower_bound(
      records_.begin(),
      records_.end(),
      transmission_id,
      [](const TransmissionRecord& candidate,
         contracts::TransmissionId id) {
        return candidate.transmission_id() < id;
      });
  if(position != records_.end() &&
     position->transmission_id() == transmission_id) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kAlreadyExists,
            "TransmissionId is already registered"});
  }
  records_.insert(position, std::move(record));
  return {};
}

inline auto TransmissionRecordStore::Find(
    contracts::TransmissionId transmission_id) const
    -> contracts::Result<
        std::reference_wrapper<const TransmissionRecord>> {
  const auto record = std::lower_bound(
      records_.begin(),
      records_.end(),
      transmission_id,
      [](const TransmissionRecord& candidate,
         contracts::TransmissionId id) {
        return candidate.transmission_id() < id;
      });
  if(record == records_.end() ||
     record->transmission_id() != transmission_id) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kNotFound,
            "TransmissionRecord was not found"});
  }
  return std::cref(*record);
}

}  // namespace ns3_factory::runtime::internal
