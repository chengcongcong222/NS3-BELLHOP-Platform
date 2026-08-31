platform_add_interface_module(environment)

target_include_directories(
  platform_environment
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(platform_environment INTERFACE Platform::contracts)

add_executable(
  platform_reference_environment_builder
  tools/reference_environment_builder.cpp
)
target_link_libraries(
  platform_reference_environment_builder
  PRIVATE Platform::environment
)

if(BUILD_TESTING)
  find_package(Python3 3.12.3 EXACT COMPONENTS Interpreter REQUIRED)

  add_executable(
    platform_environment_reference_asset_test
    tests/reference_environment_asset_test.cpp
  )
  target_link_libraries(
    platform_environment_reference_asset_test
    PRIVATE Platform::environment
  )
  target_compile_definitions(
    platform_environment_reference_asset_test
    PRIVATE
      PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets/reference_shallow_water_v1"
  )
  add_test(
    NAME platform_environment_reference_asset_test
    COMMAND platform_environment_reference_asset_test
  )
  add_test(
    NAME platform_environment_reference_source_test
    COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_environment_source_test.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference_shallow_water_v1"
  )

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

  add_executable(
    platform_environment_asset_package_test
    tests/environment_asset_package_test.cpp
  )
  target_link_libraries(
    platform_environment_asset_package_test
    PRIVATE Platform::environment
  )
  add_test(
    NAME platform_environment_asset_package_test
    COMMAND platform_environment_asset_package_test
  )

  add_executable(
    platform_environment_asset_repository_test
    tests/environment_asset_repository_test.cpp
  )
  target_link_libraries(
    platform_environment_asset_repository_test
    PRIVATE Platform::environment
  )
  add_test(
    NAME platform_environment_asset_repository_test
    COMMAND platform_environment_asset_repository_test
  )
endif()
