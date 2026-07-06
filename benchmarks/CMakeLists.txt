cmake_minimum_required(VERSION 3.28)

set(INTRINSIC_BENCHMARK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Intrinsic benchmark root")
include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)

find_program(INTRINSIC_BENCHMARK_LLVM_SYMBOLIZER
    NAMES llvm-symbolizer llvm-symbolizer-18 llvm-symbolizer-19 llvm-symbolizer-20 llvm-symbolizer-21 llvm-symbolizer-22
)

set(_intrinsic_benchmark_test_env
    "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan.supp:fast_unwind_on_malloc=0:detect_leaks=0"
    "ASAN_OPTIONS=symbolize=1:detect_leaks=0:fast_unwind_on_malloc=0"
)

if(INTRINSIC_BENCHMARK_LLVM_SYMBOLIZER)
    list(APPEND _intrinsic_benchmark_test_env "ASAN_SYMBOLIZER_PATH=${INTRINSIC_BENCHMARK_LLVM_SYMBOLIZER}")
endif()

# GEOM-009 — geometry smoke benchmark executable. The runner imports the
# Geometry C++23 modules so the target enables module scanning and links
# promoted geometry/core module libraries.
add_executable(IntrinsicBenchmarkSmoke
    runners/BenchmarkSmokeRunner.cpp
    geometry/Bench_ExampleSmoke.cpp
    geometry/Bench_PointCloudFilteringSmoke.cpp
    geometry/Bench_ProgressivePoissonReferenceSmoke.cpp
    geometry/Bench_QualityMetricsSmoke.cpp
    geometry/Bench_SignedHeatReferenceSmoke.cpp
    geometry/Bench_SurfaceSamplingSmoke.cpp
    geometry/Bench_UvAtlasSmoke.cpp
    physics/Bench_RigidBodyReferenceSmoke.cpp
    physics/Bench_ParticleSpringReferenceSmoke.cpp
    physics/Bench_XpbdClothReferenceSmoke.cpp
    physics/Bench_SphFluidReferenceSmoke.cpp
    rendering/Bench_FramegraphBarrierEmissionSmoke.cpp
    rendering/Bench_FramegraphCompilerIndexingSmoke.cpp
    rendering/Bench_FrameRecipeCompileCacheSmoke.cpp
    rendering/Bench_VertexFetchLayoutSmoke.cpp
    ${CMAKE_SOURCE_DIR}/methods/physics/rigid_body_reference/src/RigidBodyReference.cpp
    ${CMAKE_SOURCE_DIR}/methods/physics/particle_spring_reference/src/ParticleSpringReference.cpp
    ${CMAKE_SOURCE_DIR}/methods/physics/xpbd_cloth_reference/src/XpbdClothReference.cpp
    ${CMAKE_SOURCE_DIR}/methods/physics/sph_fluid_reference/src/SphFluidReference.cpp
)

set_target_properties(IntrinsicBenchmarkSmoke PROPERTIES CXX_SCAN_FOR_MODULES ON)

