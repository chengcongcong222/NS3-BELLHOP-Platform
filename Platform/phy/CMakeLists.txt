platform_add_interface_module(phy)

target_include_directories(
  platform_phy
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(platform_phy INTERFACE Platform::contracts)

if(BUILD_TESTING)
  add_executable(
    platform_phy_waveform_pipeline_test
    tests/waveform_pipeline_test.cpp
  )
  target_link_libraries(
    platform_phy_waveform_pipeline_test
    PRIVATE Platform::phy
  )
  add_test(
    NAME platform_phy_waveform_pipeline_test
    COMMAND platform_phy_waveform_pipeline_test
  )

  add_executable(
    platform_phy_configured_tx_phy_test
    tests/configured_tx_phy_test.cpp
  )
  target_link_libraries(
    platform_phy_configured_tx_phy_test
    PRIVATE Platform::phy
  )
  add_test(
    NAME platform_phy_configured_tx_phy_test
    COMMAND platform_phy_configured_tx_phy_test
  )
endif()
