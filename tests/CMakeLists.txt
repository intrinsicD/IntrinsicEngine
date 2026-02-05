set(target_name IntrinsicTests)

enable_testing()

add_executable(${target_name}
        Test_BoundedHeap.cpp
        Test_CoreMemory.cpp
        Test_CoreTasks.cpp
        Test_CoreAssets.cpp
        Test_CoreHandle.cpp
        Test_CoreHash.cpp
        Test_RuntimeGraphics.cpp
        Test_RuntimeRHI.cpp
        Test_RuntimeECS.cpp
        Test_RuntimeGeometry.cpp
        Test_RuntimeGeometry_NoHeapEPA.cpp
        Test_RuntimeGraph.cpp
        Test_RuntimeGraph_ParallelLayers.cpp
        Test_RuntimeSelection.cpp
        Test_RuntimeSelection_Multi.cpp
        Test_TextureMaterialHotReload.cpp
        Test_RuntimeCulling.cpp
)

#Apply Strict Flags ONLY to our tests
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})
set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)

# Link against the Core Engine and GTest
target_link_libraries(${target_name}
        PRIVATE
        IntrinsicConfig
        IntrinsicRuntime
        GTest::gtest_main
        dl
)

# Helper to run tests via CTest
include(GoogleTest)

# Try to locate a symbolizer so sanitizer reports have function/file/line.
find_program(INTRINSIC_LLVM_SYMBOLIZER
        NAMES llvm-symbolizer llvm-symbolizer-18 llvm-symbolizer-19 llvm-symbolizer-20 llvm-symbolizer-21 llvm-symbolizer-22
)

set(_intrinsic_test_env
        "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan.supp:fast_unwind_on_malloc=0"
        "ASAN_OPTIONS=symbolize=1:detect_leaks=1:fast_unwind_on_malloc=0"
)

if(INTRINSIC_LLVM_SYMBOLIZER)
    list(APPEND _intrinsic_test_env "ASAN_SYMBOLIZER_PATH=${INTRINSIC_LLVM_SYMBOLIZER}")
endif()

gtest_discover_tests(${target_name}
    PROPERTIES
    ENVIRONMENT "${_intrinsic_test_env}"
    DISCOVERY_MODE POST_BUILD
)
