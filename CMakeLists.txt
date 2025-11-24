cmake_minimum_required(VERSION 3.28)

# Project Definition
project(IntrinsicEngine
        VERSION 0.1.0
        LANGUAGES CXX C
)

# ------------------------------------------------------------------------------
# CRITICAL FIX: Manually point CMake to the dependency scanner
# ------------------------------------------------------------------------------
# Check if the user/IDE provided a scanner, otherwise try to find one
if(NOT CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
    find_program(CLANG_SCAN_DEPS_PATH NAMES clang-scan-deps-18 clang-scan-deps-22 clang-scan-deps)
    if(CLANG_SCAN_DEPS_PATH)
        message(STATUS "Found clang-scan-deps: ${CLANG_SCAN_DEPS_PATH}")
        set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${CLANG_SCAN_DEPS_PATH}" CACHE FILEPATH "Force scanner" FORCE)
    else()
        message(FATAL_ERROR "Could not find clang-scan-deps. Please install clang-tools-18+.")
    endif()
endif()

# ------------------------------------------------------------------------------
# Standard Settings
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output Directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ------------------------------------------------------------------------------
# Our Strict Flags
# ------------------------------------------------------------------------------
set(INTRINSIC_BASE_FLAGS
        -Wall -Wextra -Wpedantic
        -Wno-unknown-pragmas
        -Werror=return-type
        -fno-exceptions
        -fno-rtti
)

set(INTRINSIC_DEBUG_FLAGS
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
)
set(INTRINSIC_RELEASE_FLAGS
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O3>

        # Optional: Optimize for the specific CPU you are compiling on (Faster, but not portable)
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-march=native>
)

set(INTRINSIC_COMPILE_FLAGS
        ${INTRINSIC_BASE_FLAGS}
        ${INTRINSIC_DEBUG_FLAGS}
        ${INTRINSIC_RELEASE_FLAGS}
)
set(INTRINSIC_LINK_FLAGS
        ${INTRINSIC_DEBUG_FLAGS} # Sanitizers need to link too!
        # Link-time optimization (LTO) could go here in the future
)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
# This prevents re-downloading/unzipping when you clean the build folder.
get_filename_component(ROOT_DIR ${CMAKE_SOURCE_DIR} ABSOLUTE)
set(FETCHCONTENT_BASE_DIR "${ROOT_DIR}/external/cache")

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Using CCache: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

include(FetchContent)

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
FetchContent_MakeAvailable(glm)

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
FetchContent_MakeAvailable(googletest)

# --- GLFW ---
FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.9
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# --- Volk (Vulkan Meta Loader) ---
FetchContent_Declare(
        volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG master
)
FetchContent_MakeAvailable(volk)
find_package(Vulkan REQUIRED)

FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.0.1
)
# Manually populate to avoid building their library target
FetchContent_GetProperties(vma)
if(NOT vma_POPULATED)
    FetchContent_Populate(vma)
endif()

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(
        entt
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG v3.13.0 # Use a stable tag
)
FetchContent_MakeAvailable(entt)

# --- JSON (Required for TinyGLTF) ---
FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# --- TinyGLTF ---
FetchContent_Declare(
        tinygltf
        GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
        GIT_TAG v2.9.0
)
FetchContent_MakeAvailable(tinygltf)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------
add_subdirectory(src/Core)
add_subdirectory(tests)
add_subdirectory(src/Apps/Sandbox)
add_subdirectory(src/Runtime)
