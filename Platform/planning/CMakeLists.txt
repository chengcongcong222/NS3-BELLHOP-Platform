platform_add_interface_module(planning)

target_include_directories(
  platform_planning
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(platform_planning INTERFACE Platform::contracts)

if(BUILD_TESTING)
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
