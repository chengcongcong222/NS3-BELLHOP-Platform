platform_add_interface_module(phy)

target_include_directories(
  platform_phy
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(platform_phy INTERFACE Platform::contracts)

if(BUILD_TESTING)
  add_executable(platform_phy_recorded_proxy_receiver_test tests/recorded_proxy_receiver_test.cpp)
  target_link_libraries(platform_phy_recorded_proxy_receiver_test PRIVATE Platform::phy)
  add_test(NAME platform_phy_recorded_proxy_receiver_test COMMAND platform_phy_recorded_proxy_receiver_test)
  add_executable(platform_phy_receiver_sample_mixer_test tests/receiver_sample_mixer_test.cpp)
  target_link_libraries(platform_phy_receiver_sample_mixer_test PRIVATE Platform::phy)
  add_test(NAME platform_phy_receiver_sample_mixer_test COMMAND platform_phy_receiver_sample_mixer_test)
  add_executable(platform_phy_recorded_waveform_channel_test tests/recorded_waveform_channel_test.cpp)
  target_link_libraries(platform_phy_recorded_waveform_channel_test PRIVATE Platform::phy)
  add_test(NAME platform_phy_recorded_waveform_channel_test COMMAND platform_phy_recorded_waveform_channel_test)
  add_executable(platform_phy_recorded_replay_test tests/recorded_replay_test.cpp)
  target_link_libraries(platform_phy_recorded_replay_test PRIVATE Platform::phy)
  add_test(NAME platform_phy_recorded_replay_test COMMAND platform_phy_recorded_replay_test)
  add_executable(platform_phy_waveform_phy_test tests/waveform_phy_test.cpp)
  target_link_libraries(platform_phy_waveform_phy_test PRIVATE Platform::phy)
  add_test(NAME platform_phy_waveform_phy_test
           COMMAND platform_phy_waveform_phy_test)
  add_executable(
    platform_phy_channel2_integration_baseline_test
    tests/channel2_integration_baseline_test.cpp
  )
  target_link_libraries(
    platform_phy_channel2_integration_baseline_test
    PRIVATE Platform::phy
  )
  add_test(
    NAME platform_phy_channel2_integration_baseline_test
    COMMAND platform_phy_channel2_integration_baseline_test
  )

  add_executable(
    platform_phy_scalar_ber_rx_phy_test
    tests/scalar_ber_rx_phy_test.cpp
  )
  target_link_libraries(
    platform_phy_scalar_ber_rx_phy_test
    PRIVATE Platform::phy
  )
  add_test(
    NAME platform_phy_scalar_ber_rx_phy_test
    COMMAND platform_phy_scalar_ber_rx_phy_test
  )

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

  add_executable(
    platform_phy_rate_based_tx_phy_test
    tests/rate_based_tx_phy_test.cpp
  )
  target_link_libraries(
    platform_phy_rate_based_tx_phy_test
    PRIVATE Platform::phy
  )
  add_test(
    NAME platform_phy_rate_based_tx_phy_test
    COMMAND platform_phy_rate_based_tx_phy_test
  )

  add_executable(
    platform_phy_sampled_scalar_phy_test
    tests/sampled_scalar_phy_test.cpp
  )
  target_link_libraries(platform_phy_sampled_scalar_phy_test PRIVATE Platform::phy)
  add_test(NAME platform_phy_sampled_scalar_phy_test
           COMMAND platform_phy_sampled_scalar_phy_test)
endif()
