platform_add_interface_module(adapters)

target_include_directories(
  platform_adapters
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
  platform_adapters
  INTERFACE
    Platform::contracts
    Platform::third_party_nlohmann_json
)

if(BUILD_TESTING)
  add_executable(
    platform_adapters_external_cir_channel_provider_test
    tests/external_cir_channel_provider_test.cpp
  )
  target_link_libraries(
    platform_adapters_external_cir_channel_provider_test
    PRIVATE
      Platform::adapters
      Platform::phy
  )
  add_test(
    NAME platform_adapters_external_cir_channel_provider_test
    COMMAND platform_adapters_external_cir_channel_provider_test
  )
endif()
