platform_add_interface_module(structure)

target_include_directories(
  platform_structure
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(platform_structure INTERFACE Platform::contracts)

if(BUILD_TESTING)
  add_executable(
    platform_structure_connectivity_builder_test
    tests/connectivity_builder_test.cpp
  )
  target_link_libraries(
    platform_structure_connectivity_builder_test
    PRIVATE Platform::structure
  )
  add_test(
    NAME platform_structure_connectivity_builder_test
    COMMAND platform_structure_connectivity_builder_test
  )

  add_executable(
    platform_structure_role_assignment_policy_test
    tests/role_assignment_policy_test.cpp
  )
  target_link_libraries(
    platform_structure_role_assignment_policy_test
    PRIVATE Platform::structure
  )
  add_test(
    NAME platform_structure_role_assignment_policy_test
    COMMAND platform_structure_role_assignment_policy_test
  )

  add_executable(
    platform_structure_logical_topology_policy_test
    tests/logical_topology_policy_test.cpp
  )
  target_link_libraries(
    platform_structure_logical_topology_policy_test
    PRIVATE Platform::structure
  )
  add_test(
    NAME platform_structure_logical_topology_policy_test
    COMMAND platform_structure_logical_topology_policy_test
  )

  add_executable(
    platform_structure_builder_test
    tests/structure_builder_test.cpp
  )
  target_link_libraries(
    platform_structure_builder_test
    PRIVATE Platform::structure
  )
  add_test(
    NAME platform_structure_builder_test
    COMMAND platform_structure_builder_test
  )
endif()
