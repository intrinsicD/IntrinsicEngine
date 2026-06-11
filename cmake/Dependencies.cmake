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
    # BUG-015: this repository builds C++23 named modules with clang. In ccache's
    # default direct/preprocessor modes the hash of a module-consuming TU can miss
    # imported BMI contents, so a module interface layout change can reuse an
    # object compiled against the old layout. That produces silent ABI drift
    # between separately built targets (for example, vector-heavy exported
    # structs or virtual-call slots). Use depend mode and disable direct mode for
    # preset builds so ccache keys module importers through compiler-generated
    # dependency data instead of a direct source hash.
    set(_intrinsic_ccache_launcher
        "${CMAKE_COMMAND}" -E env CCACHE_DEPEND=1 CCACHE_NODIRECT=1 "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER ${_intrinsic_ccache_launcher})
    set(CMAKE_C_COMPILER_LAUNCHER ${_intrinsic_ccache_launcher})
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

if(DEFINED VCPKG_MANIFEST_MODE AND VCPKG_MANIFEST_MODE)
    set(_intrinsic_default_use_vcpkg_deps ON)
else()
    set(_intrinsic_default_use_vcpkg_deps OFF)
endif()
option(INTRINSIC_USE_VCPKG_DEPS
    "Resolve third-party dependencies through the repository vcpkg manifest"
    ${_intrinsic_default_use_vcpkg_deps}
)
unset(_intrinsic_default_use_vcpkg_deps)

if(INTRINSIC_USE_VCPKG_DEPS)
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

    return()
endif()

#
# INFRA Option A — trust a populated cache by default.
#
# Historically every configure re-validated, re-checked, and occasionally
# *deleted* the dependency cache on the slightest filesystem race, leading to
# 4-minute configures and "phantom" cache-corruption rebuilds. The new policy:
#
#   * If the cache directory already contains any `*-src/` tree, we assume it
#     is good and run fully disconnected by default (no network calls, no
#     update probes). Set `INTRINSIC_UPDATE_DEPS=ON` to opt back into update
#     probes, or `INTRINSIC_OFFLINE_DEPS=ON` to enforce strict offline checks.
#   * `INTRINSIC_DEPS_SEAL=ON` (default ON whenever the cache is hot) makes
#     `intrinsic_validate_dependency_source` non-destructive: a missing file
#     becomes a hard error with a manual-recovery hint, never a silent
#     `file(REMOVE_RECURSE)` on the cached source tree.
#
# Run `tools/setup/populate_deps.sh` once on a fresh checkout to hydrate the
# cache online; every subsequent configure is offline and fast.
file(GLOB _intrinsic_cached_dep_sources LIST_DIRECTORIES true
        "${FETCHCONTENT_BASE_DIR}/*-src")
list(LENGTH _intrinsic_cached_dep_sources _intrinsic_cached_dep_count)
unset(_intrinsic_cached_dep_sources)

if(_intrinsic_cached_dep_count GREATER 0)
    set(_intrinsic_cache_hot TRUE)
else()
    set(_intrinsic_cache_hot FALSE)
endif()

if(NOT DEFINED INTRINSIC_DEPS_SEAL)
    set(INTRINSIC_DEPS_SEAL ${_intrinsic_cache_hot} CACHE BOOL
        "Treat the populated dependency cache as authoritative; never auto-delete on a validation miss" FORCE)
endif()

if(INTRINSIC_OFFLINE_DEPS)
    set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "Disable all FetchContent network updates" FORCE)
    message(STATUS "INTRINSIC_OFFLINE_DEPS=ON: using only local dependency sources")
elseif(INTRINSIC_UPDATE_DEPS)
    # Explicit opt-in to update probes; FetchContent runs normally.
    message(STATUS "INTRINSIC_UPDATE_DEPS=ON: FetchContent may contact remotes for updates")
