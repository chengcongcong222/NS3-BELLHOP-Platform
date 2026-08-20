platform_add_interface_module(contracts)

target_include_directories(
  platform_contracts
  INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

if(BUILD_TESTING)
  foreach(contract_name IN ITEMS
      time
      identity
      errors
      committed_state
      communication
      phy_channel
      receiver_window
      rx_phy
      protocol_cycle_plan
      structure
      link_feasibility
  )
    set(test_target "platform_contracts_${contract_name}_test")
    add_executable(
      "${test_target}"
      "tests/${contract_name}_contract_test.cpp"
    )
    target_link_libraries("${test_target}" PRIVATE Platform::contracts)
    add_test(NAME "${test_target}" COMMAND "${test_target}")
  endforeach()
endif()
