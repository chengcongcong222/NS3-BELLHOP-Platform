platform_add_interface_module(worker)

target_include_directories(
  platform_worker
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
  platform_worker
  INTERFACE Platform::application
)

add_library(platform_worker_json_codec STATIC codec/json_codec.cpp)
add_library(Platform::worker_json_codec ALIAS platform_worker_json_codec)
target_include_directories(
  platform_worker_json_codec
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/codec/include"
)
target_link_libraries(
  platform_worker_json_codec
  PUBLIC Platform::worker
  PRIVATE Platform::third_party_nlohmann_json
)

add_library(
  platform_worker_process_controller STATIC
  adapter/process_controller.cpp
)
add_library(
  Platform::worker_process_controller
  ALIAS platform_worker_process_controller
)
target_include_directories(
  platform_worker_process_controller
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/adapter/include"
)
target_link_libraries(
  platform_worker_process_controller
  PUBLIC Platform::worker Platform::worker_json_codec
)

if(BUILD_TESTING)
  add_executable(
    platform_worker_json_dependency_test
    codec/tests/json_dependency_test.cpp
  )
  target_link_libraries(
    platform_worker_json_dependency_test
    PRIVATE Platform::third_party_nlohmann_json
  )
  add_test(
    NAME platform_worker_json_dependency_test
    COMMAND platform_worker_json_dependency_test
  )
  add_executable(
    platform_worker_json_codec_test
    codec/tests/json_codec_test.cpp
  )
  target_link_libraries(
    platform_worker_json_codec_test
    PRIVATE Platform::worker_json_codec
  )
  add_test(
    NAME platform_worker_json_codec_test
    COMMAND platform_worker_json_codec_test
  )
  add_executable(
    platform_worker_wire_golden_fixture
    tests/worker_wire_golden_fixture.cpp
  )
  target_link_libraries(
    platform_worker_wire_golden_fixture
    PRIVATE Platform::worker_json_codec
  )
  add_executable(
    platform_worker_test_asset_builder
    tests/worker_test_asset_builder.cpp
  )
  target_include_directories(
    platform_worker_test_asset_builder
    PRIVATE "${PROJECT_SOURCE_DIR}/environment"
  )
  target_link_libraries(
    platform_worker_test_asset_builder
    PRIVATE Platform::environment
  )
endif()

if(PLATFORM_NS3_AVAILABLE)
  add_executable(platform_sim_worker main.cpp)
  target_include_directories(
    platform_sim_worker
    PRIVATE
      "${PROJECT_SOURCE_DIR}/worker"
      "${PROJECT_SOURCE_DIR}/application"
      "${PROJECT_SOURCE_DIR}/assembly"
      "${PROJECT_SOURCE_DIR}/environment"
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/phy"
      "${PROJECT_SOURCE_DIR}/planning"
      "${PROJECT_SOURCE_DIR}/runtime"
      "${PROJECT_SOURCE_DIR}/structure"
  )
  target_link_libraries(
    platform_sim_worker
    PRIVATE
      Platform::worker
      Platform::worker_json_codec
      Platform::assembly
      Platform::environment
  )

  if(BUILD_TESTING)
    add_executable(
      platform_worker_crash_fixture
      tests/worker_crash_fixture.cpp
    )
    add_executable(
      platform_worker_eof_fixture
      tests/worker_eof_fixture.cpp
    )
    add_executable(
      platform_worker_failed_exit_zero_fixture
      tests/worker_failed_exit_zero_fixture.cpp
    )
    target_link_libraries(
      platform_worker_failed_exit_zero_fixture
      PRIVATE Platform::worker_json_codec
    )
    add_executable(
      platform_worker_completed_nonzero_fixture
      tests/worker_completed_nonzero_fixture.cpp
    )
    target_link_libraries(
      platform_worker_completed_nonzero_fixture
      PRIVATE Platform::worker_json_codec
    )
    add_executable(
      platform_simulation_worker_boundary_test
      tests/simulation_worker_boundary_test.cpp
    )
    target_include_directories(
      platform_simulation_worker_boundary_test
      PRIVATE
        "${PROJECT_SOURCE_DIR}/worker"
        "${PROJECT_SOURCE_DIR}/application"
        "${PROJECT_SOURCE_DIR}/assembly"
        "${PROJECT_SOURCE_DIR}/environment"
        "${PROJECT_SOURCE_DIR}/kernel"
        "${PROJECT_SOURCE_DIR}/phy"
        "${PROJECT_SOURCE_DIR}/planning"
        "${PROJECT_SOURCE_DIR}/runtime"
        "${PROJECT_SOURCE_DIR}/structure"
    )
    target_link_libraries(
      platform_simulation_worker_boundary_test
      PRIVATE
        Platform::worker
        Platform::worker_process_controller
        Platform::assembly
        Platform::environment
    )
    target_compile_definitions(
      platform_simulation_worker_boundary_test
      PRIVATE
        PLATFORM_SIM_WORKER_PATH="$<TARGET_FILE:platform_sim_worker>"
        PLATFORM_WORKER_CRASH_FIXTURE_PATH="$<TARGET_FILE:platform_worker_crash_fixture>"
        PLATFORM_WORKER_EOF_FIXTURE_PATH="$<TARGET_FILE:platform_worker_eof_fixture>"
        PLATFORM_WORKER_FAILED_EXIT_ZERO_FIXTURE_PATH="$<TARGET_FILE:platform_worker_failed_exit_zero_fixture>"
        PLATFORM_WORKER_COMPLETED_NONZERO_FIXTURE_PATH="$<TARGET_FILE:platform_worker_completed_nonzero_fixture>"
    )
    add_dependencies(
      platform_simulation_worker_boundary_test
      platform_sim_worker
      platform_worker_crash_fixture
      platform_worker_eof_fixture
      platform_worker_failed_exit_zero_fixture
      platform_worker_completed_nonzero_fixture
    )
    add_test(
      NAME platform_simulation_worker_boundary_test
      COMMAND platform_simulation_worker_boundary_test
    )
  endif()
endif()
