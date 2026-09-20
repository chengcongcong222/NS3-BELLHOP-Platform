platform_add_interface_module(application)

if(PLATFORM_NS3_AVAILABLE)
  add_executable(
    platform_application_channel2_feature_demo
    tools/channel2_feature_demo.cpp
  )
  target_include_directories(
    platform_application_channel2_feature_demo
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
    platform_application_channel2_feature_demo
    PRIVATE Platform::application Platform::assembly Platform::environment
  )
  target_compile_definitions(
    platform_application_channel2_feature_demo
    PRIVATE PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT="${PROJECT_SOURCE_DIR}/environment/assets/reference_shallow_water_v1"
  )
  if(BUILD_TESTING)
    foreach(modulation IN ITEMS bpsk bfsk)
      foreach(noise IN ITEMS none awgn wenz)
        add_test(
          NAME platform_application_channel2_${modulation}_${noise}_test
          COMMAND platform_application_channel2_feature_demo
            --modulation ${modulation} --noise ${noise} --verify
        )
      endforeach()
    endforeach()
    add_test(
      NAME platform_application_channel2_strong_noise_test
      COMMAND platform_application_channel2_feature_demo
        --modulation bpsk --noise awgn --psd-db 120 --verify
    )
  endif()
endif()

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

  add_executable(
    platform_application_target_packet_metrics_test
    tests/target_packet_metrics_test.cpp
  )
  target_include_directories(platform_application_target_packet_metrics_test
                             PRIVATE "${PROJECT_SOURCE_DIR}/application")
  target_link_libraries(platform_application_target_packet_metrics_test
                        PRIVATE Platform::application)
  add_test(NAME platform_application_target_packet_metrics_test
           COMMAND platform_application_target_packet_metrics_test)

  add_executable(
    platform_application_run_event_journal_test
    tests/run_event_journal_test.cpp
  )
  target_link_libraries(
    platform_application_run_event_journal_test
    PRIVATE Platform::application
  )
  add_test(
    NAME platform_application_run_event_journal_test
    COMMAND platform_application_run_event_journal_test
  )
endif()

if(BUILD_TESTING AND PLATFORM_NS3_AVAILABLE)
  add_executable(
    platform_application_channel12_comparison_baseline_test
    tests/channel12_comparison_baseline_test.cpp
  )
  target_include_directories(
    platform_application_channel12_comparison_baseline_test
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
    platform_application_channel12_comparison_baseline_test
    PRIVATE Platform::application Platform::assembly Platform::environment
  )
  target_compile_definitions(
    platform_application_channel12_comparison_baseline_test
    PRIVATE PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT="${PROJECT_SOURCE_DIR}/environment/assets/reference_shallow_water_v1"
  )
  add_test(NAME platform_application_channel12_comparison_baseline_test
           COMMAND platform_application_channel12_comparison_baseline_test)

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
