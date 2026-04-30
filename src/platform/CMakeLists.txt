set(target_name ExtrinsicPlatform)
intrinsic_add_module_library(${target_name})

if(NOT DEFINED EXTRINSIC_PLATFORM)
        set(EXTRINSIC_PLATFORM "${CMAKE_SYSTEM_NAME}" CACHE STRING "Host platform selection")
endif()
if(NOT DEFINED EXTRINSIC_BACKEND)
        set(EXTRINSIC_BACKEND "Vulkan" CACHE STRING "Graphics backend selection")
endif()

set(INTRINSIC_PLATFORM_BACKEND "Auto" CACHE STRING "Platform backend: Auto, Null, or Glfw")
set_property(CACHE INTRINSIC_PLATFORM_BACKEND PROPERTY STRINGS Auto Null Glfw)

set(_intrinsic_platform_backend "${INTRINSIC_PLATFORM_BACKEND}")
if(_intrinsic_platform_backend STREQUAL "Auto")
        if(NOT INTRINSIC_HEADLESS_NO_GLFW AND EXTRINSIC_PLATFORM STREQUAL "Linux" AND EXTRINSIC_BACKEND STREQUAL "Vulkan")
                set(_intrinsic_platform_backend "Glfw")
        else()
                set(_intrinsic_platform_backend "Null")
        endif()
endif()

if(NOT _intrinsic_platform_backend STREQUAL "Null" AND NOT _intrinsic_platform_backend STREQUAL "Glfw")
        message(FATAL_ERROR "Unsupported INTRINSIC_PLATFORM_BACKEND='${INTRINSIC_PLATFORM_BACKEND}'. Expected Auto, Null, or Glfw.")
endif()

if(_intrinsic_platform_backend STREQUAL "Glfw" AND INTRINSIC_HEADLESS_NO_GLFW)
        message(FATAL_ERROR "INTRINSIC_PLATFORM_BACKEND=Glfw requires INTRINSIC_HEADLESS_NO_GLFW=OFF.")
endif()

set(INTRINSIC_PLATFORM_BACKEND_SELECTED "${_intrinsic_platform_backend}" CACHE INTERNAL "Resolved platform backend")

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Platform.Input.cppm
        Platform.IWindow.cppm
        backends/null/Platform.Backend.Null.cppm
        PRIVATE
        Platform.CreateWindow.cpp
)

target_include_directories(${target_name} PUBLIC .)
target_link_libraries(${target_name} PUBLIC ExtrinsicCore)

if(_intrinsic_platform_backend STREQUAL "Glfw")
        target_sources(${target_name}
                PUBLIC
                FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
                backends/glfw/Platform.Backend.Glfw.cppm
                backends/glfw/Platform.Backend.GlfwVulkanSurface.cppm
        )
        target_compile_definitions(${target_name} PRIVATE INTRINSIC_PLATFORM_BACKEND_GLFW=1)
        target_link_libraries(${target_name} PUBLIC glfw imgui_lib)
else()
        target_compile_definitions(${target_name} PRIVATE INTRINSIC_PLATFORM_BACKEND_NULL=1)
endif()

message(STATUS "ExtrinsicPlatform: selected ${_intrinsic_platform_backend} backend")
