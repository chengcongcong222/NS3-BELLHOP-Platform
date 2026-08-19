platform_add_interface_module(runtime)

target_link_libraries(platform_runtime INTERFACE Platform::contracts)

if(BUILD_TESTING)
  add_executable(
    platform_runtime_cycle_working_state_test
    tests/cycle_working_state_test.cpp
  )
  target_include_directories(
    platform_runtime_cycle_working_state_test
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(
    platform_runtime_cycle_working_state_test
    PRIVATE Platform::runtime
  )
  add_test(
    NAME platform_runtime_cycle_working_state_test
    COMMAND platform_runtime_cycle_working_state_test
  )

  add_executable(
    platform_runtime_world_state_commit_test
    tests/world_state_commit_test.cpp
  )
  target_include_directories(
    platform_runtime_world_state_commit_test
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(
    platform_runtime_world_state_commit_test
    PRIVATE Platform::runtime
  )
  add_test(
    NAME platform_runtime_world_state_commit_test
    COMMAND platform_runtime_world_state_commit_test
  )

  add_executable(
    platform_runtime_transmission_session_test
    tests/transmission_session_test.cpp
  )
  target_include_directories(
    platform_runtime_transmission_session_test
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(
    platform_runtime_transmission_session_test
    PRIVATE Platform::runtime
  )
  add_test(
    NAME platform_runtime_transmission_session_test
    COMMAND platform_runtime_transmission_session_test
  )

  add_executable(
    platform_runtime_receiver_processing_test
    tests/receiver_processing_test.cpp
  )
  target_include_directories(
    platform_runtime_receiver_processing_test
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(
    platform_runtime_receiver_processing_test
    PRIVATE Platform::runtime
  )
  add_test(
    NAME platform_runtime_receiver_processing_test
    COMMAND platform_runtime_receiver_processing_test
  )
endif()
