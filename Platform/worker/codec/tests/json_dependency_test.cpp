#include <cstdlib>
#include <string>

#include <nlohmann/json.hpp>

static_assert(NLOHMANN_JSON_VERSION_MAJOR == 3);
static_assert(NLOHMANN_JSON_VERSION_MINOR == 12);
static_assert(NLOHMANN_JSON_VERSION_PATCH == 0);

auto main() -> int {
  const auto parsed = nlohmann::json::parse(
      R"({"offline":true,"schema_version":1})");
  return parsed.at("offline").get<bool>() &&
                 parsed.at("schema_version").get<int>() == 1
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
