# --- SHADER COMPILATION LOGIC ---
# This file provides a helper to compile GLSL -> SPIR-V and wire it as a build dependency.
# Usage:
#   include(cmake/CompileShaders.cmake)
#   intrinsic_add_glsl_shaders(<target> [SOURCE_DIR <dir>] [OUTPUT_DIR <dir>])

find_program(GLSL_COMPILER glslc)

function(intrinsic_add_glsl_shaders target_name)
    if (NOT GLSL_COMPILER)
        message(WARNING "glslc not found! '${target_name}' shaders will not compile.")
        return()
    endif()

    set(options)
    set(oneValueArgs SOURCE_DIR OUTPUT_DIR)
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT ARG_SOURCE_DIR)
        set(ARG_SOURCE_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")
    endif()

    # Prefer compiling directly into the runtime output dir so 'Run Sandbox' always sees current SPV.
    # Works well for single-config generators (Ninja). Multi-config can override by passing OUTPUT_DIR.
    if (NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
    endif()

    file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")

    # Discover shaders. CONFIGURE_DEPENDS makes CMake re-scan on build if files are added/removed.
    file(GLOB_RECURSE _INTRINSIC_GLSL
        CONFIGURE_DEPENDS
        "${ARG_SOURCE_DIR}/*.vert"
        "${ARG_SOURCE_DIR}/*.frag"
        "${ARG_SOURCE_DIR}/*.comp"
    )
    file(GLOB_RECURSE _INTRINSIC_GLSL_INCLUDES
        CONFIGURE_DEPENDS
        "${ARG_SOURCE_DIR}/*.glsl"
        "${ARG_SOURCE_DIR}/*.glslinc"
    )

    if (NOT _INTRINSIC_GLSL)
        message(WARNING "No shaders found under '${ARG_SOURCE_DIR}'.")
        return()
    endif()

    set(_spv_outputs "")
    foreach(_src IN LISTS _INTRINSIC_GLSL)
        file(RELATIVE_PATH _rel "${ARG_SOURCE_DIR}" "${_src}")
        # Preserve subfolders and add .spv suffix (e.g. foo/bar.comp -> foo/bar.comp.spv)
        set(_out "${ARG_OUTPUT_DIR}/${_rel}.spv")
        get_filename_component(_out_dir "${_out}" DIRECTORY)
        file(MAKE_DIRECTORY "${_out_dir}")

        add_custom_command(
            OUTPUT "${_out}"
            COMMAND "${GLSL_COMPILER}" "${_src}" -I "${ARG_SOURCE_DIR}" -o "${_out}" --target-env=vulkan1.3
            DEPENDS "${_src}" ${_INTRINSIC_GLSL_INCLUDES}
            COMMENT "Compiling GLSL -> SPV: ${_rel}"
            VERBATIM
        )

        list(APPEND _spv_outputs "${_out}")
    endforeach()

    set(_shader_target "${target_name}_Shaders")
    add_custom_target(${_shader_target} DEPENDS ${_spv_outputs})
    add_dependencies(${target_name} ${_shader_target})

    set_property(TARGET ${_shader_target} PROPERTY FOLDER "Shaders")
endfunction()
