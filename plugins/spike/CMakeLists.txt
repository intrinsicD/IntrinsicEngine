# ARCH-022 slice 2, S1: a plugin built after the host, loaded with dlopen, in one of two link variants.
add_library(IntrinsicSpikePlugin MODULE SpikePlugin.cpp)
set_target_properties(IntrinsicSpikePlugin PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
target_compile_options(IntrinsicSpikePlugin PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_link_options(IntrinsicSpikePlugin PRIVATE ${INTRINSIC_LINK_FLAGS})
add_dependencies(IntrinsicSpikePlugin ExtrinsicSandbox)

if(BUILD_SHARED_LIBS)
    # Variant (b): the engine libraries are shared, so the plugin links them like any target and CMake
    # carries their module sets; their symbols load once, from the same .so files the Sandbox uses.
    set_target_properties(IntrinsicSpikePlugin PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(IntrinsicSpikePlugin PRIVATE ExtrinsicSandboxEditor)
    return()
endif()

# Variant (a): the engine is static inside the executable, which exports its symbols. CMake 4.3's
# $<COMPILE_ONLY:...> carries include paths and definitions but not module sets, so the host's BMIs are
# passed explicitly, collected from the host's own .modmap files.
set(SPIKE_HOST_MODULES ${CMAKE_BINARY_DIR}/plugins/spike/host-modules.rsp)
add_custom_target(IntrinsicSpikeHostModules
        COMMAND ${CMAKE_COMMAND} -DHOST_ROOT=${CMAKE_BINARY_DIR}/src -DOUT=${SPIKE_HOST_MODULES}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/CollectHostModules.cmake
        BYPRODUCTS ${SPIKE_HOST_MODULES}
        COMMENT "Collecting the host's module BMIs for the spike plugin")
add_dependencies(IntrinsicSpikeHostModules ExtrinsicSandbox)
set_target_properties(IntrinsicSpikePlugin PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_compile_options(IntrinsicSpikePlugin PRIVATE @${SPIKE_HOST_MODULES})
# Usage requirements only; no engine archive is linked, so engine symbols bind to the executable at load.
target_link_libraries(IntrinsicSpikePlugin PRIVATE $<COMPILE_ONLY:ExtrinsicSandboxEditor>)
add_dependencies(IntrinsicSpikePlugin IntrinsicSpikeHostModules)
# With module scanning off, Ninja does not see the imported BMIs; rebuild whenever the host relinks or its
# module list changes, so the plugin never keeps an object compiled against an older host.
set_source_files_properties(SpikePlugin.cpp PROPERTIES OBJECT_DEPENDS
        "${SPIKE_HOST_MODULES};${CMAKE_BINARY_DIR}/bin/ExtrinsicSandbox")
