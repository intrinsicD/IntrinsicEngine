# ARCH-022: the fixture plugin of the plugin host, loaded with dlopen into the running Sandbox. Built only with the
# shared engine libraries: the plugin links them like any target and CMake carries their module sets; their
# symbols load once, from the same .so files the Sandbox uses.
add_library(IntrinsicSpikePlugin MODULE SpikePlugin.cpp)
set_target_properties(IntrinsicSpikePlugin PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_SCAN_FOR_MODULES ON
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
target_compile_options(IntrinsicSpikePlugin PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_link_options(IntrinsicSpikePlugin PRIVATE ${INTRINSIC_LINK_FLAGS})
target_link_libraries(IntrinsicSpikePlugin PRIVATE ExtrinsicRuntime)
# The drivers start the Sandbox they load it into.
add_dependencies(IntrinsicSpikePlugin ExtrinsicSandbox)
# The build's generation lives in the build directory, so the reload driver can make a new build without touching
# repository sources. Written once; the driver rewrites it.
set(SPIKE_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(NOT EXISTS ${SPIKE_GENERATED}/SpikeVersion.inc)
    file(WRITE ${SPIKE_GENERATED}/SpikeVersion.inc "#define SPIKE_GENERATION 1\n#define SPIKE_FAIL_REGISTER 0\n")
endif()
target_include_directories(IntrinsicSpikePlugin PRIVATE ${SPIKE_GENERATED})
