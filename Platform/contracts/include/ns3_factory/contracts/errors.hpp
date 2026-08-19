#pragma once

#include <expected>
#include <string>

#include <ns3_factory/contracts/common.hpp>

namespace ns3_factory::contracts {

enum class ErrorCode : std::uint16_t {
  kInvalidArgument = 1,
  kOutOfRange = 2,
  kOverflow = 3,
  kNotFound = 4,
  kAlreadyExists = 5,
  kFailedPrecondition = 6,
  kUnsupported = 7,
  kUnavailable = 8,
  kInternal = 9,
};

struct Error final {
  ErrorCode code;
  std::string message;

  auto operator==(const Error&) const -> bool = default;
};

template <typename T>
using Result = std::expected<T, Error>;

using Status = Result<void>;

}  // namespace ns3_factory::contracts
