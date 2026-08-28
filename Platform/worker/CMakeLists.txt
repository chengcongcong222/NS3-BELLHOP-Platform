platform_add_interface_module(worker)

target_include_directories(
  platform_worker
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
  platform_worker
  INTERFACE Platform::application
)

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
      Platform::assembly
      Platform::environment
  )

  if(BUILD_TESTING)
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
        Platform::assembly
        Platform::environment
    )
    target_compile_definitions(
      platform_simulation_worker_boundary_test
      PRIVATE
        PLATFORM_SIM_WORKER_PATH="$<TARGET_FILE:platform_sim_worker>"
    )
    add_dependencies(
      platform_simulation_worker_boundary_test
      platform_sim_worker
    )
    add_test(
      NAME platform_simulation_worker_boundary_test
      COMMAND platform_simulation_worker_boundary_test
    )
  endif()
endif()
