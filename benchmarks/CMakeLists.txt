cmake_minimum_required(VERSION 3.28)

set(INTRINSIC_BENCHMARK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Intrinsic benchmark root")

# GEOM-009 — geometry smoke benchmark executable. The runner imports the
# Geometry C++23 modules so the target enables module scanning and links
# the IntrinsicGeometry/IntrinsicCore module libraries.
add_executable(IntrinsicBenchmarkSmoke
    runners/BenchmarkSmokeRunner.cpp
    geometry/Bench_ExampleSmoke.cpp
)

set_target_properties(IntrinsicBenchmarkSmoke PROPERTIES CXX_SCAN_FOR_MODULES ON)

target_compile_features(IntrinsicBenchmarkSmoke PRIVATE cxx_std_23)
target_compile_options(IntrinsicBenchmarkSmoke PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_link_options(IntrinsicBenchmarkSmoke PRIVATE ${INTRINSIC_LINK_FLAGS})

target_link_libraries(IntrinsicBenchmarkSmoke
    PRIVATE
        IntrinsicConfig
        IntrinsicCore
        IntrinsicGeometry
)

set(INTRINSIC_BENCHMARK_SMOKE_OUTPUT
    "${CMAKE_BINARY_DIR}/benchmark/IntrinsicBenchmarkSmoke/result.json"
    CACHE INTERNAL "Path for benchmark smoke runner JSON output"
)

add_custom_target(IntrinsicBenchmarks
    COMMAND $<TARGET_FILE:IntrinsicBenchmarkSmoke> ${INTRINSIC_BENCHMARK_SMOKE_OUTPUT}
    DEPENDS IntrinsicBenchmarkSmoke
    COMMENT "Running benchmark smoke scaffold"
)

# GEOM-009 — register the smoke runner as a CTest case so the default CPU
# gate exercises the benchmark workload + emitted result schema, not just the
# placeholder. The validator script is invoked as a follow-up fixture so the
# JSON is structurally checked on every run.
if(BUILD_TESTING)
    set(_intrinsic_bench_smoke_test_dir "${CMAKE_BINARY_DIR}/benchmark/IntrinsicBenchmarkSmokeTest")

    add_test(
        NAME IntrinsicBenchmarkSmoke.HalfedgeSmoke.Run
        COMMAND $<TARGET_FILE:IntrinsicBenchmarkSmoke> "${_intrinsic_bench_smoke_test_dir}"
    )
    set_tests_properties(IntrinsicBenchmarkSmoke.HalfedgeSmoke.Run PROPERTIES
        LABELS "benchmark;geometry"
        TIMEOUT 30
        FIXTURES_SETUP IntrinsicBenchmarkSmoke.HalfedgeSmoke
    )

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME IntrinsicBenchmarkSmoke.HalfedgeSmoke.Validate
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/tools/benchmark/validate_benchmark_results.py"
                --root "${_intrinsic_bench_smoke_test_dir}"
                --strict
        )
        set_tests_properties(IntrinsicBenchmarkSmoke.HalfedgeSmoke.Validate PROPERTIES
            LABELS "benchmark;geometry"
            TIMEOUT 30
            FIXTURES_REQUIRED IntrinsicBenchmarkSmoke.HalfedgeSmoke
        )
    endif()
endif()
