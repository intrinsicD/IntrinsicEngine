cmake_minimum_required(VERSION 3.28)

# Benchmark tree scaffolding only (RORG-050).
# Concrete benchmark targets are added by follow-up tasks.

set(INTRINSIC_BENCHMARK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Intrinsic benchmark root")

add_custom_target(IntrinsicBenchmarks
    COMMENT "Benchmark scaffold target (no benchmark binaries defined yet)"
)
