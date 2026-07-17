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

# ------------------------------------------------------------------------------
# Dependency toolchain selection
#
# Presets pass this explicitly, but IDEs such as CLion often generate raw CMake
# configure commands. If the caller did not choose a toolchain, select the
# repository-local vcpkg toolchain before the first project() call so dependency
# discovery still runs through the manifest path.
# ------------------------------------------------------------------------------
set(_INTRINSIC_REPO_VCPKG_TOOLCHAIN
    "${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg/scripts/buildsystems/vcpkg.cmake")
set(_INTRINSIC_REPO_CLANG_CHAINLOAD
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/IntrinsicClangToolchain.cmake")

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE OR CMAKE_TOOLCHAIN_FILE STREQUAL "")
    if(EXISTS "${_INTRINSIC_REPO_VCPKG_TOOLCHAIN}")
        set(CMAKE_TOOLCHAIN_FILE "${_INTRINSIC_REPO_VCPKG_TOOLCHAIN}" CACHE FILEPATH
            "IntrinsicEngine repository vcpkg toolchain" FORCE)
        message(STATUS
            "IntrinsicEngine defaulting to repository vcpkg toolchain: "
            "${CMAKE_TOOLCHAIN_FILE}")
    else()
        message(FATAL_ERROR
            "IntrinsicEngine dependencies require the repository-local vcpkg "
            "toolchain at '${_INTRINSIC_REPO_VCPKG_TOOLCHAIN}'. Run "
            "tools/setup/bootstrap_vcpkg.sh or configure with an explicit "
            "vcpkg CMAKE_TOOLCHAIN_FILE.")
    endif()
endif()

get_filename_component(_INTRINSIC_ACTIVE_TOOLCHAIN
    "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
get_filename_component(_INTRINSIC_REPO_VCPKG_TOOLCHAIN_ABS
    "${_INTRINSIC_REPO_VCPKG_TOOLCHAIN}" ABSOLUTE)

if("${_INTRINSIC_ACTIVE_TOOLCHAIN}" STREQUAL "${_INTRINSIC_REPO_VCPKG_TOOLCHAIN_ABS}")
    if(NOT DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE
        OR VCPKG_CHAINLOAD_TOOLCHAIN_FILE STREQUAL "")
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${_INTRINSIC_REPO_CLANG_CHAINLOAD}"
            CACHE FILEPATH "IntrinsicEngine Clang toolchain chainloaded by vcpkg" FORCE)
    endif()
    if(NOT DEFINED VCPKG_INSTALLED_DIR OR VCPKG_INSTALLED_DIR STREQUAL "")
        get_filename_component(_INTRINSIC_BUILD_TREE_NAME "${CMAKE_BINARY_DIR}" NAME)
        if(_INTRINSIC_BUILD_TREE_NAME STREQUAL "")
            set(_INTRINSIC_BUILD_TREE_NAME "local")
        endif()
        set(VCPKG_INSTALLED_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg-installed/${_INTRINSIC_BUILD_TREE_NAME}"
            CACHE PATH "IntrinsicEngine vcpkg package install tree" FORCE)
    endif()
    if(NOT DEFINED VCPKG_MANIFEST_MODE)
        set(VCPKG_MANIFEST_MODE ON CACHE BOOL
            "Resolve IntrinsicEngine dependencies through vcpkg manifest mode" FORCE)
    endif()
endif()

unset(_INTRINSIC_ACTIVE_TOOLCHAIN)
unset(_INTRINSIC_BUILD_TREE_NAME)
unset(_INTRINSIC_REPO_CLANG_CHAINLOAD)
unset(_INTRINSIC_REPO_VCPKG_TOOLCHAIN)
unset(_INTRINSIC_REPO_VCPKG_TOOLCHAIN_ABS)

# Project Definition
project(IntrinsicEngine
        VERSION 0.1.0
        LANGUAGES CXX C
)

# Global build identity must exist before any renderer or platform consumer.
set(EXTRINSIC_PLATFORM "${CMAKE_SYSTEM_NAME}" CACHE STRING "Configured target platform selection")
set(EXTRINSIC_BACKEND "Vulkan" CACHE STRING "Graphics backend selection")

