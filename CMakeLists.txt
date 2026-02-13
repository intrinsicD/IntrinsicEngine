
cmake_minimum_required(VERSION 3.28)

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
            clang-scan-deps-18
            clang-scan-deps-19
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
            message(FATAL_ERROR "Could not find clang-scan-deps. Ensure Clang Tools 18+ are in your PATH.")
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

# Clang 18 reports __cpp_concepts as 201907L but GCC 14's <expected> header
# guards on >= 202002L.  Override so libstdc++-14 C++23 features are available.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19")
    list(APPEND INTRINSIC_BASE_FLAGS -Wno-builtin-macro-redefined -D__cpp_concepts=202002L)
endif()

# Add sanitizers to RelWithDebInfo for better testing coverage
set(INTRINSIC_DEBUG_FLAGS
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
    $<$<CONFIG:RelWithDebInfo>:-fsanitize=address,undefined>
    $<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer>
)
set(INTRINSIC_RELEASE_FLAGS
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O3>

        # Optional: Optimize for the specific CPU you are compiling on (Faster, but not portable)
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-march=native>
)
option(INTRINSIC_ENABLE_NATIVE_ARCH "Enable -march=native for local profiling builds" OFF)

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

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------
add_subdirectory(src/Core)
add_subdirectory(src/Runtime)
add_subdirectory(src/Apps/Sandbox)
add_subdirectory(tests)
