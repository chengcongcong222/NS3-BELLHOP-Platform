cmake_minimum_required(VERSION 3.20)

project(NS3FactoryPlatform VERSION 0.1.0 LANGUAGES CXX)

set(PLATFORM_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD ${PLATFORM_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(PlatformModule)
include(PlatformNs3)

option(
  PLATFORM_ENABLE_NS3
  "Probe ns-3.47 core and build the kernel event smoke test"
  OFF
)

set(
  PLATFORM_NS3_DIR
  ""
  CACHE PATH
  "Directory containing the ns-3.47 CMake package configuration"
)

platform_probe_ns3()

add_subdirectory(third_party)
add_subdirectory(contracts)
add_subdirectory(kernel)
add_subdirectory(runtime)
add_subdirectory(structure)
add_subdirectory(planning)
add_subdirectory(phy)
add_subdirectory(environment)
add_subdirectory(adapters)
add_subdirectory(assembly)
add_subdirectory(application)
add_subdirectory(worker)
add_subdirectory(backend)
add_subdirectory(observability)
add_subdirectory(algorithms)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

message(STATUS "Platform C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Platform tests enabled: ${BUILD_TESTING}")
message(STATUS "Platform ns-3 probe enabled: ${PLATFORM_ENABLE_NS3}")
message(STATUS "Platform ns-3 available: ${PLATFORM_NS3_AVAILABLE}")
