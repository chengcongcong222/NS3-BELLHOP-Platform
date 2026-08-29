find_package(Python3 3.12.3 EXACT COMPONENTS Interpreter REQUIRED)

set(PLATFORM_BACKEND_VENV "${CMAKE_CURRENT_BINARY_DIR}/venv")
set(PLATFORM_BACKEND_PYTHON "${PLATFORM_BACKEND_VENV}/bin/python")
set(PLATFORM_BACKEND_WHEELHOUSE
    "${PROJECT_SOURCE_DIR}/third_party/python_wheels/wheels")
set(PLATFORM_BACKEND_LOCK "${CMAKE_CURRENT_SOURCE_DIR}/requirements.lock")

file(
  GLOB PLATFORM_BACKEND_WHEELS
  CONFIGURE_DEPENDS
  "${PLATFORM_BACKEND_WHEELHOUSE}/*.whl"
)

add_custom_command(
  OUTPUT "${PLATFORM_BACKEND_VENV}/.s4_05_ready"
  COMMAND "${Python3_EXECUTABLE}" -m venv "${PLATFORM_BACKEND_VENV}"
  COMMAND
    "${PLATFORM_BACKEND_PYTHON}" -m pip install
    --disable-pip-version-check
    --no-index
    --find-links "${PLATFORM_BACKEND_WHEELHOUSE}"
    --require-hashes
    --requirement "${PLATFORM_BACKEND_LOCK}"
  COMMAND
    "${CMAKE_COMMAND}" -E touch
    "${PLATFORM_BACKEND_VENV}/.s4_05_ready"
  DEPENDS
    "${PLATFORM_BACKEND_LOCK}"
    ${PLATFORM_BACKEND_WHEELS}
  COMMENT "Preparing the offline P0-S4-05 Python environment"
  VERBATIM
)

add_custom_target(
  platform_backend_python_environment ALL
  DEPENDS "${PLATFORM_BACKEND_VENV}/.s4_05_ready"
)

if(BUILD_TESTING)
  add_test(
    NAME platform_backend_dependency_lock_test
    COMMAND
      "${PLATFORM_BACKEND_PYTHON}" -m pytest -q
      -o "cache_dir=${CMAKE_CURRENT_BINARY_DIR}/pytest-cache-lock"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/dependency_lock_test.py"
  )
  set_tests_properties(
    platform_backend_dependency_lock_test
    PROPERTIES
      ENVIRONMENT
        "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/src;PYTHONDONTWRITEBYTECODE=1"
  )

  add_test(
    NAME platform_backend_python_test
    COMMAND
      "${PLATFORM_BACKEND_PYTHON}" -m pytest -q
      -o "cache_dir=${CMAKE_CURRENT_BINARY_DIR}/pytest-cache"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests"
  )
  set(PLATFORM_BACKEND_TEST_ENVIRONMENT
      "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/src"
      "PYTHONDONTWRITEBYTECODE=1"
      "PLATFORM_WORKER_WIRE_GOLDEN_FIXTURE_PATH=$<TARGET_FILE:platform_worker_wire_golden_fixture>"
  )
  if(PLATFORM_NS3_AVAILABLE)
    list(
      APPEND PLATFORM_BACKEND_TEST_ENVIRONMENT
      "PLATFORM_SIM_WORKER_PATH=$<TARGET_FILE:platform_sim_worker>"
      "PLATFORM_WORKER_TEST_ASSET_BUILDER_PATH=$<TARGET_FILE:platform_worker_test_asset_builder>"
    )
  endif()
  set_tests_properties(
    platform_backend_python_test
    PROPERTIES ENVIRONMENT "${PLATFORM_BACKEND_TEST_ENVIRONMENT}"
  )
endif()
