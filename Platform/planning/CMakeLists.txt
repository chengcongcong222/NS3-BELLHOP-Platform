platform_add_interface_module(planning)

# Explicit trusted source registration. Never scan arbitrary server uploads.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PLATFORM_MAC_PLUGIN_DIRS "" CACHE STRING "Additional trusted planning MAC package directories")
set(mac_packages "${PROJECT_SOURCE_DIR}/algorithms/adaptive_tdma" ${PLATFORM_MAC_PLUGIN_DIRS})
foreach(package IN LISTS mac_packages)
  file(GLOB_RECURSE package_inputs CONFIGURE_DEPENDS "${package}/*")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${package_inputs})
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/algorithms/compile_catalog.py")
set(algorithm_header "${CMAKE_CURRENT_BINARY_DIR}/generated/ns3_factory/planning/registered_algorithms.hpp")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/algorithms/compile_catalog.py"
  --output "${algorithm_header}" --cmake-output "${CMAKE_CURRENT_BINARY_DIR}/algorithm_sources.cmake"
  ${mac_packages} COMMAND_ERROR_IS_FATAL ANY)
include("${CMAKE_CURRENT_BINARY_DIR}/algorithm_sources.cmake")
if(PLATFORM_ALGORITHM_SOURCES)
  add_library(platform_algorithm_sources STATIC ${PLATFORM_ALGORITHM_SOURCES})
  target_include_directories(platform_algorithm_sources PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
    ${PLATFORM_ALGORITHM_INCLUDES})
  target_link_libraries(platform_algorithm_sources PUBLIC Platform::contracts)
  target_link_libraries(platform_planning INTERFACE platform_algorithm_sources)
endif()

target_include_directories(
  platform_planning
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_BINARY_DIR}/generated"
            ${PLATFORM_ALGORITHM_INCLUDES}
)
target_link_libraries(platform_planning INTERFACE Platform::contracts)

if(BUILD_TESTING)
  add_executable(platform_channel_aware_example_test
    "${PROJECT_SOURCE_DIR}/algorithms/examples/channel_aware/tests/ordering.cpp")
  target_include_directories(platform_channel_aware_example_test PRIVATE
    "${PROJECT_SOURCE_DIR}/algorithms/examples/channel_aware")
  target_link_libraries(platform_channel_aware_example_test PRIVATE Platform::planning)
  add_test(NAME platform_channel_aware_example_test COMMAND platform_channel_aware_example_test)
  add_executable(platform_flow_planning_test tests/flow_planning_test.cpp)
  target_include_directories(platform_flow_planning_test PRIVATE "${PROJECT_SOURCE_DIR}/runtime")
  target_link_libraries(platform_flow_planning_test PRIVATE Platform::planning)
  add_test(NAME platform_flow_planning_test COMMAND platform_flow_planning_test)
  add_test(NAME platform_algorithm_xml_catalog_test
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/algorithms/test_compile_catalog.py")
  add_executable(platform_planning_adaptive_planning_test tests/adaptive_planning_test.cpp)
  target_link_libraries(platform_planning_adaptive_planning_test PRIVATE Platform::planning)
  add_test(NAME platform_planning_adaptive_planning_test COMMAND platform_planning_adaptive_planning_test)

  add_executable(
    platform_planning_direct_to_sink_routing_planner_test
    tests/direct_to_sink_routing_planner_test.cpp
  )
  target_link_libraries(
    platform_planning_direct_to_sink_routing_planner_test
    PRIVATE Platform::planning
  )
  add_test(
    NAME platform_planning_direct_to_sink_routing_planner_test
    COMMAND platform_planning_direct_to_sink_routing_planner_test
  )

  add_executable(
    platform_planning_shortest_path_to_sink_routing_planner_test
    tests/shortest_path_to_sink_routing_planner_test.cpp
  )
  target_link_libraries(
    platform_planning_shortest_path_to_sink_routing_planner_test
    PRIVATE Platform::planning
  )
  add_test(
    NAME platform_planning_shortest_path_to_sink_routing_planner_test
    COMMAND platform_planning_shortest_path_to_sink_routing_planner_test
  )

  add_executable(
    platform_planning_configured_tdma_mac_planner_test
    tests/configured_tdma_mac_planner_test.cpp
  )
  target_link_libraries(
    platform_planning_configured_tdma_mac_planner_test
    PRIVATE Platform::planning
  )
  add_test(
    NAME platform_planning_configured_tdma_mac_planner_test
    COMMAND platform_planning_configured_tdma_mac_planner_test
  )

  add_executable(
    platform_planning_composite_protocol_cycle_planner_test
    tests/composite_protocol_cycle_planner_test.cpp
  )
  target_link_libraries(
    platform_planning_composite_protocol_cycle_planner_test
    PRIVATE Platform::planning
  )
  add_test(
    NAME platform_planning_composite_protocol_cycle_planner_test
    COMMAND platform_planning_composite_protocol_cycle_planner_test
  )
endif()
