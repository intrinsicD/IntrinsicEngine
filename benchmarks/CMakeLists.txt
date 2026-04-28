cmake_minimum_required(VERSION 3.28)

set(INTRINSIC_BENCHMARK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Intrinsic benchmark root")

add_executable(IntrinsicBenchmarkSmoke
    runners/BenchmarkSmokeRunner.cpp
    geometry/Bench_ExampleSmoke.cpp
)

target_compile_features(IntrinsicBenchmarkSmoke PRIVATE cxx_std_23)

target_link_libraries(IntrinsicBenchmarkSmoke PRIVATE IntrinsicConfig)

set(INTRINSIC_BENCHMARK_SMOKE_OUTPUT
    "${CMAKE_BINARY_DIR}/benchmark/IntrinsicBenchmarkSmoke/result.json"
    CACHE INTERNAL "Path for benchmark smoke runner JSON output"
)

add_custom_target(IntrinsicBenchmarks
    COMMAND $<TARGET_FILE:IntrinsicBenchmarkSmoke> ${INTRINSIC_BENCHMARK_SMOKE_OUTPUT}
    DEPENDS IntrinsicBenchmarkSmoke
    COMMENT "Running benchmark smoke scaffold"
)
