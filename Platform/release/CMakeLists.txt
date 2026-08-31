if(BUILD_TESTING)
  find_package(Python3 3.12 COMPONENTS Interpreter REQUIRED)
  add_test(
    NAME platform_release_contract_test
    COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/release_contract_test.py"
      "${CMAKE_CURRENT_SOURCE_DIR}"
  )
endif()
