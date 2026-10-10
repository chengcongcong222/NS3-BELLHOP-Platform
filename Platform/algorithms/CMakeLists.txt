platform_add_interface_module(algorithms)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME platform_protocolctl_test
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/test_protocolctl.py")
endif()
