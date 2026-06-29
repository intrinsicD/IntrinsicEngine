# Rendering Benchmarks

Rendering benchmark suites live here, including frame-graph and renderer
workload measurements.

## Smoke Benchmarks

- `rendering.vertex_fetch_layout.smoke` is the `RUNTIME-125` benchmark gate for
  the optional AoS static-geometry fast lane. It measures the current uniform
  SoA vertex-fetch shape and an interleaved AoS probe over the same
  deterministic built-in static mesh. The smoke result is a baseline/probe only:
  it records `adoption_claim=false`, and shader/storage variants remain blocked
  until a comparable GPU/profile baseline proves vertex fetch is the bottleneck.
