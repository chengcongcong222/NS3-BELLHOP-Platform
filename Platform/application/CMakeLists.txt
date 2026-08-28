platform_add_interface_module(application)

target_include_directories(
  platform_application
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(
  platform_application
  INTERFACE Platform::contracts
)

if(BUILD_TESTING)
  add_executable(
    platform_application_domain_test
    tests/application_domain_test.cpp
  )
  target_link_libraries(
    platform_application_domain_test
    PRIVATE Platform::application
  )
  add_test(
    NAME platform_application_domain_test
    COMMAND platform_application_domain_test
  )
endif()

if(BUILD_TESTING AND PLATFORM_NS3_AVAILABLE)
  add_executable(
    platform_application_acceptance_run_service_test
    tests/application_acceptance_run_service_test.cpp
  )
  target_include_directories(
    platform_application_acceptance_run_service_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/application"
      "${PROJECT_SOURCE_DIR}/assembly"
      "${PROJECT_SOURCE_DIR}/environment"
      "${PROJECT_SOURCE_DIR}/kernel"
      "${PROJECT_SOURCE_DIR}/phy"
      "${PROJECT_SOURCE_DIR}/planning"
      "${PROJECT_SOURCE_DIR}/runtime"
      "${PROJECT_SOURCE_DIR}/structure"
  )
  target_link_libraries(
    platform_application_acceptance_run_service_test
    PRIVATE
      Platform::application
      Platform::assembly
      Platform::environment
  )
  add_test(
    NAME platform_application_acceptance_run_service_test
    COMMAND platform_application_acceptance_run_service_test
  )
endif()
