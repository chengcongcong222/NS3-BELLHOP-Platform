#pragma once

#include <compare>
#include <string>
#include <string_view>
#include <utility>

#include <ns3_factory/contracts/errors.hpp>

namespace ns3_factory::application {

namespace detail {

[[nodiscard]] inline auto ValidateStableIdText(std::string_view value)
    -> contracts::Status {
  if(value.empty() || value.size() > 128U) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Application ID length must be within [1, 128]"});
  }
  const auto is_alphanumeric = [](char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9');
  };
  if(!is_alphanumeric(value.front())) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Application ID must begin with an alphanumeric "
                         "byte"});
  }
  for(const auto character : value) {
    if(!is_alphanumeric(character) && character != '-' &&
       character != '_' && character != '.') {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Application ID contains an invalid byte"});
    }
  }
  return {};
}

template <typename Tag>
class StringId final {
 public:
  [[nodiscard]] static auto Create(std::string value)
      -> contracts::Result<StringId> {
    const auto valid = ValidateStableIdText(value);
    if(!valid) return std::unexpected(valid.error());
    return StringId{std::move(value)};
  }

  [[nodiscard]] constexpr auto value() const noexcept
      -> const std::string& {
    return value_;
  }

  auto operator<=>(const StringId&) const = default;

 private:
  explicit StringId(std::string value) : value_(std::move(value)) {}

  std::string value_;
};

struct ScenarioIdTag final {};
struct ExperimentIdTag final {};
struct RunIdTag final {};

}  // namespace detail

using ScenarioId = detail::StringId<detail::ScenarioIdTag>;
using ExperimentId = detail::StringId<detail::ExperimentIdTag>;
using RunId = detail::StringId<detail::RunIdTag>;

}  // namespace ns3_factory::application
