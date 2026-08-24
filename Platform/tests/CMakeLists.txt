set(platform_expected_targets
  platform_contracts
  platform_kernel
  platform_runtime
  platform_structure
  platform_planning
  platform_phy
  platform_environment
  platform_adapters
  platform_assembly
  platform_observability
  platform_algorithms
)

foreach(target_name IN LISTS platform_expected_targets)
  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR "Required Platform module target is missing: ${target_name}")
  endif()
endforeach()

add_executable(platform_build_smoke_test build_smoke_test.cpp)
target_link_libraries(platform_build_smoke_test PRIVATE Platform::contracts)

add_test(NAME platform_build_smoke_test COMMAND platform_build_smoke_test)

if(PLATFORM_NS3_AVAILABLE)
  add_executable(
    platform_ns3_signal_lifecycle_integration_test
    ns3_signal_lifecycle_integration_test.cpp
  )
  target_include_directories(
    platform_ns3_signal_lifecycle_integration_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/runtime"
  )
  target_link_libraries(
    platform_ns3_signal_lifecycle_integration_test
    PRIVATE
      Platform::kernel
      Platform::runtime
  )
  add_test(
    NAME platform_ns3_signal_lifecycle_integration_test
    COMMAND platform_ns3_signal_lifecycle_integration_test
  )

  add_executable(
    platform_ns3_plan_driven_cycle_integration_test
    ns3_plan_driven_cycle_integration_test.cpp
  )
  target_include_directories(
    platform_ns3_plan_driven_cycle_integration_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/runtime"
  )
  target_link_libraries(
    platform_ns3_plan_driven_cycle_integration_test
    PRIVATE
      Platform::kernel
      Platform::runtime
  )
  add_test(
    NAME platform_ns3_plan_driven_cycle_integration_test
    COMMAND platform_ns3_plan_driven_cycle_integration_test
  )

  add_executable(
    platform_ns3_plan_bound_tx_runtime_integration_test
    ns3_plan_bound_tx_runtime_integration_test.cpp
  )
  target_include_directories(
    platform_ns3_plan_bound_tx_runtime_integration_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/runtime"
  )
  target_link_libraries(
    platform_ns3_plan_bound_tx_runtime_integration_test
    PRIVATE
      Platform::kernel
      Platform::runtime
  )
  add_test(
    NAME platform_ns3_plan_bound_tx_runtime_integration_test
    COMMAND platform_ns3_plan_bound_tx_runtime_integration_test
  )

  add_executable(
    platform_ns3_relay_enqueue_integration_test
    ns3_relay_enqueue_integration_test.cpp
  )
  target_include_directories(
    platform_ns3_relay_enqueue_integration_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/runtime"
  )
  target_link_libraries(
    platform_ns3_relay_enqueue_integration_test
    PRIVATE
      Platform::kernel
      Platform::runtime
  )
  add_test(
    NAME platform_ns3_relay_enqueue_integration_test
    COMMAND platform_ns3_relay_enqueue_integration_test
  )

  add_executable(
    platform_ns3_multi_cycle_multihop_integration_test
    ns3_multi_cycle_multihop_integration_test.cpp
  )
  target_include_directories(
    platform_ns3_multi_cycle_multihop_integration_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/planning"
      "${PROJECT_SOURCE_DIR}/runtime"
      "${PROJECT_SOURCE_DIR}/structure"
  )
  target_link_libraries(
    platform_ns3_multi_cycle_multihop_integration_test
    PRIVATE
      Platform::kernel
      Platform::planning
      Platform::runtime
      Platform::structure
  )
  add_test(
    NAME platform_ns3_multi_cycle_multihop_integration_test
    COMMAND platform_ns3_multi_cycle_multihop_integration_test
  )

  add_executable(
    platform_ns3_scenario_runtime_multihop_integration_test
    ns3_scenario_runtime_multihop_integration_test.cpp
  )
  target_include_directories(
    platform_ns3_scenario_runtime_multihop_integration_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/assembly"
      "${PROJECT_SOURCE_DIR}/assembly/tests"
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/planning"
      "${PROJECT_SOURCE_DIR}/runtime"
      "${PROJECT_SOURCE_DIR}/structure"
  )
  target_link_libraries(
    platform_ns3_scenario_runtime_multihop_integration_test
    PRIVATE Platform::assembly
  )
  add_test(
    NAME platform_ns3_scenario_runtime_multihop_integration_test
    COMMAND platform_ns3_scenario_runtime_multihop_integration_test
  )

  add_executable(
    platform_ns3_scenario_runtime_m5_tx_integration_test
    ns3_scenario_runtime_m5_tx_integration_test.cpp
  )
  target_include_directories(
    platform_ns3_scenario_runtime_m5_tx_integration_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/assembly"
      "${PROJECT_SOURCE_DIR}/assembly/tests"
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/planning"
      "${PROJECT_SOURCE_DIR}/runtime"
      "${PROJECT_SOURCE_DIR}/structure"
  )
  target_link_libraries(
    platform_ns3_scenario_runtime_m5_tx_integration_test
    PRIVATE
      Platform::assembly
      Platform::phy
  )
  add_test(
    NAME platform_ns3_scenario_runtime_m5_tx_integration_test
    COMMAND platform_ns3_scenario_runtime_m5_tx_integration_test
  )

  add_executable(
    platform_ns3_scenario_runtime_acoustic_field_integration_test
    ns3_scenario_runtime_acoustic_field_integration_test.cpp
  )
  target_include_directories(
    platform_ns3_scenario_runtime_acoustic_field_integration_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/assembly"
      "${PROJECT_SOURCE_DIR}/assembly/tests"
      "${PROJECT_SOURCE_DIR}/environment"
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/phy"
      "${PROJECT_SOURCE_DIR}/planning"
      "${PROJECT_SOURCE_DIR}/runtime"
      "${PROJECT_SOURCE_DIR}/structure"
  )
  target_link_libraries(
    platform_ns3_scenario_runtime_acoustic_field_integration_test
    PRIVATE
      Platform::assembly
      Platform::environment
      Platform::phy
  )
  add_test(
    NAME platform_ns3_scenario_runtime_acoustic_field_integration_test
    COMMAND platform_ns3_scenario_runtime_acoustic_field_integration_test
  )

  add_executable(
    platform_ns3_bellhop_import_scenario_runtime_integration_test
    ns3_bellhop_import_scenario_runtime_integration_test.cpp
  )
  target_include_directories(
    platform_ns3_bellhop_import_scenario_runtime_integration_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/assembly"
      "${PROJECT_SOURCE_DIR}/assembly/tests"
      "${PROJECT_SOURCE_DIR}/environment"
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/phy"
      "${PROJECT_SOURCE_DIR}/planning"
      "${PROJECT_SOURCE_DIR}/runtime"
      "${PROJECT_SOURCE_DIR}/structure"
  )
  target_link_libraries(
    platform_ns3_bellhop_import_scenario_runtime_integration_test
    PRIVATE
      Platform::assembly
      Platform::environment
      Platform::phy
  )
  add_test(
    NAME platform_ns3_bellhop_import_scenario_runtime_integration_test
    COMMAND platform_ns3_bellhop_import_scenario_runtime_integration_test
  )
endif()
