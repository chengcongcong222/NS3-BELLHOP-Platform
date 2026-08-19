include_guard(GLOBAL)

function(platform_add_interface_module module_name)
  set(target_name "platform_${module_name}")

  if(TARGET "${target_name}")
    message(FATAL_ERROR "Platform target already exists: ${target_name}")
  endif()

  add_library("${target_name}" INTERFACE)
  add_library("Platform::${module_name}" ALIAS "${target_name}")
  target_compile_features(
    "${target_name}"
    INTERFACE "cxx_std_${PLATFORM_CXX_STANDARD}"
  )
endfunction()
