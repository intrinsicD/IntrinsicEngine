# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
# This prevents re-downloading/unzipping when you clean the build folder.
get_filename_component(ROOT_DIR ${CMAKE_SOURCE_DIR} ABSOLUTE)

# IMPORTANT: FetchContent only honors FETCHCONTENT_BASE_DIR when it's a CACHE variable.
# If it's a normal variable, CMake will default to <build>/_deps and <name>_SOURCE_DIR
# can end up unset/empty in some policy/config combinations.
if(NOT DEFINED INTRINSIC_DEPS_CACHE_DIR OR "${INTRINSIC_DEPS_CACHE_DIR}" STREQUAL "")
    set(INTRINSIC_DEPS_CACHE_DIR "${ROOT_DIR}/external/cache" CACHE PATH
        "Shared FetchContent dependency cache directory used by all build trees" FORCE)
endif()

if(NOT DEFINED FETCHCONTENT_BASE_DIR OR "${FETCHCONTENT_BASE_DIR}" STREQUAL "")
    set(FETCHCONTENT_BASE_DIR "${INTRINSIC_DEPS_CACHE_DIR}" CACHE PATH "FetchContent base directory" FORCE)
else()
    # Re-export it as a cache variable to ensure FetchContent honors it.
    set(FETCHCONTENT_BASE_DIR "${FETCHCONTENT_BASE_DIR}" CACHE PATH "FetchContent base directory" FORCE)
    set(INTRINSIC_DEPS_CACHE_DIR "${FETCHCONTENT_BASE_DIR}" CACHE PATH
        "Shared FetchContent dependency cache directory used by all build trees" FORCE)
endif()
file(MAKE_DIRECTORY "${FETCHCONTENT_BASE_DIR}/.locks")

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Using CCache: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

include(FetchContent)

if(UNIX AND NOT APPLE AND NOT EXISTS "/usr/include/X11/extensions/Xrandr.h")
    if(NOT INTRINSIC_HEADLESS_NO_GLFW)
        message(WARNING
            "X11 RandR headers were not found at /usr/include/X11/extensions/Xrandr.h. "
            "Enabling INTRINSIC_HEADLESS_NO_GLFW to keep non-windowing modules buildable."
        )
    endif()
    set(INTRINSIC_HEADLESS_NO_GLFW ON CACHE BOOL "" FORCE)
endif()

if(INTRINSIC_OFFLINE_DEPS)
    set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "Disable all FetchContent network updates" FORCE)
    message(STATUS "INTRINSIC_OFFLINE_DEPS=ON: using only local dependency sources")
elseif(NOT INTRINSIC_UPDATE_DEPS)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
        "Skip network update checks for already-populated FetchContent sources" FORCE)
endif()

macro(intrinsic_lock_dependency dep_name)
    set(lock_path "${FETCHCONTENT_BASE_DIR}/.locks/${dep_name}.lock")
    file(LOCK "${lock_path}" GUARD FUNCTION TIMEOUT 600 RESULT_VARIABLE lock_result)
    if(NOT lock_result STREQUAL "0")
        message(FATAL_ERROR
            "Timed out waiting for dependency cache lock: ${lock_path}\n"
            "Another configure may be populating '${dep_name}'. Stop stale CMake/Ninja/Git processes or remove the lock after verifying no configure is running.")
    endif()
endmacro()

function(intrinsic_require_offline_source dep_name)
    string(TOUPPER "${dep_name}" dep_name_upper)

    set(dep_override_var "FETCHCONTENT_SOURCE_DIR_${dep_name_upper}")
    if(DEFINED ${dep_override_var} AND NOT "${${dep_override_var}}" STREQUAL "")
        set(dep_source_dir "${${dep_override_var}}")
    else()
        set(dep_source_dir "${FETCHCONTENT_BASE_DIR}/${dep_name}-src")
        set(${dep_override_var}
            "${dep_source_dir}"
            CACHE PATH "Offline source directory for ${dep_name}" FORCE)
    endif()

    if(NOT IS_DIRECTORY "${dep_source_dir}")
        message(FATAL_ERROR
            "INTRINSIC_OFFLINE_DEPS=ON requires pre-populated dependency sources.\n"
            "Missing dependency '${dep_name}' at: ${dep_source_dir}\n"
            "Populate external/cache first (e.g. run configure once online or mirror repositories there)."
        )
    endif()

    file(GLOB dep_entries LIST_DIRECTORIES true "${dep_source_dir}/*")
    list(LENGTH dep_entries dep_entry_count)
    if(dep_entry_count EQUAL 0)
        message(FATAL_ERROR
            "INTRINSIC_OFFLINE_DEPS=ON found an empty directory for '${dep_name}' at: ${dep_source_dir}\n"
            "Populate this dependency source tree before configuring offline."
        )
    endif()
