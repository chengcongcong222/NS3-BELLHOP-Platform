platform_add_interface_module(trajectory)
target_include_directories(platform_trajectory INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(platform_trajectory INTERFACE Platform::contracts)

if(BUILD_TESTING)
  add_executable(platform_trajectory_asset_test tests/asset_test.cpp)
  target_link_libraries(platform_trajectory_asset_test PRIVATE Platform::trajectory)
  add_test(NAME platform_trajectory_asset_test COMMAND platform_trajectory_asset_test)

  add_executable(platform_trajectory_uuv_5dof_test tests/uuv_5dof_test.cpp)
  target_link_libraries(platform_trajectory_uuv_5dof_test PRIVATE Platform::trajectory)
  add_test(NAME platform_trajectory_uuv_5dof_test COMMAND platform_trajectory_uuv_5dof_test)

  add_executable(platform_trajectory_geometry_test tests/geometry_test.cpp)
  target_include_directories(platform_trajectory_geometry_test PRIVATE
    "${PROJECT_SOURCE_DIR}/assembly" "${PROJECT_SOURCE_DIR}/environment"
    "${PROJECT_SOURCE_DIR}/phy" "${PROJECT_SOURCE_DIR}/runtime")
  target_link_libraries(platform_trajectory_geometry_test PRIVATE Platform::trajectory Platform::assembly)
  target_compile_definitions(platform_trajectory_geometry_test PRIVATE
    PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT="${PROJECT_SOURCE_DIR}/environment/assets/reference_shallow_water_v1")
  add_test(NAME platform_trajectory_geometry_test COMMAND platform_trajectory_geometry_test)
  if(PLATFORM_NS3_AVAILABLE)
    add_executable(platform_ns3_trajectory_geometry_test tests/geometry_test.cpp)
    target_include_directories(platform_ns3_trajectory_geometry_test PRIVATE
      "${PROJECT_SOURCE_DIR}/assembly" "${PROJECT_SOURCE_DIR}/environment"
      "${PROJECT_SOURCE_DIR}/phy" "${PROJECT_SOURCE_DIR}/runtime" "${PROJECT_SOURCE_DIR}/kernel")
    target_link_libraries(platform_ns3_trajectory_geometry_test PRIVATE Platform::trajectory Platform::assembly)
    target_compile_definitions(platform_ns3_trajectory_geometry_test PRIVATE TRAJECTORY_NS3_TEST
      PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT="${PROJECT_SOURCE_DIR}/environment/assets/reference_shallow_water_v1")
    add_test(NAME platform_ns3_trajectory_geometry_test COMMAND platform_ns3_trajectory_geometry_test)
  endif()
endif()
