cmake_minimum_required(VERSION 3.28)

# ------------------------------------------------------------------------------
# In-source build guard — prevents accidental 'cmake .' from poisoning the
# source tree with a CMakeCache.txt that blocks CLion / other generators.
# Always use an out-of-source build directory (cmake-build-debug/, build/, etc.)
# ------------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed.\n"
        "Please create a separate build directory and run CMake from there:\n"
        "  mkdir build && cd build && cmake ..\n"
        "You may need to delete the CMakeCache.txt and CMakeFiles/ already\n"
        "created in the source root before this guard can take effect.")
endif()

# Project Definition
project(IntrinsicEngine
        VERSION 0.1.0
        LANGUAGES CXX C
)

# ------------------------------------------------------------------------------
# Manually point CMake to the dependency scanner
# ------------------------------------------------------------------------------
# Check if the user/IDE provided a scanner, otherwise try to find one
if(NOT CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
    # Search for versioned binaries first, then generic
    find_program(CLANG_SCAN_DEPS_PATH
            NAMES
            clang-scan-deps-23
            clang-scan-deps-22
            clang-scan-deps-20
            clang-scan-deps
            HINTS
            $ENV{LLVM_DIR}/bin
    )

    if(CLANG_SCAN_DEPS_PATH)
        message(STATUS "Found clang-scan-deps: ${CLANG_SCAN_DEPS_PATH}")
        set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${CLANG_SCAN_DEPS_PATH}" CACHE FILEPATH "Force scanner" FORCE)
    else()
        # Fallback: Check if the compiler itself can tell us
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-prog-name=clang-scan-deps
                OUTPUT_VARIABLE COMPILER_HINT_PATH
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(EXISTS "${COMPILER_HINT_PATH}")
            set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${COMPILER_HINT_PATH}" CACHE FILEPATH "Force scanner" FORCE)
        else()
            message(FATAL_ERROR "Could not find clang-scan-deps. Ensure Clang Tools 20+ are in your PATH.")
        endif()
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

# Sanitizer support — gated behind INTRINSIC_ENABLE_SANITIZERS.
# Auto-detects whether the Clang ASan runtime is available; disables gracefully
# in container / CI environments that ship Clang but not compiler-rt.
include(CheckCXXSourceCompiles)
set(_SAVED_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
check_cxx_source_compiles("int main(){return 0;}" INTRINSIC_ASAN_AVAILABLE)
set(CMAKE_REQUIRED_FLAGS "${_SAVED_CMAKE_REQUIRED_FLAGS}")

option(INTRINSIC_ENABLE_SANITIZERS
    "Enable AddressSanitizer + UBSan in Debug/RelWithDebInfo builds"
    ${INTRINSIC_ASAN_AVAILABLE}
)

set(INTRINSIC_DEBUG_FLAGS "")
if(INTRINSIC_ENABLE_SANITIZERS)
    set(INTRINSIC_DEBUG_FLAGS
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        $<$<CONFIG:RelWithDebInfo>:-fsanitize=address,undefined>
        $<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>
    )
    message(STATUS "Sanitizers ENABLED (ASan + UBSan) for Debug/RelWithDebInfo builds")
else()
    message(STATUS "Sanitizers DISABLED (libclang_rt.asan not found or manually turned off)")
endif()
option(INTRINSIC_ENABLE_NATIVE_ARCH "Enable -march=native for local profiling builds" OFF)
option(INTRINSIC_ENABLE_CUDA "Enable CUDA compute backend for GPU-accelerated geometry processing" OFF)
option(INTRINSIC_BUILD_SANDBOX "Build Sandbox application target" ON)
option(INTRINSIC_BUILD_TESTS "Build test targets" ON)
option(INTRINSIC_BUILD_BENCHMARKS "Build benchmark scaffold targets" ON)
option(INTRINSIC_HEADLESS_NO_GLFW
    "Headless build mode that skips GLFW-dependent modules when X11 RandR headers are unavailable"
    OFF
)
option(INTRINSIC_OFFLINE_DEPS
    "Disable network dependency fetches and require pre-populated external/cache/<name>-src trees"
    OFF
)

set(INTRINSIC_RELEASE_FLAGS
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O3>
)

if(INTRINSIC_ENABLE_NATIVE_ARCH)
    # Optional: Optimize for the specific CPU you are compiling on (Faster, but not portable)
    list(APPEND INTRINSIC_RELEASE_FLAGS
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-march=native>
    )
endif()

set(INTRINSIC_COMPILE_FLAGS
        ${INTRINSIC_BASE_FLAGS}
        ${INTRINSIC_DEBUG_FLAGS}
        ${INTRINSIC_RELEASE_FLAGS}
)
set(INTRINSIC_LINK_FLAGS
        ${INTRINSIC_DEBUG_FLAGS} # Sanitizers need to link too!
        # Link-time optimization (LTO) could go here in the future
)

add_library(IntrinsicConfig INTERFACE)
target_compile_definitions(IntrinsicConfig INTERFACE
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_RIGHT_HANDED
)

include(cmake/Dependencies.cmake)
include(cmake/IntrinsicModule.cmake)

# ------------------------------------------------------------------------------
# CUDA Compute Backend (optional)
# ------------------------------------------------------------------------------
if(INTRINSIC_ENABLE_CUDA)
    # Enable CUDA language support and find the CUDA driver API.
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    message(STATUS "CUDA compute backend ENABLED (CUDAToolkit ${CUDAToolkit_VERSION})")
    # Propagate to all targets that need conditional CUDA compilation.
    target_compile_definitions(IntrinsicConfig INTERFACE INTRINSIC_HAS_CUDA=1)
else()
    message(STATUS "CUDA compute backend DISABLED")
endif()

if(INTRINSIC_BUILD_TESTS)
    include(CTest)
endif()

# ------------------------------------------------------------------------------
# Source-root path abstraction (RORG-091)
# ------------------------------------------------------------------------------
# These variables centralize source tree paths so migration tasks can repoint
# directories without touching every add_subdirectory call at once.
set(INTRINSIC_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(INTRINSIC_SOURCE_NEW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src_new")

set(INTRINSIC_LEGACY_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/legacy")
set(INTRINSIC_CORE_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/core")
set(INTRINSIC_LEGACY_CORE_SOURCE_ROOT "${INTRINSIC_LEGACY_SOURCE_ROOT}/Core")
set(INTRINSIC_GEOMETRY_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/geometry")
set(INTRINSIC_LEGACY_ECS_SOURCE_ROOT "${INTRINSIC_LEGACY_SOURCE_ROOT}/ECS")
set(INTRINSIC_LEGACY_RHI_SOURCE_ROOT "${INTRINSIC_LEGACY_SOURCE_ROOT}/RHI")
set(INTRINSIC_LEGACY_ASSET_SOURCE_ROOT "${INTRINSIC_LEGACY_SOURCE_ROOT}/Asset")
set(INTRINSIC_LEGACY_GRAPHICS_SOURCE_ROOT "${INTRINSIC_LEGACY_SOURCE_ROOT}/Graphics")
set(INTRINSIC_LEGACY_INTERFACE_SOURCE_ROOT "${INTRINSIC_LEGACY_SOURCE_ROOT}/Interface")
set(INTRINSIC_LEGACY_EDITOR_UI_SOURCE_ROOT "${INTRINSIC_LEGACY_SOURCE_ROOT}/EditorUI")
set(INTRINSIC_LEGACY_RUNTIME_SOURCE_ROOT "${INTRINSIC_LEGACY_SOURCE_ROOT}/Runtime")
set(INTRINSIC_LEGACY_SANDBOX_SOURCE_ROOT "${INTRINSIC_LEGACY_SOURCE_ROOT}/Apps/Sandbox")

set(INTRINSIC_ASSETS_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/assets")
set(INTRINSIC_ECS_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/ecs")
set(INTRINSIC_GRAPHICS_RHI_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/graphics/rhi")
set(INTRINSIC_GRAPHICS_VULKAN_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/graphics/vulkan")

set(INTRINSIC_PLATFORM_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/platform")
set(INTRINSIC_RUNTIME_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/runtime")
set(INTRINSIC_APP_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/app")
set(INTRINSIC_PROMOTED_SOURCE_ROOT "${INTRINSIC_SOURCE_NEW_ROOT}")
set(INTRINSIC_MODULE_INVENTORY_ROOT "${INTRINSIC_PROMOTED_SOURCE_ROOT}")

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------
add_subdirectory(${INTRINSIC_LEGACY_CORE_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_CORE_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_GEOMETRY_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_ASSETS_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_ECS_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_GRAPHICS_RHI_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_PLATFORM_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_RUNTIME_SOURCE_ROOT})
if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    add_subdirectory(${INTRINSIC_APP_SOURCE_ROOT})
endif()
add_subdirectory(${INTRINSIC_LEGACY_ECS_SOURCE_ROOT})
if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    add_subdirectory(${INTRINSIC_LEGACY_RHI_SOURCE_ROOT})
    add_subdirectory(${INTRINSIC_LEGACY_ASSET_SOURCE_ROOT})       # needs Core + RHI
    add_subdirectory(${INTRINSIC_LEGACY_GRAPHICS_SOURCE_ROOT})    # needs Asset
    add_subdirectory(${INTRINSIC_LEGACY_INTERFACE_SOURCE_ROOT})
    add_subdirectory(${INTRINSIC_LEGACY_EDITOR_UI_SOURCE_ROOT})
    add_subdirectory(${INTRINSIC_LEGACY_RUNTIME_SOURCE_ROOT})     # needs Asset
else()
    message(WARNING
        "INTRINSIC_HEADLESS_NO_GLFW=ON: skipping legacy GLFW-dependent modules "
        "(RHI/Asset/Graphics/Interface/EditorUI/Runtime)."
    )
endif()

add_subdirectory(${INTRINSIC_PROMOTED_SOURCE_ROOT})

if(INTRINSIC_BUILD_SANDBOX AND NOT INTRINSIC_HEADLESS_NO_GLFW)
    add_subdirectory(${INTRINSIC_LEGACY_SANDBOX_SOURCE_ROOT})
endif()
if(INTRINSIC_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(INTRINSIC_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
