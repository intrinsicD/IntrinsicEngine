# --- SHADER COMPILATION LOGIC ---
find_program(GLSL_COMPILER glslc)
if (NOT GLSL_COMPILER)
    message(WARNING "glslc not found! Shaders will not compile.")
else ()
    set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")
    set(SHADER_BINARY_DIR "${CMAKE_BINARY_DIR}/bin/shaders")

    file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

    # Define shaders
    set(SHADERS
            triangle.vert
            triangle.frag
            pick_id.vert
            pick_id.frag
            debug_view.vert
            debug_view.frag
            debug_view.comp
            selection_outline.frag
            line.vert
            line.frag
            pointcloud.vert
            pointcloud.frag
            instance_cull.comp
            instance_cull_multigeo.comp
            scene_update.comp
    )

    foreach (SHADER ${SHADERS})
        add_custom_command(
                OUTPUT ${SHADER_BINARY_DIR}/${SHADER}.spv
                COMMAND ${GLSL_COMPILER} ${SHADER_SOURCE_DIR}/${SHADER} -o ${SHADER_BINARY_DIR}/${SHADER}.spv --target-env=vulkan1.3
                DEPENDS ${SHADER_SOURCE_DIR}/${SHADER}
                COMMENT "Compiling ${SHADER}"
        )
        list(APPEND SPV_SHADERS ${SHADER_BINARY_DIR}/${SHADER}.spv)
    endforeach ()

    # Ensure shaders are built before the app
    add_custom_target(Shaders ALL DEPENDS ${SPV_SHADERS})
    add_dependencies(${target_name} Shaders)
endif ()