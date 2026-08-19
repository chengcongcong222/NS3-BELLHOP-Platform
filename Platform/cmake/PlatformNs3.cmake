include_guard(GLOBAL)

function(platform_probe_ns3)
  set(PLATFORM_NS3_AVAILABLE FALSE PARENT_SCOPE)

  if(NOT PLATFORM_ENABLE_NS3)
    message(STATUS
      "ns-3 integration is disabled. Set PLATFORM_ENABLE_NS3=ON to probe ns-3.47 core."
    )
    return()
  endif()

  if(PLATFORM_NS3_DIR)
    if(NOT IS_DIRECTORY "${PLATFORM_NS3_DIR}")
      message(WARNING
        "PLATFORM_NS3_DIR is not a directory: ${PLATFORM_NS3_DIR}. "
        "The ns-3 kernel smoke target will be disabled."
      )
      return()
    endif()

    set(ns3_DIR "${PLATFORM_NS3_DIR}")
  endif()

  find_package(ns3 3.47 EXACT QUIET CONFIG COMPONENTS core)

  if(ns3_FOUND AND TARGET ns3::core)
    set(PLATFORM_NS3_AVAILABLE TRUE PARENT_SCOPE)
    message(STATUS "Found ns-3 ${ns3_VERSION} core target: ns3::core")
    return()
  endif()

  message(WARNING
    "PLATFORM_ENABLE_NS3=ON, but the ns-3.47 core CMake package was not found. "
    "The Platform skeleton remains configurable and the ns-3 kernel smoke target is disabled. "
    "Build/install ns-3.47 separately, then set PLATFORM_NS3_DIR to the directory containing "
    "ns3Config.cmake (commonly <prefix>/lib/cmake/ns3). No fallback scheduler is used."
  )
endfunction()
