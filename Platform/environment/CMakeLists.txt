platform_add_interface_module(environment)

target_include_directories(
  platform_environment
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(platform_environment INTERFACE Platform::contracts)

if(BUILD_TESTING)
  add_executable(
    platform_environment_acoustic_field_asset_test
    tests/acoustic_field_asset_test.cpp
  )
  target_link_libraries(
    platform_environment_acoustic_field_asset_test
    PRIVATE Platform::environment
  )
  add_test(
    NAME platform_environment_acoustic_field_asset_test
    COMMAND platform_environment_acoustic_field_asset_test
  )

  add_executable(
    platform_environment_acoustic_field_channel_provider_test
    tests/acoustic_field_channel_provider_test.cpp
  )
  target_link_libraries(
    platform_environment_acoustic_field_channel_provider_test
    PRIVATE Platform::environment
  )
  add_test(
    NAME platform_environment_acoustic_field_channel_provider_test
    COMMAND platform_environment_acoustic_field_channel_provider_test
  )
endif()