target_compile_features(IntrinsicBenchmarkSmoke PRIVATE cxx_std_23)
target_compile_options(IntrinsicBenchmarkSmoke PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_link_options(IntrinsicBenchmarkSmoke PRIVATE ${INTRINSIC_LINK_FLAGS})
target_include_directories(IntrinsicBenchmarkSmoke PRIVATE
    ${CMAKE_SOURCE_DIR}/methods/physics/rigid_body_reference/include
    ${CMAKE_SOURCE_DIR}/methods/physics/particle_spring_reference/include
    ${CMAKE_SOURCE_DIR}/methods/physics/xpbd_cloth_reference/include
    ${CMAKE_SOURCE_DIR}/methods/physics/sph_fluid_reference/include
)

target_link_libraries(IntrinsicBenchmarkSmoke
    PRIVATE
        IntrinsicConfig
        ExtrinsicCore
        ExtrinsicGraphics
        IntrinsicGeometry
        IntrinsicProgressivePoissonReference
)

set(INTRINSIC_BENCHMARK_SMOKE_OUTPUT
    "${CMAKE_BINARY_DIR}/benchmark/IntrinsicBenchmarkSmoke/result.json"
    CACHE INTERNAL "Path for benchmark smoke runner JSON output"
)

add_custom_target(IntrinsicBenchmarks
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/benchmark"
    COMMAND $<TARGET_FILE:IntrinsicBenchmarkSmoke> ${INTRINSIC_BENCHMARK_SMOKE_OUTPUT}
    DEPENDS IntrinsicBenchmarkSmoke
    COMMENT "Running benchmark smoke scaffold"
)

if(TARGET ExtrinsicRuntime AND TARGET ExtrinsicBackendsVulkan AND INTRINSIC_PLATFORM_BACKEND_SELECTED STREQUAL "Glfw")
    add_executable(IntrinsicKMeansGpuBenchmarkSmoke
        geometry/Bench_KMeansGpuVulkanSmoke.cpp
    )
    set_target_properties(IntrinsicKMeansGpuBenchmarkSmoke PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_compile_features(IntrinsicKMeansGpuBenchmarkSmoke PRIVATE cxx_std_23)
    target_compile_options(IntrinsicKMeansGpuBenchmarkSmoke PRIVATE ${INTRINSIC_COMPILE_FLAGS})
    target_link_options(IntrinsicKMeansGpuBenchmarkSmoke PRIVATE ${INTRINSIC_LINK_FLAGS})
    target_link_libraries(IntrinsicKMeansGpuBenchmarkSmoke
        PRIVATE
            IntrinsicConfig
            ExtrinsicRuntime
            ExtrinsicGraphics
            ExtrinsicBackendsVulkan
            ExtrinsicPlatform
            ExtrinsicRHI
            ExtrinsicCore
            IntrinsicGeometry
    )
    intrinsic_add_glsl_shaders(IntrinsicKMeansGpuBenchmarkSmoke)
endif()

# Register the smoke runner as a CTest case so the default CPU gate exercises
# the benchmark workloads + emitted result schema, not just placeholders. The
# validator script is invoked as a follow-up fixture so the JSON is
# structurally checked on every run.
if(BUILD_TESTING)
    set_property(GLOBAL APPEND PROPERTY INTRINSIC_REGISTERED_TEST_TARGETS IntrinsicBenchmarkSmoke)

    set(_intrinsic_bench_smoke_test_dir "${CMAKE_BINARY_DIR}/benchmark-ctest/IntrinsicBenchmarkSmokeTest")

    add_test(
        NAME IntrinsicBenchmarkSmoke.Run
        COMMAND $<TARGET_FILE:IntrinsicBenchmarkSmoke> "${_intrinsic_bench_smoke_test_dir}"
    )
    set_tests_properties(IntrinsicBenchmarkSmoke.Run PROPERTIES
        LABELS "benchmark;geometry;graphics;physics"
        TIMEOUT 30
        FIXTURES_SETUP IntrinsicBenchmarkSmoke
    )

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME IntrinsicBenchmarkSmoke.Validate
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/tools/benchmark/validate_benchmark_results.py"
                --root "${_intrinsic_bench_smoke_test_dir}"
                --strict
        )
        set_tests_properties(IntrinsicBenchmarkSmoke.Validate PROPERTIES
            LABELS "benchmark;geometry;graphics;physics"
            TIMEOUT 30
            FIXTURES_REQUIRED IntrinsicBenchmarkSmoke
        )
    endif()

    if(TARGET IntrinsicKMeansGpuBenchmarkSmoke)
        set_property(GLOBAL APPEND PROPERTY INTRINSIC_REGISTERED_TEST_TARGETS IntrinsicKMeansGpuBenchmarkSmoke)

        set(_intrinsic_kmeans_gpu_bench_dir
            "${CMAKE_BINARY_DIR}/benchmark-ctest/IntrinsicKMeansGpuBenchmarkSmoke")

        add_test(
            NAME IntrinsicKMeansGpuBenchmarkSmoke.Run
            COMMAND $<TARGET_FILE:IntrinsicKMeansGpuBenchmarkSmoke>
                "${_intrinsic_kmeans_gpu_bench_dir}"
        )
        set_tests_properties(IntrinsicKMeansGpuBenchmarkSmoke.Run PROPERTIES
            ENVIRONMENT "${_intrinsic_benchmark_test_env}"
            LABELS "benchmark;geometry;runtime;gpu;vulkan"
            TIMEOUT 120
            FIXTURES_SETUP IntrinsicKMeansGpuBenchmarkSmoke
        )

        if(Python3_Interpreter_FOUND)
            add_test(
                NAME IntrinsicKMeansGpuBenchmarkSmoke.Validate
                COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_SOURCE_DIR}/tools/benchmark/validate_benchmark_results.py"
                    --root "${_intrinsic_kmeans_gpu_bench_dir}"
                    --strict
            )
            set_tests_properties(IntrinsicKMeansGpuBenchmarkSmoke.Validate PROPERTIES
                ENVIRONMENT "${_intrinsic_benchmark_test_env}"
                LABELS "benchmark;geometry;runtime;gpu;vulkan"
                TIMEOUT 30
                FIXTURES_REQUIRED IntrinsicKMeansGpuBenchmarkSmoke
            )
        endif()
    endif()
endif()
