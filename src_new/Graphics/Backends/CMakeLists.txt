add_subdirectory(Null)

if(EXTRINSIC_BACKEND STREQUAL "Vulkan")
    if(INTRINSIC_HEADLESS_NO_GLFW)
        message(STATUS "INTRINSIC_HEADLESS_NO_GLFW=ON: skipping Extrinsic Vulkan backend")
    else()
        add_subdirectory(Vulkan)
    endif()
endif()
