# ARCH-022: the plugin build id. A plugin returns INTRINSIC_PLUGIN_BUILD_ID from the generated PluginBuildId.h and
# the host refuses another. It is a sha256 over the build settings a plugin must share with the engine (toolchain,
# standard library, flags, definitions, build type, sanitizers, shared engine) and every module interface under the
# source root, sorted by path.
# - include(): intrinsic_add_plugin_build_id(<target> <source root> <output dir>) generates the header at build time
#   and puts its directory on <target>'s public include path.
# - cmake -P with ROOT, OUT, SETTINGS and DEFINITIONS: writes the header.

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    foreach(_required IN ITEMS ROOT OUT SETTINGS)
        if(NOT DEFINED ${_required})
            message(FATAL_ERROR "PluginBuildId.cmake requires ${_required}")
        endif()
    endforeach()

    file(GLOB_RECURSE _interfaces RELATIVE "${ROOT}" "${ROOT}/*.cppm")
    list(SORT _interfaces)
    set(_digest_input "${SETTINGS}\n${DEFINITIONS}\n")
    foreach(_interface IN LISTS _interfaces)
        file(SHA256 "${ROOT}/${_interface}" _hash)
        string(APPEND _digest_input "${_interface} ${_hash}\n")
    endforeach()
    string(SHA256 _id "${_digest_input}")

    # Rewritten only when the id changes, so its dependents do not rebuild for nothing.
    set(_header "#define INTRINSIC_PLUGIN_BUILD_ID \"${_id}\"\n")
    set(_current "")
    if(EXISTS "${OUT}")
        file(READ "${OUT}" _current)
    endif()
    if(NOT _current STREQUAL _header)
        file(WRITE "${OUT}" "${_header}")
    endif()
    return()
endif()

function(intrinsic_add_plugin_build_id target root dir)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _config)
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_FLAGS} -print-file-name=libstdc++.so
            OUTPUT_VARIABLE _stdlib OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    string(REPLACE ";" "," _settings
            "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} ${CMAKE_CXX_COMPILER}|stdlib ${_stdlib}|"
            "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_config}}|${INTRINSIC_COMPILE_FLAGS}|${CMAKE_BUILD_TYPE}|"
            "sanitizers ${INTRINSIC_ENABLE_SANITIZERS} ${INTRINSIC_SANITIZER_MODE}|shared ${BUILD_SHARED_LIBS}")
    # The sorted interface list is a dependency too: adding or removing an interface rewrites it at configure time
    # (the glob reconfigures), so a removal also computes the id again.
    file(GLOB_RECURSE _interfaces CONFIGURE_DEPENDS "${root}/*.cppm")
    list(SORT _interfaces)
    string(REPLACE ";" "\n" _listed "${_interfaces}")
    file(CONFIGURE OUTPUT "${dir}/PluginBuildId.interfaces" CONTENT "${_listed}\n")
    add_custom_command(
            OUTPUT "${dir}/PluginBuildId.stamp"
            BYPRODUCTS "${dir}/PluginBuildId.h"
            COMMAND ${CMAKE_COMMAND} "-DROOT=${root}" "-DOUT=${dir}/PluginBuildId.h" "-DSETTINGS=${_settings}"
                    "-DDEFINITIONS=$<JOIN:$<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>,,>"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
            COMMAND ${CMAKE_COMMAND} -E touch "${dir}/PluginBuildId.stamp"
            DEPENDS ${_interfaces} "${dir}/PluginBuildId.interfaces" "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
            COMMENT "Plugin build id"
            VERBATIM)
    add_custom_target(${target}PluginBuildId DEPENDS "${dir}/PluginBuildId.stamp")
    add_dependencies(${target} ${target}PluginBuildId)
    target_include_directories(${target} PUBLIC $<BUILD_INTERFACE:${dir}>)
endfunction()