elseif(_intrinsic_cache_hot)
    # Cache already hydrated → no network at all. Configure is a no-op.
    set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL
        "Cache is hot; skip all FetchContent network activity" FORCE)
    message(STATUS "FetchContent fully disconnected (cache hot at ${FETCHCONTENT_BASE_DIR}). "
                   "Set -DINTRINSIC_UPDATE_DEPS=ON to refresh.")
else()
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
        "Skip network update checks for already-populated FetchContent sources" FORCE)
endif()

unset(_intrinsic_cached_dep_count)
unset(_intrinsic_cache_hot)

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
    # INFRA Option A — never auto-delete a cached source tree. A missing
    # file is almost always a transient stat() race under parallel scan-deps;
    # silently wiping the cache turned blips into 4-minute re-fetches. Demand
    # explicit manual recovery instead.
    if(phase STREQUAL "pre" AND NOT INTRINSIC_OFFLINE_DEPS AND NOT INTRINSIC_DEPS_SEAL)
        message(WARNING
            "Dependency cache for '${dep_name}' appears incomplete at: ${dep_source_dir}\n"
            "Missing required file(s):\n  - ${missing_file_list}\n"
            "FetchContent will be allowed to attempt re-population. If you see this repeatedly, "
            "manually `rm -rf ${dep_source_dir} ${FETCHCONTENT_BASE_DIR}/${dep_name}-build "
            "${FETCHCONTENT_BASE_DIR}/${dep_name}-subbuild` and re-run configure with network access."
        )
        return()
    endif()

    message(FATAL_ERROR
        "Dependency cache for '${dep_name}' is incomplete at: ${dep_source_dir}\n"
        "Missing required file(s):\n  - ${missing_file_list}\n"
        "Recovery: manually `rm -rf ${dep_source_dir} ${FETCHCONTENT_BASE_DIR}/${dep_name}-build "
        "${FETCHCONTENT_BASE_DIR}/${dep_name}-subbuild` then rerun configure with network access "
        "(or with `-DINTRINSIC_UPDATE_DEPS=ON`)."
    )
endfunction()

# INFRA Option A — fast-path: when the cache is sealed and the
# required marker files are present, we can skip the locking + validation
# dance entirely on the hot path. Pre-validate quickly without touching
# the filesystem more than necessary.
function(_intrinsic_cache_is_hydrated out_var dep_name)
    get_property(required_files GLOBAL PROPERTY "INTRINSIC_DEP_REQUIRED_FILES_${dep_name}")
    if(NOT required_files)
        set(${out_var} FALSE PARENT_SCOPE)
        return()
    endif()
    intrinsic_dependency_source_dir(dep_source_dir ${dep_name})
    if(NOT IS_DIRECTORY "${dep_source_dir}")
        set(${out_var} FALSE PARENT_SCOPE)
        return()
    endif()
    foreach(required_file IN LISTS required_files)
        if(NOT EXISTS "${dep_source_dir}/${required_file}")
            set(${out_var} FALSE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} TRUE PARENT_SCOPE)
endfunction()

function(intrinsic_make_available dep_name)
    if(INTRINSIC_DEPS_SEAL)
        _intrinsic_cache_is_hydrated(_hydrated ${dep_name})
        if(_hydrated)
            # Trust the cache. Still call FetchContent_MakeAvailable so the
            # dependency's add_subdirectory() runs and defines its targets, but
            # skip the lock + validation passes that dominate configure time.
            FetchContent_MakeAvailable(${dep_name})
            return()
        endif()
    endif()
    intrinsic_lock_dependency(${dep_name})
    if(INTRINSIC_OFFLINE_DEPS)
        intrinsic_require_offline_source(${dep_name})
    endif()
    intrinsic_validate_dependency_source(${dep_name} pre)
    FetchContent_MakeAvailable(${dep_name})
    intrinsic_validate_dependency_source(${dep_name} post)
endfunction()

