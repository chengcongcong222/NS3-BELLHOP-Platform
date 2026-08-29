add_library(platform_third_party_nlohmann_json INTERFACE)
add_library(
  Platform::third_party_nlohmann_json
  ALIAS platform_third_party_nlohmann_json
)

target_include_directories(
  platform_third_party_nlohmann_json
  INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json/include>"
)