endfunction()

function(intrinsic_dependency_source_dir out_var dep_name)
    string(TOUPPER "${dep_name}" dep_name_upper)
    set(dep_override_var "FETCHCONTENT_SOURCE_DIR_${dep_name_upper}")
    if(DEFINED ${dep_override_var} AND NOT "${${dep_override_var}}" STREQUAL "")
        set(${out_var} "${${dep_override_var}}" PARENT_SCOPE)
    else()
        set(${out_var} "${FETCHCONTENT_BASE_DIR}/${dep_name}-src" PARENT_SCOPE)
    endif()
endfunction()

function(intrinsic_set_dependency_required_files dep_name)
    set_property(GLOBAL PROPERTY "INTRINSIC_DEP_REQUIRED_FILES_${dep_name}" "${ARGN}")
endfunction()

function(intrinsic_validate_dependency_source dep_name phase)
    get_property(required_files GLOBAL PROPERTY "INTRINSIC_DEP_REQUIRED_FILES_${dep_name}")
    if(NOT required_files)
        return()
    endif()

    intrinsic_dependency_source_dir(dep_source_dir ${dep_name})
    if(NOT IS_DIRECTORY "${dep_source_dir}")
        if(phase STREQUAL "pre" AND NOT INTRINSIC_OFFLINE_DEPS)
            return()
        endif()
        message(FATAL_ERROR
            "Dependency cache for '${dep_name}' is incomplete at: ${dep_source_dir}\n"
            "Missing dependency source directory.\n"
            "Recovery: remove ${dep_source_dir} and rerun configure with network access, or repopulate external/cache before configuring with INTRINSIC_OFFLINE_DEPS=ON."
        )
        return()
    endif()

    set(missing_files "")
    foreach(required_file IN LISTS required_files)
        if(NOT EXISTS "${dep_source_dir}/${required_file}")
            list(APPEND missing_files "${required_file}")
        endif()
    endforeach()
    if(NOT missing_files)
        return()
    endif()

    string(REPLACE ";" "\n  - " missing_file_list "${missing_files}")
    if(phase STREQUAL "pre" AND NOT INTRINSIC_OFFLINE_DEPS)
        message(WARNING
            "Dependency cache for '${dep_name}' is incomplete at: ${dep_source_dir}\n"
            "Missing required file(s):\n  - ${missing_file_list}\n"
            "Removing the partial source/build trees before FetchContent repopulates them."
        )
        file(REMOVE_RECURSE "${dep_source_dir}")
        file(REMOVE_RECURSE "${FETCHCONTENT_BASE_DIR}/${dep_name}-build")
        file(REMOVE_RECURSE "${FETCHCONTENT_BASE_DIR}/${dep_name}-subbuild")
        return()
    endif()

    message(FATAL_ERROR
        "Dependency cache for '${dep_name}' is incomplete at: ${dep_source_dir}\n"
        "Missing required file(s):\n  - ${missing_file_list}\n"
        "Recovery: remove ${dep_source_dir} and rerun configure with network access, or repopulate external/cache before configuring with INTRINSIC_OFFLINE_DEPS=ON."
    )
endfunction()

function(intrinsic_make_available dep_name)
    intrinsic_lock_dependency(${dep_name})
    if(INTRINSIC_OFFLINE_DEPS)
        intrinsic_require_offline_source(${dep_name})
    endif()
    intrinsic_validate_dependency_source(${dep_name} pre)
    FetchContent_MakeAvailable(${dep_name})
    intrinsic_validate_dependency_source(${dep_name} post)
endfunction()

function(intrinsic_populate_source dep_name)
    intrinsic_lock_dependency(${dep_name})
    if(INTRINSIC_OFFLINE_DEPS)
        intrinsic_require_offline_source(${dep_name})
    endif()
    intrinsic_validate_dependency_source(${dep_name} pre)
    FetchContent_GetProperties(${dep_name})
    intrinsic_dependency_source_dir(dep_source_dir ${dep_name})
    if(NOT IS_DIRECTORY "${dep_source_dir}")
        set(${dep_name}_POPULATED FALSE)
    endif()
    if(NOT ${dep_name}_POPULATED)
        FetchContent_Populate(${dep_name})
    endif()
    intrinsic_validate_dependency_source(${dep_name} post)
    FetchContent_GetProperties(${dep_name})
    set(${dep_name}_SOURCE_DIR "${${dep_name}_SOURCE_DIR}" PARENT_SCOPE)
    set(${dep_name}_BINARY_DIR "${${dep_name}_BINARY_DIR}" PARENT_SCOPE)
    set(${dep_name}_POPULATED "${${dep_name}_POPULATED}" PARENT_SCOPE)
