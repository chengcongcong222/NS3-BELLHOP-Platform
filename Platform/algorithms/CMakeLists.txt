platform_add_interface_module(algorithms)
if(BUILD_TESTING)
  add_test(NAME platform_protocolctl_test
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/test_protocolctl.py")
endif()
