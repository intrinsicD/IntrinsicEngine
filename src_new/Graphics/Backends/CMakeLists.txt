add_subdirectory(Null)

if(EXTRINSIC_BACKEND STREQUAL "Vulkan")
    if(INTRINSIC_HEADLESS_NO_GLFW)
        message(STATUS "INTRINSIC_HEADLESS_NO_GLFW=ON: skipping Extrinsic Vulkan backend")
    else()
        add_subdirectory(${CMAKE_SOURCE_DIR}/src/graphics/vulkan ${CMAKE_BINARY_DIR}/src_graphics_vulkan)
    endif()
endif()
