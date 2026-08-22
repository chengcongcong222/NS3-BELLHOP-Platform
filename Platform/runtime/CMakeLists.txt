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

  add_executable(
    platform_runtime_packet_queue_store_test
    tests/packet_queue_store_test.cpp
  )
  target_include_directories(
    platform_runtime_packet_queue_store_test
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(
    platform_runtime_packet_queue_store_test
    PRIVATE Platform::runtime
  )
  add_test(
    NAME platform_runtime_packet_queue_store_test
    COMMAND platform_runtime_packet_queue_store_test
  )

  add_executable(
    platform_runtime_tx_preparation_test
    tests/tx_preparation_test.cpp
  )
  target_include_directories(
    platform_runtime_tx_preparation_test
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(
    platform_runtime_tx_preparation_test
    PRIVATE Platform::runtime
  )
  add_test(
    NAME platform_runtime_tx_preparation_test
    COMMAND platform_runtime_tx_preparation_test
  )

  add_executable(
    platform_runtime_candidate_receiver_resolver_test
    tests/candidate_receiver_resolver_test.cpp
  )
  target_include_directories(
    platform_runtime_candidate_receiver_resolver_test
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(
    platform_runtime_candidate_receiver_resolver_test
    PRIVATE Platform::runtime
  )
  add_test(
    NAME platform_runtime_candidate_receiver_resolver_test
    COMMAND platform_runtime_candidate_receiver_resolver_test
  )

  add_executable(
    platform_runtime_plan_bound_tx_runtime_test
    tests/plan_bound_tx_runtime_test.cpp
  )
  target_include_directories(
    platform_runtime_plan_bound_tx_runtime_test
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(
    platform_runtime_plan_bound_tx_runtime_test
    PRIVATE Platform::runtime
  )
  add_test(
    NAME platform_runtime_plan_bound_tx_runtime_test
    COMMAND platform_runtime_plan_bound_tx_runtime_test
  )
endif()