function(intrinsic_populate_source dep_name)
    if(INTRINSIC_DEPS_SEAL)
        _intrinsic_cache_is_hydrated(_hydrated ${dep_name})
        if(_hydrated)
            FetchContent_GetProperties(${dep_name})
            intrinsic_dependency_source_dir(dep_source_dir ${dep_name})
            if(NOT ${dep_name}_POPULATED)
                # Tell FetchContent the source is already on disk so it skips
                # any download/extract; we just need the *_SOURCE_DIR var set.
                FetchContent_Populate(${dep_name})
            endif()
            FetchContent_GetProperties(${dep_name})
            set(${dep_name}_SOURCE_DIR "${${dep_name}_SOURCE_DIR}" PARENT_SCOPE)
            set(${dep_name}_BINARY_DIR "${${dep_name}_BINARY_DIR}" PARENT_SCOPE)
            set(${dep_name}_POPULATED "${${dep_name}_POPULATED}" PARENT_SCOPE)
            return()
        endif()
    endif()
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

# --- Eigen3 ---
set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(EIGEN_MPL2_ONLY ON CACHE BOOL "Restrict Eigen to MPL2-licensed code paths" FORCE)
FetchContent_Declare(
        eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG 3.4.0
)
intrinsic_set_dependency_required_files(eigen
        CMakeLists.txt
        Eigen/Core
        Eigen/Dense
        Eigen/Sparse
        Eigen/SVD
)
intrinsic_populate_source(eigen)
if("${eigen_SOURCE_DIR}" STREQUAL "")
    set(eigen_SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/eigen-src")
endif()
if(NOT IS_DIRECTORY "${eigen_SOURCE_DIR}" OR NOT EXISTS "${eigen_SOURCE_DIR}/Eigen/Core")
    message(FATAL_ERROR
        "Eigen sources not found. Expected at: ${eigen_SOURCE_DIR}\n"
        "Missing file: ${eigen_SOURCE_DIR}/Eigen/Core\n"
        "Run tools/setup/populate_deps.sh --refresh or ensure external/cache/eigen-src is populated."
    )
endif()
add_library(eigen INTERFACE)
add_library(Eigen3::Eigen ALIAS eigen)
target_include_directories(eigen INTERFACE "${eigen_SOURCE_DIR}")
target_compile_definitions(eigen INTERFACE EIGEN_MPL2_ONLY)

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
# `imgui_core_lib` is the backend-agnostic Dear ImGui core (no platform/renderer
# backends, no glfw/volk). It is always available — including headless-no-glfw
# builds — so layers that only need the ImGui context + ImDrawData (e.g. the
# runtime-side `Extrinsic.Runtime.ImGuiAdapter`, RUNTIME-090) can link ImGui
# without pulling in the windowing/Vulkan backends.
add_library(imgui_core_lib STATIC
        ${IMGUI_SOURCE_DIR}/imgui.cpp
        ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
        ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
        ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
        ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui_core_lib PUBLIC ${IMGUI_SOURCE_DIR} ${IMGUI_SOURCE_DIR}/backends)
target_compile_definitions(imgui_core_lib PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES GLFW_INCLUDE_NONE)

# `imgui_lib` adds the GLFW + Vulkan backends on top of the core. It builds on
# `imgui_core_lib` (rather than recompiling the core sources) so a final binary
# can link both without duplicate ImGui symbols.
if(NOT INTRINSIC_HEADLESS_NO_GLFW)
    add_library(imgui_lib STATIC
            ${IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    )
    target_include_directories(imgui_lib PUBLIC ${IMGUI_SOURCE_DIR} ${IMGUI_SOURCE_DIR}/backends)
    target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES GLFW_INCLUDE_NONE)
    target_link_libraries(imgui_lib PUBLIC imgui_core_lib glfw volk)

    add_library(imguizmo_lib STATIC
            ${IMGUIZMO_CPP}
    )
    target_include_directories(imguizmo_lib PUBLIC ${IMGUI_SOURCE_DIR} ${IMGUI_SOURCE_DIR}/backends ${IMGUIZMO_INCLUDE_DIR})
    target_compile_definitions(imguizmo_lib PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES GLFW_INCLUDE_NONE)
    target_link_libraries(imguizmo_lib PUBLIC imgui_lib)
endif()
