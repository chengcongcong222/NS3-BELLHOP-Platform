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

  add_executable(
    platform_environment_bellhop_ascii_arrival_parser_test
    tests/bellhop_ascii_arrival_parser_test.cpp
  )
  target_link_libraries(
    platform_environment_bellhop_ascii_arrival_parser_test
    PRIVATE Platform::environment
  )
  add_test(
    NAME platform_environment_bellhop_ascii_arrival_parser_test
    COMMAND platform_environment_bellhop_ascii_arrival_parser_test
  )

  add_executable(
    platform_environment_bellhop_raw_arrival_bundle_test
    tests/bellhop_raw_arrival_bundle_test.cpp
  )
  target_link_libraries(
    platform_environment_bellhop_raw_arrival_bundle_test
    PRIVATE Platform::environment
  )
  add_test(
    NAME platform_environment_bellhop_raw_arrival_bundle_test
    COMMAND platform_environment_bellhop_raw_arrival_bundle_test
  )

  add_executable(
    platform_environment_bellhop_raw_arrival_normalizer_test
    tests/bellhop_raw_arrival_normalizer_test.cpp
  )
  target_link_libraries(
    platform_environment_bellhop_raw_arrival_normalizer_test
    PRIVATE Platform::environment
  )
  add_test(
    NAME platform_environment_bellhop_raw_arrival_normalizer_test
    COMMAND platform_environment_bellhop_raw_arrival_normalizer_test
  )
endif()
