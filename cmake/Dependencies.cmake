# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Using CCache: ${CCACHE_PROGRAM}")
    # BUG-015: this repository builds C++23 named modules with clang. In ccache's
    # default direct/preprocessor modes the hash of a module-consuming TU can miss
    # imported BMI contents, so a module interface layout change can reuse an
    # object compiled against the old layout. Use depend mode and disable direct
    # mode for preset builds so ccache keys module importers through
    # compiler-generated dependency data instead of a direct source hash.
    set(_intrinsic_ccache_launcher
        "${CMAKE_COMMAND}" -E env CCACHE_DEPEND=1 CCACHE_NODIRECT=1 "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER ${_intrinsic_ccache_launcher})
    set(CMAKE_C_COMPILER_LAUNCHER ${_intrinsic_ccache_launcher})
endif()

if(UNIX AND NOT APPLE AND NOT EXISTS "/usr/include/X11/extensions/Xrandr.h")
    if(NOT INTRINSIC_HEADLESS_NO_GLFW)
        message(WARNING
            "X11 RandR headers were not found at /usr/include/X11/extensions/Xrandr.h. "
            "Enabling INTRINSIC_HEADLESS_NO_GLFW to keep non-windowing modules buildable."
        )
    endif()
    set(INTRINSIC_HEADLESS_NO_GLFW ON CACHE BOOL "" FORCE)
endif()

if(NOT VCPKG_MANIFEST_MODE)
    message(FATAL_ERROR
        "IntrinsicEngine dependencies are resolved through vcpkg manifest mode. "
        "Run tools/setup/bootstrap_vcpkg.sh and configure with a repository preset "
        "so CMAKE_TOOLCHAIN_FILE points at external/vcpkg/scripts/buildsystems/vcpkg.cmake."
    )
endif()

message(STATUS "Intrinsic dependencies: using vcpkg manifest packages")

find_package(glm CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(draco CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)

if(NOT TARGET glm AND TARGET glm::glm)
    add_library(glm INTERFACE IMPORTED)
    target_link_libraries(glm INTERFACE glm::glm)
endif()

if(NOT TARGET EnTT)
    add_library(EnTT INTERFACE IMPORTED)
    target_link_libraries(EnTT INTERFACE EnTT::EnTT)
    get_target_property(_intrinsic_entt_includes EnTT::EnTT INTERFACE_INCLUDE_DIRECTORIES)
    if(_intrinsic_entt_includes)
        set_target_properties(EnTT PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_intrinsic_entt_includes}")
    endif()
    unset(_intrinsic_entt_includes)
endif()

if(NOT TARGET stb)
    add_library(stb INTERFACE)
    if(NOT Stb_INCLUDE_DIR)
        find_path(Stb_INCLUDE_DIR NAMES stb_image.h REQUIRED)
    endif()
    target_include_directories(stb INTERFACE "${Stb_INCLUDE_DIR}")
endif()

find_path(tinygltf_SOURCE_DIR NAMES tiny_gltf.h REQUIRED)

find_path(INTRINSIC_DRACO_INCLUDE_DIR NAMES draco/compression/encode.h REQUIRED)
set(_intrinsic_draco_target "")
foreach(_candidate IN ITEMS Draco::draco draco::draco draco draco_static draco_shared)
    if(TARGET ${_candidate})
        set(_intrinsic_draco_target "${_candidate}")
        break()
    endif()
endforeach()
if(_intrinsic_draco_target STREQUAL "")
    message(FATAL_ERROR "draco package did not define a recognized target")
endif()
if(NOT TARGET Draco::draco)
    add_library(Draco::draco INTERFACE IMPORTED)
    target_link_libraries(Draco::draco INTERFACE "${_intrinsic_draco_target}")
endif()
unset(_intrinsic_draco_target)

if(NOT TARGET tinygltf)
    set(_intrinsic_tinygltf_impl_dir "${CMAKE_BINARY_DIR}/generated/deps")
    set(_intrinsic_tinygltf_impl "${_intrinsic_tinygltf_impl_dir}/tinygltf_impl.cpp")
    file(MAKE_DIRECTORY "${_intrinsic_tinygltf_impl_dir}")
    file(WRITE "${_intrinsic_tinygltf_impl}"
        "#define TINYGLTF_NOEXCEPTION\n"
        "#define TINYGLTF_IMPLEMENTATION\n"
        "#define STB_IMAGE_IMPLEMENTATION\n"
        "#define STB_IMAGE_WRITE_IMPLEMENTATION\n"
        "#include <tiny_gltf.h>\n"
    )

    add_library(tinygltf STATIC "${_intrinsic_tinygltf_impl}")
    target_include_directories(tinygltf PUBLIC "${tinygltf_SOURCE_DIR}")
    target_compile_options(tinygltf PRIVATE -w)
    target_compile_definitions(tinygltf PUBLIC TINYGLTF_ENABLE_DRACO TINYGLTF_NOEXCEPTION)
    target_link_libraries(tinygltf PUBLIC stb nlohmann_json::nlohmann_json Draco::draco)
endif()

add_library(imgui_core_lib INTERFACE)
target_link_libraries(imgui_core_lib INTERFACE imgui::imgui)

if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(volk CONFIG REQUIRED)
    find_package(Vulkan REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    find_package(imguizmo CONFIG REQUIRED)

    if(NOT TARGET volk)
        add_library(volk INTERFACE IMPORTED)
        target_link_libraries(volk INTERFACE volk::volk)
    endif()
    if(NOT TARGET VulkanMemoryAllocator)
        add_library(VulkanMemoryAllocator INTERFACE IMPORTED)
        target_link_libraries(VulkanMemoryAllocator INTERFACE GPUOpen::VulkanMemoryAllocator)
    endif()

    find_file(IMGUI_IMPL_GLFW_SOURCE
        NAMES imgui_impl_glfw.cpp
        PATH_SUFFIXES share/imgui/backends
        REQUIRED
    )
    get_filename_component(IMGUI_BACKEND_SOURCE_DIR "${IMGUI_IMPL_GLFW_SOURCE}" DIRECTORY)
    set(IMGUI_IMPL_VULKAN_SOURCE "${IMGUI_BACKEND_SOURCE_DIR}/imgui_impl_vulkan.cpp")
    if(NOT EXISTS "${IMGUI_IMPL_VULKAN_SOURCE}")
        message(FATAL_ERROR "Missing ImGui Vulkan backend source: ${IMGUI_IMPL_VULKAN_SOURCE}")
    endif()

    add_library(imgui_lib STATIC
        "${IMGUI_IMPL_GLFW_SOURCE}"
        "${IMGUI_IMPL_VULKAN_SOURCE}"
    )
    target_include_directories(imgui_lib PUBLIC "${IMGUI_BACKEND_SOURCE_DIR}")
    target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES GLFW_INCLUDE_NONE)
    target_link_libraries(imgui_lib PUBLIC imgui_core_lib glfw volk)

    add_library(imguizmo_lib INTERFACE)
    target_link_libraries(imguizmo_lib INTERFACE imguizmo::imguizmo imgui_lib)
else()
    add_library(glfw INTERFACE)
endif()
