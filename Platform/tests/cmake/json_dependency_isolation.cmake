set(
  forbidden_roots
  contracts
  kernel
  runtime
  structure
  planning
  phy
  environment
  application
)

foreach(root IN LISTS forbidden_roots)
  file(
    GLOB_RECURSE candidate_files
    LIST_DIRECTORIES FALSE
    "${PLATFORM_SOURCE_DIR}/${root}/*.cpp"
    "${PLATFORM_SOURCE_DIR}/${root}/*.hpp"
    "${PLATFORM_SOURCE_DIR}/${root}/*.h"
  )
  foreach(candidate IN LISTS candidate_files)
    file(READ "${candidate}" contents)
    if(contents MATCHES "nlohmann/json")
      message(
        FATAL_ERROR
        "Forbidden nlohmann/json dependency outside worker adapter: ${candidate}"
      )
    endif()
  endforeach()
endforeach()

file(
  GLOB_RECURSE worker_files
  LIST_DIRECTORIES FALSE
  "${PLATFORM_SOURCE_DIR}/worker/*.cpp"
  "${PLATFORM_SOURCE_DIR}/worker/*.hpp"
  "${PLATFORM_SOURCE_DIR}/worker/*.h"
)
foreach(candidate IN LISTS worker_files)
  file(READ "${candidate}" contents)
  if(contents MATCHES "nlohmann/json" AND
     NOT candidate MATCHES "/worker/(codec|adapter)/")
    message(
      FATAL_ERROR
      "nlohmann/json is only allowed in worker codec/adapter: ${candidate}"
    )
  endif()
endforeach()
