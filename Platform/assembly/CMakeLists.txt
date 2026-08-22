platform_add_interface_module(assembly)

target_link_libraries(
  platform_assembly
  INTERFACE
    Platform::contracts
    Platform::kernel
    Platform::planning
    Platform::runtime
    Platform::structure
)

if(BUILD_TESTING AND PLATFORM_NS3_AVAILABLE)
  add_executable(
    platform_assembly_ns3_gateway_multirun_test
    tests/ns3_gateway_multirun_test.cpp
  )
  target_include_directories(
    platform_assembly_ns3_gateway_multirun_test
    PRIVATE "${PROJECT_SOURCE_DIR}/kernel"
  )
  target_link_libraries(
    platform_assembly_ns3_gateway_multirun_test
    PRIVATE Platform::assembly
  )
  add_test(
    NAME platform_assembly_ns3_gateway_multirun_test
    COMMAND platform_assembly_ns3_gateway_multirun_test
  )

  add_executable(
    platform_assembly_scenario_runtime_test
    tests/scenario_runtime_test.cpp
  )
  target_include_directories(
    platform_assembly_scenario_runtime_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/assembly"
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/planning"
      "${PROJECT_SOURCE_DIR}/runtime"
      "${PROJECT_SOURCE_DIR}/structure"
  )
  target_link_libraries(
    platform_assembly_scenario_runtime_test
    PRIVATE Platform::assembly
  )
  add_test(
    NAME platform_assembly_scenario_runtime_test
    COMMAND platform_assembly_scenario_runtime_test
  )
endif()