endfunction()

# --- GLM ---
set(GLM_QUIET ON CACHE BOOL "" FORCE)
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.0
)
intrinsic_set_dependency_required_files(glm
        CMakeLists.txt
        glm/glm.hpp
        glm/detail/setup.hpp
        glm/detail/type_vec4.inl
        glm/detail/type_mat4x4.inl
)
intrinsic_make_available(glm)
#target_link_libraries(glm PUBLIC GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_ENABLE_EXPERIMENTAL GLM_RIGHT_HANDED)
#target_link_libraries(glm PUBLIC IntrinsicConfig)

if(TARGET glm)
    target_compile_options(glm PRIVATE -w)
endif()

# --- GTest ---
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
)
intrinsic_set_dependency_required_files(googletest CMakeLists.txt googletest/include/gtest/gtest.h)
set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
intrinsic_make_available(googletest)

# --- GLFW ---
FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.9
)
intrinsic_set_dependency_required_files(glfw CMakeLists.txt include/GLFW/glfw3.h)
if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    intrinsic_make_available(glfw)
else()
    add_library(glfw INTERFACE)
endif()

if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    # --- Volk (Vulkan Meta Loader) ---
    # Pin to stable commit instead of master
    FetchContent_Declare(
            volk
            GIT_REPOSITORY https://github.com/zeux/volk.git
            GIT_TAG vulkan-sdk-1.3.268.0
    )
    intrinsic_set_dependency_required_files(volk CMakeLists.txt volk.h volk.c)
    intrinsic_make_available(volk)
    find_package(Vulkan REQUIRED)

    # VulkanMemoryAllocator is header-only for engine usage.
    # Disable its sample targets to avoid toolchain-specific libc header issues.
    set(VMA_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(VMA_STATIC_VULKAN_FUNCTIONS ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
            vma
            GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
            GIT_TAG v3.1.0
    )

    intrinsic_set_dependency_required_files(vma CMakeLists.txt include/vk_mem_alloc.h)
    intrinsic_make_available(vma)
endif()


# Pin STB to specific commit for reproducible builds
FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG 5736b15f7ea0ffb08dd38af21067c314d6a3aae9  # 2023-01-30
)

intrinsic_set_dependency_required_files(stb stb_image.h stb_image_write.h)
intrinsic_make_available(stb)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

FetchContent_Declare(
        entt
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG v3.13.0 # Use a stable tag
)
intrinsic_set_dependency_required_files(entt CMakeLists.txt src/entt/entity/registry.hpp)
intrinsic_make_available(entt)

# --- JSON (Required for TinyGLTF) ---
FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)
intrinsic_set_dependency_required_files(json CMakeLists.txt include/nlohmann/json.hpp single_include/nlohmann/json.hpp)
intrinsic_make_available(json)

# --- Draco (Required for glTF KHR_draco_mesh_compression) ---
set(DRACO_INSTALL OFF CACHE BOOL "Disable Draco install targets" FORCE)
set(DRACO_TESTS OFF CACHE BOOL "Disable Draco tests" FORCE)
FetchContent_Declare(
        draco
        GIT_REPOSITORY https://github.com/google/draco.git
        GIT_TAG 1.5.7
        GIT_SUBMODULES ""
        GIT_SUBMODULES_RECURSE FALSE
)
intrinsic_set_dependency_required_files(draco
        CMakeLists.txt
        src/draco/compression/encode.h
        src/draco/core/decoder_buffer.cc
        src/draco/mesh/mesh.cc
        src/draco/compression/mesh/mesh_decoder.cc
        src/draco/io/file_utils.cc
        src/draco/tools/draco_decoder.cc
)
intrinsic_make_available(draco)

if(TARGET draco)
    add_library(Draco::draco ALIAS draco)
elseif(TARGET draco_static)
    add_library(Draco::draco ALIAS draco_static)
elseif(TARGET draco_shared)
    add_library(Draco::draco ALIAS draco_shared)
else()
    message(FATAL_ERROR "Draco dependency did not define a recognized library target")
endif()

# --- TinyGLTF ---
FetchContent_Declare(
        tinygltf
        GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
        GIT_TAG v2.9.0
)
intrinsic_set_dependency_required_files(tinygltf CMakeLists.txt tiny_gltf.h)
intrinsic_make_available(tinygltf)

# --- ImGui (Docking Branch) ---
# Pin to specific docking branch commit for stability
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.5-docking
)
intrinsic_set_dependency_required_files(imgui imgui.cpp imgui.h backends/imgui_impl_vulkan.cpp)

