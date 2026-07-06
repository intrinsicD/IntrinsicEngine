# Rendering Benchmarks

Rendering benchmark suites live here, including frame-graph and renderer
workload measurements.

## Smoke Benchmarks

- `rendering.framegraph_barrier_emission.smoke` is the `GRAPHICS-120`
  baseline/probe for CPU-side framegraph barrier packet traversal. It compares
  the legacy full-scan emission shape against the shared indexed range lookup
  on the same deterministic synthetic packet set and records
  `adoption_claim=false` because it is traversal evidence, not renderer-wide
  frame-time evidence.
- `rendering.framegraph_compiler_indexing.smoke` is the `GRAPHICS-120`
  baseline/probe for compiler-side duplicate pass-id scanning and barrier
  packet insertion. It compares the legacy nested/linear scan shape against the
  sorted/indexed shape on deterministic synthetic compiler work and records
  `adoption_claim=false` because it is algorithm evidence, not renderer-wide
  frame-time evidence.
- `rendering.frame_recipe_compile_cache.smoke` is the `GRAPHICS-117`
  baseline/probe for the default frame recipe's CPU declare+compile stage. It
  measures rebuild-each-frame declare+compile time and records the renderer
  cache contract's steady-state compile attempts as `0`; the result keeps
  `adoption_claim=false` because it is compile-stage evidence, not a
  renderer-wide frame-time claim.
- `rendering.vertex_fetch_layout.smoke` is the `RUNTIME-125` benchmark gate for
  the optional AoS static-geometry fast lane. It measures the current uniform
  SoA vertex-fetch shape and an interleaved AoS probe over the same
  deterministic built-in static mesh. The smoke result is a baseline/probe only:
  it records `adoption_claim=false`, and shader/storage variants remain blocked
  until a comparable GPU/profile baseline proves vertex fetch is the bottleneck.
