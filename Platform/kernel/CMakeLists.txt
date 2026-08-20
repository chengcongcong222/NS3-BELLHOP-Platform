platform_add_interface_module(kernel)

target_include_directories(
  platform_kernel
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(platform_kernel INTERFACE Platform::contracts)

if(PLATFORM_NS3_AVAILABLE)
  target_link_libraries(platform_kernel INTERFACE ns3::core)
endif()

if(BUILD_TESTING AND PLATFORM_NS3_AVAILABLE)
  add_executable(
    platform_ns3_kernel_smoke_test
    tests/ns3_kernel_smoke_test.cpp
  )
  target_link_libraries(
    platform_ns3_kernel_smoke_test
    PRIVATE
      Platform::kernel
      ns3::core
  )
  add_test(
    NAME platform_ns3_kernel_smoke_test
    COMMAND platform_ns3_kernel_smoke_test
  )

  add_executable(
    platform_ns3_event_dispatcher_test
    tests/ns3_event_dispatcher_test.cpp
  )
  target_link_libraries(
    platform_ns3_event_dispatcher_test
    PRIVATE Platform::kernel
  )
  add_test(
    NAME platform_ns3_event_dispatcher_test
    COMMAND platform_ns3_event_dispatcher_test
  )

  add_executable(
    platform_ns3_event_dispatcher_batch_test
    tests/ns3_event_dispatcher_batch_test.cpp
  )
  target_link_libraries(
    platform_ns3_event_dispatcher_batch_test
    PRIVATE Platform::kernel
  )
  add_test(
    NAME platform_ns3_event_dispatcher_batch_test
    COMMAND platform_ns3_event_dispatcher_batch_test
  )

  add_executable(
    platform_ns3_plan_installer_test
    tests/ns3_plan_installer_test.cpp
  )
  target_link_libraries(
    platform_ns3_plan_installer_test
    PRIVATE Platform::kernel
  )
  add_test(
    NAME platform_ns3_plan_installer_test
    COMMAND platform_ns3_plan_installer_test
  )

elseif(BUILD_TESTING AND PLATFORM_ENABLE_NS3)
  message(STATUS
    "platform_ns3_kernel_smoke_test is disabled because ns-3.47 core is unavailable."
  )
endif()
