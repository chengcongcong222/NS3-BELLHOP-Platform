#include <cstdlib>
#include <expected>
#include <string>
#include <type_traits>

#include <ns3_factory/contracts/errors.hpp>

using ns3_factory::contracts::Error;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::Status;

static_assert(
    std::is_same_v<Result<int>, std::expected<int, Error>>);
static_assert(std::is_same_v<Status, std::expected<void, Error>>);
static_assert(ErrorCode::kInvalidArgument != ErrorCode::kNotFound);

int main() {
  const Result<int> success{42};
  const Result<int> failure{
      std::unexpected(Error{ErrorCode::kInvalidArgument, "invalid value"})};
  const Status ok{};
  const Status unavailable{
      std::unexpected(Error{ErrorCode::kUnavailable, "service unavailable"})};

  if(!success || *success != 42 || failure ||
     failure.error().code != ErrorCode::kInvalidArgument) {
    return EXIT_FAILURE;
  }
  if(!ok || unavailable ||
     unavailable.error().message != std::string{"service unavailable"}) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