include(cmake/ClangToolchainSelection.cmake)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang"
    OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR
        "IntrinsicEngine requires LLVM Clang ${INTRINSIC_MINIMUM_CLANG_MAJOR}+ "
        "for C++23 module dependency scanning. Configure with the provided "
        "CMake presets or set both CMAKE_C_COMPILER and CMAKE_CXX_COMPILER to "
        "compatible Clang binaries.")
endif()

set(_INTRINSIC_MINIMUM_CLANG_VERSION "${INTRINSIC_MINIMUM_CLANG_MAJOR}.0")
if(CMAKE_C_COMPILER_VERSION VERSION_LESS "${_INTRINSIC_MINIMUM_CLANG_VERSION}"
    OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS "${_INTRINSIC_MINIMUM_CLANG_VERSION}")
    message(FATAL_ERROR
        "IntrinsicEngine requires Clang ${INTRINSIC_MINIMUM_CLANG_MAJOR}+; "
        "detected C=${CMAKE_C_COMPILER_VERSION} "
        "and CXX=${CMAKE_CXX_COMPILER_VERSION}.")
endif()

string(REGEX MATCH "^[0-9]+" INTRINSIC_SELECTED_CLANG_MAJOR
    "${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS
    "IntrinsicEngine Clang requirement: using C=${CMAKE_C_COMPILER_VERSION} "
    "and CXX=${CMAKE_CXX_COMPILER_VERSION}; minimum is "
    "${INTRINSIC_MINIMUM_CLANG_MAJOR}.0")

