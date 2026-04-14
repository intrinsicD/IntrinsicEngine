set(target_name ExtrinsicPlatform)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Platform.IWindow.cppm
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicCore)

if (EXTRINSIC_PLATFORM STREQUAL "Linux")
    if (EXTRINSIC_BACKEND STREQUAL "Vulkan")
        add_subdirectory(LinuxGlfwVulkan)
    endif ()
endif ()
