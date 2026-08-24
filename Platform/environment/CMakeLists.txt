platform_add_interface_module(environment)

target_include_directories(
  platform_environment
  INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(platform_environment INTERFACE Platform::contracts)

set(
  PLATFORM_BELLHOP_EXECUTABLE
  ""
  CACHE FILEPATH
  "Optional Bellhop executable for the real offline Environment golden test"
)
set(
  PLATFORM_WOSS_ASSET_ROOT
  ""
  CACHE PATH
  "Optional legacy WOSS asset root for the real Environment golden test"
)
set(
  PLATFORM_WOSS_MANIFEST
  ""
  CACHE FILEPATH
  "Optional WOSS source manifest for the real Environment golden test"
)
set(
  PLATFORM_BELLHOP_REFERENCE_ARR
  ""
  CACHE FILEPATH
  "Optional reference ARR for the real Environment golden test"
)
set(
  PLATFORM_RAW_DATA_PYTHON
  ""
  CACHE FILEPATH
  "Optional Python interpreter with numpy/netCDF4 for raw WOA tests"
)
set(
  PLATFORM_WOA_TEMPERATURE_DATASET
  ""
  CACHE FILEPATH
  "Optional WOA temperature NetCDF for the raw Environment import test"
)
set(
  PLATFORM_WOA_SALINITY_DATASET
  ""
  CACHE FILEPATH
  "Optional WOA salinity NetCDF for the raw Environment import test"
)

if(BUILD_TESTING)
  foreach(existing_environment_test IN ITEMS
      acoustic_field_asset
      acoustic_field_channel_provider
      bellhop_ascii_arrival_parser
      bellhop_raw_arrival_bundle
      bellhop_raw_arrival_normalizer
  )
    set(test_target
        "platform_environment_${existing_environment_test}_test")
    add_executable(
      "${test_target}"
      "tests/${existing_environment_test}_test.cpp"
    )
    target_link_libraries("${test_target}" PRIVATE Platform::environment)
    add_test(NAME "${test_target}" COMMAND "${test_target}")
  endforeach()

  foreach(environment_test IN ITEMS
      bathymetry_profile
      bellhop_environment_builder
      environment_asset_repository
      environment_profile
      legacy_asset_loader
      offline_asset_pipeline
  )
    set(test_target "platform_environment_${environment_test}_test")
    add_executable("${test_target}" "tests/${environment_test}_test.cpp")
    target_link_libraries("${test_target}" PRIVATE Platform::environment)
    add_test(NAME "${test_target}" COMMAND "${test_target}")
  endforeach()

  add_executable(
    platform_environment_fake_bellhop
    tests/fake_bellhop.cpp
  )

  add_executable(
    platform_environment_bellhop_process_runner_test
    tests/bellhop_process_runner_test.cpp
  )
  target_link_libraries(
    platform_environment_bellhop_process_runner_test
    PRIVATE Platform::environment
  )
  add_test(
    NAME platform_environment_bellhop_process_runner_test
    COMMAND
      platform_environment_bellhop_process_runner_test
      $<TARGET_FILE:platform_environment_fake_bellhop>
  )

  add_executable(
    platform_environment_woss_cache_loader_test
    tests/woss_cache_loader_test.cpp
  )
  target_link_libraries(
    platform_environment_woss_cache_loader_test
    PRIVATE Platform::environment
  )
  add_test(
    NAME platform_environment_woss_cache_loader_test
    COMMAND
      platform_environment_woss_cache_loader_test
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/woss_cache_fixture"
  )

  add_executable(
    platform_environment_offline_raw_environment_asset_builder_test
    tests/offline_raw_environment_asset_builder_test.cpp
  )
  target_link_libraries(
    platform_environment_offline_raw_environment_asset_builder_test
    PRIVATE Platform::environment
  )
  add_test(
    NAME platform_environment_offline_raw_environment_asset_builder_test
    COMMAND
      platform_environment_offline_raw_environment_asset_builder_test
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/woss_cache_fixture"
  )

  add_executable(
    platform_environment_real_asset_golden_test
    tests/real_asset_golden_test.cpp
  )
  target_link_libraries(
    platform_environment_real_asset_golden_test
    PRIVATE Platform::environment
  )
  if(PLATFORM_BELLHOP_EXECUTABLE
     AND PLATFORM_WOSS_ASSET_ROOT
     AND PLATFORM_WOSS_MANIFEST
     AND PLATFORM_BELLHOP_REFERENCE_ARR)
    add_test(
      NAME platform_environment_real_asset_golden_test
      COMMAND
        platform_environment_real_asset_golden_test
        "${PLATFORM_BELLHOP_EXECUTABLE}"
        "${PLATFORM_WOSS_ASSET_ROOT}"
        "${PLATFORM_WOSS_MANIFEST}"
        "${PLATFORM_BELLHOP_REFERENCE_ARR}"
    )
    set_tests_properties(
      platform_environment_real_asset_golden_test
      PROPERTIES LABELS "integration;bellhop;real-asset"
    )
  endif()

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME platform_environment_raw_import_tool_self_test
      COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_raw_environment.py"
        --self-test
    )
    set_tests_properties(
      platform_environment_raw_import_tool_self_test
      PROPERTIES LABELS "environment;offline-tool"
    )
  endif()

  add_executable(
    platform_environment_raw_environment_import_test
    tests/raw_environment_import_test.cpp
  )
  target_link_libraries(
    platform_environment_raw_environment_import_test
    PRIVATE Platform::environment
  )
  if(PLATFORM_RAW_DATA_PYTHON
     AND PLATFORM_WOA_TEMPERATURE_DATASET
     AND PLATFORM_WOA_SALINITY_DATASET
     AND PLATFORM_BELLHOP_EXECUTABLE)
    add_test(
      NAME platform_environment_raw_environment_import_test
      COMMAND
        platform_environment_raw_environment_import_test
        "${PLATFORM_RAW_DATA_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/import_raw_environment.py"
        "${PLATFORM_WOA_TEMPERATURE_DATASET}"
        "${PLATFORM_WOA_SALINITY_DATASET}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/gebco_response_fixture.json"
        "${PLATFORM_BELLHOP_EXECUTABLE}"
    )
    set_tests_properties(
      platform_environment_raw_environment_import_test
      PROPERTIES LABELS "environment;integration;raw-data;real-asset"
    )
  endif()
endif()
