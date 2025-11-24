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
    )

    foreach (SHADER ${SHADERS})
        add_custom_command(
                OUTPUT ${SHADER_BINARY_DIR}/${SHADER}.spv
                COMMAND ${GLSL_COMPILER} ${SHADER_SOURCE_DIR}/${SHADER} -o ${SHADER_BINARY_DIR}/${SHADER}.spv
                DEPENDS ${SHADER_SOURCE_DIR}/${SHADER}
                COMMENT "Compiling ${SHADER}"
        )
        list(APPEND SPV_SHADERS ${SHADER_BINARY_DIR}/${SHADER}.spv)
    endforeach ()

    # Ensure shaders are built before the app
    add_custom_target(Shaders ALL DEPENDS ${SPV_SHADERS})
    add_dependencies(Sandbox Shaders)
endif ()