cmake_minimum_required(VERSION 3.28)

set(INTRINSIC_BENCHMARK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Intrinsic benchmark root")
include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

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

# Monolithic CPU smoke benchmark executable. The runner imports promoted
# module libraries across its declared ownership labels, so module scanning
# remains enabled for every workload source.
add_executable(IntrinsicBenchmarkSmoke
    runners/BenchmarkSmokeRunner.cpp
    core/Bench_SchedulerHardeningSmoke.cpp
    core/Bench_TaskGraphPlanReuseSmoke.cpp
    geometry/Bench_BoundaryFirstFlatteningReferenceSmoke.cpp
    geometry/Bench_ContinuousLopReferenceSmoke.cpp
    geometry/Bench_EdgeAwareResamplingReferenceSmoke.cpp
    geometry/Bench_ExampleSmoke.cpp
    geometry/Bench_LopFamilyComparisonSmoke.cpp
    geometry/Bench_PointCloudConsolidationReferenceSmoke.cpp
    geometry/Bench_PointCloudFilteringSmoke.cpp
    geometry/Bench_ProgressivePoissonReferenceSmoke.cpp
    geometry/Bench_QualityMetricsSmoke.cpp
    geometry/Bench_SignedHeatReferenceSmoke.cpp
    geometry/Bench_SimplificationQualitySmoke.cpp
    geometry/Bench_SurfaceSamplingSmoke.cpp
    geometry/Bench_UvAtlasSmoke.cpp
    physics/Bench_RigidBodyReferenceSmoke.cpp
    physics/Bench_ParticleSpringReferenceSmoke.cpp
    physics/Bench_XpbdClothReferenceSmoke.cpp
    physics/Bench_SphFluidReferenceSmoke.cpp
    rendering/Bench_FramegraphBarrierEmissionSmoke.cpp
    rendering/Bench_FramegraphCompilerIndexingSmoke.cpp
    rendering/Bench_FramegraphScratchReuseSmoke.cpp
    rendering/Bench_FrameRecipeCompileCacheSmoke.cpp
    rendering/Bench_RenderGraphParallelRecordingSmoke.cpp
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
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tools/benchmark/run_and_seal.py"
        --executable $<TARGET_FILE:IntrinsicBenchmarkSmoke>
        --output ${INTRINSIC_BENCHMARK_SMOKE_OUTPUT}
        --manifests-root "${CMAKE_SOURCE_DIR}/benchmarks"
        --run-id "cmake-smoke"
        --attempt-id "attempt-001"
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

    add_executable(IntrinsicLopFamilyGpuBenchmarkSmoke
        geometry/Bench_LopFamilyGpuVulkanSmoke.cpp
    )
    set_target_properties(IntrinsicLopFamilyGpuBenchmarkSmoke PROPERTIES CXX_SCAN_FOR_MODULES ON)
    target_compile_features(IntrinsicLopFamilyGpuBenchmarkSmoke PRIVATE cxx_std_23)
    target_compile_options(IntrinsicLopFamilyGpuBenchmarkSmoke PRIVATE ${INTRINSIC_COMPILE_FLAGS})
    target_link_options(IntrinsicLopFamilyGpuBenchmarkSmoke PRIVATE ${INTRINSIC_LINK_FLAGS})
    target_link_libraries(IntrinsicLopFamilyGpuBenchmarkSmoke
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
    intrinsic_add_glsl_shaders(IntrinsicLopFamilyGpuBenchmarkSmoke)
endif()

# Keep the monolithic smoke runner available as an opt-in CTest fixture. The
# optimized Release workflow owns execution and strict JSON validation; hosted
# evidence classifies the complete pair outside the default 30-second fast-test
# budget (BUG-088).
if(BUILD_TESTING)
    intrinsic_register_test_executable(
        TARGET IntrinsicBenchmarkSmoke
        LABELS benchmark core geometry graphics physics slow
    )

    set(_intrinsic_bench_smoke_test_dir "${CMAKE_BINARY_DIR}/benchmark-ctest/IntrinsicBenchmarkSmokeTest")

    add_test(
        NAME IntrinsicBenchmarkSmoke.Run
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/benchmark/run_and_seal.py"
            --executable $<TARGET_FILE:IntrinsicBenchmarkSmoke>
            --output "${_intrinsic_bench_smoke_test_dir}"
            --manifests-root "${CMAKE_SOURCE_DIR}/benchmarks"
            --run-id "ctest-smoke"
            --attempt-id "attempt-001"
    )
    set_tests_properties(IntrinsicBenchmarkSmoke.Run PROPERTIES
        LABELS "benchmark;core;geometry;graphics;physics;slow"
        PROCESSORS 9
        TIMEOUT 120
        FIXTURES_SETUP IntrinsicBenchmarkSmoke
    )

    if(Python3_Interpreter_FOUND)
        add_test(
            NAME IntrinsicBenchmarkSmoke.Validate
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/tools/benchmark/validate_benchmark_results.py"
                --root "${_intrinsic_bench_smoke_test_dir}"
                --strict
        )
        set_tests_properties(IntrinsicBenchmarkSmoke.Validate PROPERTIES
            LABELS "benchmark;core;geometry;graphics;physics;slow"
            TIMEOUT 30
            FIXTURES_REQUIRED IntrinsicBenchmarkSmoke
        )
    endif()

    if(TARGET IntrinsicKMeansGpuBenchmarkSmoke)
        intrinsic_register_test_executable(
            TARGET IntrinsicKMeansGpuBenchmarkSmoke
            LABELS benchmark geometry runtime gpu vulkan
        )

        set(_intrinsic_kmeans_gpu_bench_dir
            "${CMAKE_BINARY_DIR}/benchmark-ctest/IntrinsicKMeansGpuBenchmarkSmoke")

        add_test(
            NAME IntrinsicKMeansGpuBenchmarkSmoke.Run
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/tools/benchmark/run_and_seal.py"
                --executable $<TARGET_FILE:IntrinsicKMeansGpuBenchmarkSmoke>
                --output "${_intrinsic_kmeans_gpu_bench_dir}"
                --manifests-root "${CMAKE_SOURCE_DIR}/benchmarks"
                --run-id "ctest-kmeans-gpu-smoke"
                --attempt-id "attempt-001"
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

    if(TARGET IntrinsicLopFamilyGpuBenchmarkSmoke)
        intrinsic_register_test_executable(
            TARGET IntrinsicLopFamilyGpuBenchmarkSmoke
            LABELS benchmark geometry runtime gpu vulkan
        )

        set(_intrinsic_lop_gpu_bench_dir
            "${CMAKE_BINARY_DIR}/benchmark-ctest/IntrinsicLopFamilyGpuBenchmarkSmoke")

        add_test(
            NAME IntrinsicLopFamilyGpuBenchmarkSmoke.Run
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/tools/benchmark/run_and_seal.py"
                --executable $<TARGET_FILE:IntrinsicLopFamilyGpuBenchmarkSmoke>
                --output "${_intrinsic_lop_gpu_bench_dir}"
                --manifests-root "${CMAKE_SOURCE_DIR}/benchmarks"
                --run-id "ctest-lop-gpu-smoke"
                --attempt-id "attempt-001"
        )
        set_tests_properties(IntrinsicLopFamilyGpuBenchmarkSmoke.Run PROPERTIES
            ENVIRONMENT "${_intrinsic_benchmark_test_env}"
            LABELS "benchmark;geometry;runtime;gpu;vulkan"
            TIMEOUT 180
            FIXTURES_SETUP IntrinsicLopFamilyGpuBenchmarkSmoke
        )

        if(Python3_Interpreter_FOUND)
            add_test(
                NAME IntrinsicLopFamilyGpuBenchmarkSmoke.UnavailableEnvironmentFailsClosed
                COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_SOURCE_DIR}/tests/regression/tooling/Test.LopFamilyGpuBenchmarkUnavailable.py"
                    --executable $<TARGET_FILE:IntrinsicLopFamilyGpuBenchmarkSmoke>
                    --output "${CMAKE_BINARY_DIR}/benchmark-ctest/IntrinsicLopFamilyGpuBenchmarkSmokeUnavailable/raw.json"
            )
            set_tests_properties(
                IntrinsicLopFamilyGpuBenchmarkSmoke.UnavailableEnvironmentFailsClosed
                PROPERTIES
                    LABELS "benchmark;geometry;runtime;gpu;vulkan"
                    TIMEOUT 60
            )

            add_test(
                NAME IntrinsicLopFamilyGpuBenchmarkSmoke.Validate
                COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_SOURCE_DIR}/tools/benchmark/validate_benchmark_results.py"
                    --root "${_intrinsic_lop_gpu_bench_dir}"
                    --strict
            )
            set_tests_properties(IntrinsicLopFamilyGpuBenchmarkSmoke.Validate PROPERTIES
                ENVIRONMENT "${_intrinsic_benchmark_test_env}"
                LABELS "benchmark;geometry;runtime;gpu;vulkan"
                TIMEOUT 30
                FIXTURES_REQUIRED IntrinsicLopFamilyGpuBenchmarkSmoke
            )
        endif()
    endif()
endif()
