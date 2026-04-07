# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
# This prevents re-downloading/unzipping when you clean the build folder.
get_filename_component(ROOT_DIR ${CMAKE_SOURCE_DIR} ABSOLUTE)

# IMPORTANT: FetchContent only honors FETCHCONTENT_BASE_DIR when it's a CACHE variable.
# If it's a normal variable, CMake will default to <build>/_deps and <name>_SOURCE_DIR
# can end up unset/empty in some policy/config combinations.
if(NOT DEFINED FETCHCONTENT_BASE_DIR OR "${FETCHCONTENT_BASE_DIR}" STREQUAL "")
    set(FETCHCONTENT_BASE_DIR "${ROOT_DIR}/external/cache" CACHE PATH "FetchContent base directory" FORCE)
else()
    # Re-export it as a cache variable to ensure FetchContent honors it.
    set(FETCHCONTENT_BASE_DIR "${FETCHCONTENT_BASE_DIR}" CACHE PATH "FetchContent base directory" FORCE)
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Using CCache: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

include(FetchContent)

if(INTRINSIC_OFFLINE_DEPS)
    set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "Disable all FetchContent network updates" FORCE)
    message(STATUS "INTRINSIC_OFFLINE_DEPS=ON: using only local dependency sources")
endif()

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

function(intrinsic_make_available dep_name)
    if(INTRINSIC_OFFLINE_DEPS)
        intrinsic_require_offline_source(${dep_name})
    endif()
    FetchContent_MakeAvailable(${dep_name})
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
set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
intrinsic_make_available(googletest)

# --- GLFW ---
FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.9
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
intrinsic_make_available(glfw)

# --- Volk (Vulkan Meta Loader) ---
# Pin to stable commit instead of master
FetchContent_Declare(
        volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG vulkan-sdk-1.3.268.0
)
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

intrinsic_make_available(vma)


# Pin STB to specific commit for reproducible builds
FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG 5736b15f7ea0ffb08dd38af21067c314d6a3aae9  # 2023-01-30
)

intrinsic_make_available(stb)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

FetchContent_Declare(
        entt
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG v3.13.0 # Use a stable tag
)
intrinsic_make_available(entt)

# --- JSON (Required for TinyGLTF) ---
FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)
intrinsic_make_available(json)

# --- Draco (Required for glTF KHR_draco_mesh_compression) ---
set(DRACO_INSTALL OFF CACHE BOOL "Disable Draco install targets" FORCE)
set(DRACO_TESTS OFF CACHE BOOL "Disable Draco tests" FORCE)
FetchContent_Declare(
        draco
        GIT_REPOSITORY https://github.com/google/draco.git
        GIT_TAG 1.5.7
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
intrinsic_make_available(tinygltf)

# --- ImGui (Docking Branch) ---
# Pin to specific docking branch commit for stability
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.5-docking
)

# ImGui is not a first-class CMake project; depending on CMake policies/version,
# FetchContent_MakeAvailable(imgui) may not leave us with a reliable imgui_SOURCE_DIR.
# We therefore ensure the content is populated and then resolve the source path.
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

FetchContent_Declare(
        imguizmo
        GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
)

intrinsic_make_available(imguizmo)

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

if("${IMGUIZMO_SOURCE_DIR}" STREQUAL "")
    set(IMGUIZMO_SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/imguizmo-src")
endif()


if(NOT IS_DIRECTORY "${IMGUIZMO_SOURCE_DIR}" OR NOT EXISTS "${IMGUIZMO_SOURCE_DIR}/ImGuizmo.cpp")
    message(FATAL_ERROR
        "ImGuizmo sources not found. Expected at: ${IMGUIZMO_SOURCE_DIR}\n"
        "Missing file: ${IMGUIZMO_SOURCE_DIR}/ImGuizmo.cpp\n"
        "Set -DINTRINSIC_IMGUIZMO_SOURCE_DIR=/path/to/ImGuizmo or ensure FetchContent/offline cache is populated."
    )
endif()

# Create a library for ImGui to make linking easier
# We also include the backend sources for GLFW and Vulkan
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
        ${IMGUIZMO_SOURCE_DIR}/ImGuizmo.cpp
)
target_include_directories(imguizmo_lib PUBLIC ${IMGUI_SOURCE_DIR} ${IMGUI_SOURCE_DIR}/backends ${IMGUIZMO_SOURCE_DIR})
target_compile_definitions(imguizmo_lib PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES GLFW_INCLUDE_NONE)
target_link_libraries(imguizmo_lib PUBLIC imgui_lib)