# ImGui is not a first-class CMake project; depending on CMake policies/version,
# FetchContent_MakeAvailable(imgui) may not leave us with a reliable imgui_SOURCE_DIR.
# We therefore ensure the content is populated and then resolve the source path.
intrinsic_populate_source(imgui)

if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    FetchContent_Declare(
            imguizmo
            GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    )

    # Only populate ImGuizmo sources. Its upstream CMake target is a helper/demo
    # target that does not know about this repository's ImGui include path; the
    # repository-owned imguizmo_lib target below supplies the correct dependency.
    intrinsic_set_dependency_required_files(imguizmo CMakeLists.txt src/ImGuizmo.cpp)
    intrinsic_populate_source(imguizmo)
endif()

# Prefer the path returned by FetchContent_Populate; fall back to our offline cache layout.
set(IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}")
set(IMGUIZMO_SOURCE_DIR "${imguizmo_SOURCE_DIR}")

if("${IMGUI_SOURCE_DIR}" STREQUAL "")
    set(IMGUI_SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/imgui-src")
endif()

if(NOT IS_DIRECTORY "${IMGUI_SOURCE_DIR}" OR NOT EXISTS "${IMGUI_SOURCE_DIR}/imgui.cpp")
    message(FATAL_ERROR
        "ImGui sources not found. Expected at: ${IMGUI_SOURCE_DIR}\n"
        "Missing file: ${IMGUI_SOURCE_DIR}/imgui.cpp\n"
        "If configuring offline, ensure external/cache/imgui-src is populated and non-empty."
    )
endif()

if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    if("${IMGUIZMO_SOURCE_DIR}" STREQUAL "")
        set(IMGUIZMO_SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/imguizmo-src")
    endif()
    if(DEFINED INTRINSIC_IMGUIZMO_SOURCE_DIR AND NOT "${INTRINSIC_IMGUIZMO_SOURCE_DIR}" STREQUAL "")
        set(IMGUIZMO_SOURCE_DIR "${INTRINSIC_IMGUIZMO_SOURCE_DIR}")
    endif()
endif()


if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    set(INTRINSIC_IMGUIZMO_SOURCE_DIR "${IMGUIZMO_SOURCE_DIR}" CACHE PATH
        "ImGuizmo checkout root or directory containing ImGuizmo.cpp" FORCE)
    set(IMGUIZMO_INCLUDE_DIR "${IMGUIZMO_SOURCE_DIR}")
    set(IMGUIZMO_CPP "${IMGUIZMO_SOURCE_DIR}/ImGuizmo.cpp")

    if(IS_DIRECTORY "${IMGUIZMO_SOURCE_DIR}" AND NOT EXISTS "${IMGUIZMO_CPP}" AND EXISTS "${IMGUIZMO_SOURCE_DIR}/src/ImGuizmo.cpp")
        set(IMGUIZMO_INCLUDE_DIR "${IMGUIZMO_SOURCE_DIR}/src")
        set(IMGUIZMO_CPP "${IMGUIZMO_SOURCE_DIR}/src/ImGuizmo.cpp")
    endif()

    if(NOT IS_DIRECTORY "${IMGUIZMO_SOURCE_DIR}" OR NOT EXISTS "${IMGUIZMO_CPP}")
        message(FATAL_ERROR
            "ImGuizmo sources not found. Expected at: ${IMGUIZMO_SOURCE_DIR}\n"
            "Checked files:\n"
            "  ${IMGUIZMO_SOURCE_DIR}/ImGuizmo.cpp\n"
            "  ${IMGUIZMO_SOURCE_DIR}/src/ImGuizmo.cpp\n"
            "Set -DINTRINSIC_IMGUIZMO_SOURCE_DIR=/path/to/ImGuizmo or ensure FetchContent/offline cache is populated."
        )
    endif()
endif()

# Create a library for ImGui to make linking easier
# We also include the backend sources for GLFW and Vulkan
if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    add_library(imgui_lib STATIC
            ${IMGUI_SOURCE_DIR}/imgui.cpp
            ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
            ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
            ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
            ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
            ${IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    )
    target_include_directories(imgui_lib PUBLIC ${IMGUI_SOURCE_DIR} ${IMGUI_SOURCE_DIR}/backends)
    target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES GLFW_INCLUDE_NONE)
    target_link_libraries(imgui_lib PUBLIC glfw volk)

    add_library(imguizmo_lib STATIC
            ${IMGUIZMO_CPP}
    )
    target_include_directories(imguizmo_lib PUBLIC ${IMGUI_SOURCE_DIR} ${IMGUI_SOURCE_DIR}/backends ${IMGUIZMO_INCLUDE_DIR})
    target_compile_definitions(imguizmo_lib PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES GLFW_INCLUDE_NONE)
    target_link_libraries(imguizmo_lib PUBLIC imgui_lib)
endif()