# ------------------------------------------------------------------------------
# Manually point CMake to the dependency scanner
# ------------------------------------------------------------------------------
# Check if the user/IDE provided a scanner, otherwise try to find one
if(NOT CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
    intrinsic_find_matching_clang_scan_deps(
        CLANG_SCAN_DEPS_PATH
        "${INTRINSIC_SELECTED_CLANG_MAJOR}"
    )
    if(NOT CLANG_SCAN_DEPS_PATH)
        intrinsic_find_highest_clang_scan_deps(
            CLANG_SCAN_DEPS_PATH
            CLANG_SCAN_DEPS_MAJOR
        )
    endif()

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

intrinsic_clang_major(
    _INTRINSIC_SCAN_DEPS_MAJOR
    "${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}"
)
if(NOT _INTRINSIC_SCAN_DEPS_MAJOR
    OR _INTRINSIC_SCAN_DEPS_MAJOR LESS "${INTRINSIC_MINIMUM_CLANG_MAJOR}")
    message(FATAL_ERROR
        "IntrinsicEngine requires clang-scan-deps-${INTRINSIC_MINIMUM_CLANG_MAJOR} "
        "or newer; detected '${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}'.")
endif()
if(NOT _INTRINSIC_SCAN_DEPS_MAJOR STREQUAL "${INTRINSIC_SELECTED_CLANG_MAJOR}")
    message(WARNING
        "clang-scan-deps major (${_INTRINSIC_SCAN_DEPS_MAJOR}) does not match "
        "the selected Clang major (${INTRINSIC_SELECTED_CLANG_MAJOR}). Prefer "
        "installing clang-scan-deps-${INTRINSIC_SELECTED_CLANG_MAJOR} for "
        "module-scan compatibility.")
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

# Sanitizer support. INTRINSIC_ENABLE_SANITIZERS remains the compatibility
# switch for raw callers; named presets also set the exact sanitizer mode.
include(CheckCXXSourceCompiles)
set(_SAVED_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
check_cxx_source_compiles("int main(){return 0;}" INTRINSIC_ASAN_AVAILABLE)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined")
check_cxx_source_compiles("int main(){return 0;}" INTRINSIC_UBSAN_AVAILABLE)
set(CMAKE_REQUIRED_FLAGS "${_SAVED_CMAKE_REQUIRED_FLAGS}")

option(INTRINSIC_ENABLE_SANITIZERS
    "Enable sanitizer instrumentation in Debug/RelWithDebInfo builds"
    ${INTRINSIC_ASAN_AVAILABLE}
)
set(INTRINSIC_SANITIZER_MODE "" CACHE STRING
    "Sanitizer mode; empty preserves INTRINSIC_ENABLE_SANITIZERS compatibility"
)
set_property(CACHE INTRINSIC_SANITIZER_MODE PROPERTY STRINGS
    "" none address undefined "address,undefined"
)

set(_intrinsic_allowed_sanitizer_modes
    none
    address
    undefined
    "address,undefined"
)
if(INTRINSIC_SANITIZER_MODE STREQUAL "")
    if(INTRINSIC_ENABLE_SANITIZERS)
        set(_intrinsic_sanitizer_mode "address,undefined")
    else()
        set(_intrinsic_sanitizer_mode "none")
    endif()
else()
    list(FIND _intrinsic_allowed_sanitizer_modes
        "${INTRINSIC_SANITIZER_MODE}"
        _intrinsic_sanitizer_mode_index
    )
    if(_intrinsic_sanitizer_mode_index EQUAL -1)
        message(FATAL_ERROR
            "Unsupported INTRINSIC_SANITIZER_MODE='${INTRINSIC_SANITIZER_MODE}'. "
            "Expected none, address, undefined, or address,undefined.")
    endif()
    if(INTRINSIC_SANITIZER_MODE STREQUAL "none"
       AND INTRINSIC_ENABLE_SANITIZERS)
        message(FATAL_ERROR
            "INTRINSIC_SANITIZER_MODE=none requires "
            "INTRINSIC_ENABLE_SANITIZERS=OFF.")
    elseif(NOT INTRINSIC_SANITIZER_MODE STREQUAL "none"
           AND NOT INTRINSIC_ENABLE_SANITIZERS)
        message(FATAL_ERROR
            "INTRINSIC_SANITIZER_MODE=${INTRINSIC_SANITIZER_MODE} requires "
            "INTRINSIC_ENABLE_SANITIZERS=ON.")
    endif()
    set(_intrinsic_sanitizer_mode "${INTRINSIC_SANITIZER_MODE}")
endif()

set(_intrinsic_sanitizer_identity "none")
set(_intrinsic_sanitizer_has_address OFF)
set(_intrinsic_sanitizer_has_undefined OFF)
if(_intrinsic_sanitizer_mode STREQUAL "address")
    set(_intrinsic_sanitizer_identity "asan")
    set(_intrinsic_sanitizer_has_address ON)
elseif(_intrinsic_sanitizer_mode STREQUAL "undefined")
    set(_intrinsic_sanitizer_identity "ubsan")
    set(_intrinsic_sanitizer_has_undefined ON)
elseif(_intrinsic_sanitizer_mode STREQUAL "address,undefined")
    set(_intrinsic_sanitizer_identity "asan-ubsan")
    set(_intrinsic_sanitizer_has_address ON)
    set(_intrinsic_sanitizer_has_undefined ON)
endif()

if(_intrinsic_sanitizer_has_address AND NOT INTRINSIC_ASAN_AVAILABLE)
    message(FATAL_ERROR
        "AddressSanitizer was requested, but the configured Clang ASan "
        "runtime is unavailable.")
endif()
if(_intrinsic_sanitizer_has_undefined AND NOT INTRINSIC_UBSAN_AVAILABLE)
    message(FATAL_ERROR
        "UndefinedBehaviorSanitizer was requested, but the configured Clang "
        "UBSan runtime is unavailable.")
endif()

set(INTRINSIC_SANITIZER_IDENTITY
    "${_intrinsic_sanitizer_identity}"
    CACHE INTERNAL "Resolved sanitizer telemetry identity" FORCE
)
set(INTRINSIC_SANITIZER_HAS_ADDRESS
    "${_intrinsic_sanitizer_has_address}"
    CACHE INTERNAL "Whether AddressSanitizer instrumentation is active" FORCE
)
set(INTRINSIC_SANITIZER_HAS_UNDEFINED
    "${_intrinsic_sanitizer_has_undefined}"
    CACHE INTERNAL "Whether UBSan instrumentation is active" FORCE
)

option(INTRINSIC_ENABLE_SOURCE_COVERAGE
    "Enable Clang source-based code coverage instrumentation"
    OFF
)

if(INTRINSIC_ENABLE_SOURCE_COVERAGE
   AND NOT INTRINSIC_SANITIZER_IDENTITY STREQUAL "none")
    message(FATAL_ERROR
        "INTRINSIC_ENABLE_SOURCE_COVERAGE and sanitizer instrumentation are "
        "mutually exclusive. Use the unsanitized ci-coverage-cpu preset for "
        "source coverage.")
endif()
if(INTRINSIC_ENABLE_SOURCE_COVERAGE
    AND (NOT CMAKE_C_COMPILER_ID STREQUAL "Clang"
        OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    message(FATAL_ERROR
        "INTRINSIC_ENABLE_SOURCE_COVERAGE requires LLVM Clang source-based "
        "coverage support.")
endif()

set(INTRINSIC_DEBUG_FLAGS "")
if(NOT INTRINSIC_SANITIZER_IDENTITY STREQUAL "none")
    set(INTRINSIC_DEBUG_FLAGS
        $<$<CONFIG:Debug>:-fsanitize=${_intrinsic_sanitizer_mode}>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        $<$<CONFIG:RelWithDebInfo>:-fsanitize=${_intrinsic_sanitizer_mode}>
        $<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>
    )
    if(INTRINSIC_SANITIZER_HAS_UNDEFINED)
        list(APPEND INTRINSIC_DEBUG_FLAGS
            $<$<CONFIG:Debug>:-fno-sanitize-recover=undefined>
            $<$<CONFIG:RelWithDebInfo>:-fno-sanitize-recover=undefined>
        )
    endif()
    message(STATUS
        "Sanitizers ENABLED (${INTRINSIC_SANITIZER_IDENTITY}; "
        "${_intrinsic_sanitizer_mode}) for Debug/RelWithDebInfo builds")
else()
    message(STATUS "Sanitizers DISABLED")
endif()

set(INTRINSIC_COVERAGE_COMPILE_FLAGS "")
set(INTRINSIC_COVERAGE_LINK_FLAGS "")
set(INTRINSIC_SOURCE_COVERAGE_PROFILE_UPDATE
    "none"
    CACHE INTERNAL "Source-coverage profile counter update mode" FORCE
)
if(INTRINSIC_ENABLE_SOURCE_COVERAGE)
    set(INTRINSIC_SOURCE_COVERAGE_PROFILE_UPDATE
        "atomic"
        CACHE INTERNAL "Source-coverage profile counter update mode" FORCE
    )
    set(INTRINSIC_COVERAGE_COMPILE_FLAGS
        -fprofile-instr-generate
        -fprofile-update=atomic
        -fcoverage-mapping
    )
    set(INTRINSIC_COVERAGE_LINK_FLAGS
        -fprofile-instr-generate
    )
    message(STATUS
        "Clang source coverage instrumentation ENABLED "
        "(profile updates: ${INTRINSIC_SOURCE_COVERAGE_PROFILE_UPDATE})")
else()
    message(STATUS "Clang source coverage instrumentation DISABLED")
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
        ${INTRINSIC_COVERAGE_COMPILE_FLAGS}
        ${INTRINSIC_RELEASE_FLAGS}
)
set(INTRINSIC_LINK_FLAGS
        ${INTRINSIC_DEBUG_FLAGS} # Sanitizers need to link too!
        ${INTRINSIC_COVERAGE_LINK_FLAGS}
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
if(BUILD_TESTING)
    include(cmake/IntrinsicTest.cmake)
endif()

# ------------------------------------------------------------------------------
# Source-root path abstraction (RORG-091)
# ------------------------------------------------------------------------------
# These variables centralize source tree paths so migration tasks can repoint
# directories without touching every add_subdirectory call at once.
set(INTRINSIC_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(INTRINSIC_CORE_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/core")
set(INTRINSIC_GEOMETRY_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/geometry")
set(INTRINSIC_ASSETS_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/assets")
set(INTRINSIC_ECS_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/ecs")
set(INTRINSIC_PHYSICS_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/physics")
set(INTRINSIC_GRAPHICS_RHI_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/graphics/rhi")
set(INTRINSIC_GRAPHICS_VULKAN_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/graphics/vulkan")
set(INTRINSIC_GRAPHICS_RENDERER_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/graphics/renderer")
set(INTRINSIC_GRAPHICS_RENDERERGRAPH_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/graphics/framegraph")
set(INTRINSIC_GRAPHICS_ASSETS_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/graphics/assets")
set(INTRINSIC_PLATFORM_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/platform")
set(INTRINSIC_RUNTIME_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/runtime")
set(INTRINSIC_APP_SOURCE_ROOT "${INTRINSIC_SOURCE_ROOT}/app")
set(INTRINSIC_MODULE_INVENTORY_ROOT "${INTRINSIC_SOURCE_ROOT}")

# METHOD-012 CPU reference package. Keep it as a plain hermetic C++ target so
# runtime/UI callers, method tests, and smoke benchmarks share one definition.
add_library(IntrinsicProgressivePoissonReference STATIC
        methods/geometry/progressive_poisson/src/ProgressivePoissonReference.cpp
)
target_compile_features(IntrinsicProgressivePoissonReference PUBLIC cxx_std_23)
target_compile_options(IntrinsicProgressivePoissonReference PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_link_options(IntrinsicProgressivePoissonReference PRIVATE ${INTRINSIC_LINK_FLAGS})
target_include_directories(IntrinsicProgressivePoissonReference PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/methods/geometry/progressive_poisson/include
)
target_link_libraries(IntrinsicProgressivePoissonReference PUBLIC IntrinsicConfig glm::glm)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------
add_subdirectory(${INTRINSIC_CORE_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_GEOMETRY_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_ASSETS_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_ECS_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_PHYSICS_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_GRAPHICS_RHI_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_GRAPHICS_ASSETS_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_GRAPHICS_RENDERER_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_GRAPHICS_RENDERERGRAPH_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_PLATFORM_SOURCE_ROOT})
add_subdirectory(${INTRINSIC_RUNTIME_SOURCE_ROOT})
if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    add_subdirectory(${INTRINSIC_APP_SOURCE_ROOT})
endif()

if(INTRINSIC_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(INTRINSIC_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)

    intrinsic_write_test_registry()
    intrinsic_add_test_aggregate(NAME IntrinsicTests)
    intrinsic_add_test_aggregate(
        NAME IntrinsicPrFastTests
        INCLUDE_ANY unit contract
        EXCLUDE_ANY gpu vulkan slow flaky-quarantine
    )
    intrinsic_add_test_aggregate(
        NAME IntrinsicCpuTests
        EXCLUDE_ANY gpu vulkan slow flaky-quarantine
    )
    intrinsic_add_test_aggregate(
        NAME IntrinsicCpuCoverageTests
        EXCLUDE_ANY benchmark slo gpu vulkan flaky-quarantine
    )
    intrinsic_add_test_aggregate(
        NAME IntrinsicCpuSlowTests
        OMIT_IF_EMPTY
        INCLUDE_ALL slow
        EXCLUDE_ANY benchmark slo gpu vulkan flaky-quarantine
    )
    if(TARGET ExtrinsicBackendsVulkan)
        intrinsic_add_test_aggregate(
            NAME IntrinsicGpuVulkanTests
            INCLUDE_ALL gpu vulkan
            EXCLUDE_ANY slow flaky-quarantine
        )
    endif()
    intrinsic_add_test_aggregate(
        NAME IntrinsicPrSmokeTests
        INCLUDE_ALL integration runtime graphics
        EXCLUDE_ANY gpu vulkan slow flaky-quarantine
    )
endif()
