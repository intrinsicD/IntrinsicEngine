# ARCH-022 slice 2, S1: a plugin built after the host from the host's module BMIs, loaded with dlopen.
# CMake 4.3's $<COMPILE_ONLY:...> carries include paths and definitions but not module sets, so the
# host's BMIs are passed explicitly, collected from the host's own .modmap files.
set(SPIKE_HOST_MODULES ${CMAKE_BINARY_DIR}/plugins/spike/host-modules.rsp)
add_custom_target(IntrinsicSpikeHostModules
        COMMAND ${CMAKE_COMMAND} -DHOST_ROOT=${CMAKE_BINARY_DIR}/src -DOUT=${SPIKE_HOST_MODULES}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/CollectHostModules.cmake
        BYPRODUCTS ${SPIKE_HOST_MODULES}
        COMMENT "Collecting the host's module BMIs for the spike plugin")
add_dependencies(IntrinsicSpikeHostModules ExtrinsicSandbox)

add_library(IntrinsicSpikePlugin MODULE SpikePlugin.cpp)
set_target_properties(IntrinsicSpikePlugin PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_SCAN_FOR_MODULES OFF
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
target_compile_features(IntrinsicSpikePlugin PRIVATE cxx_std_23)
target_compile_options(IntrinsicSpikePlugin PRIVATE ${INTRINSIC_COMPILE_FLAGS} @${SPIKE_HOST_MODULES})
target_link_options(IntrinsicSpikePlugin PRIVATE ${INTRINSIC_LINK_FLAGS})
# Usage requirements only; no engine archive is linked, so engine symbols bind to the executable at load.
target_link_libraries(IntrinsicSpikePlugin PRIVATE $<COMPILE_ONLY:ExtrinsicSandboxEditor>)
add_dependencies(IntrinsicSpikePlugin IntrinsicSpikeHostModules)
