---
id: RUNTIME-125
theme: B
depends_on: [RUNTIME-122]
maturity_target: CPUContracted
completed_on: 2026-07-02
---
# RUNTIME-125 — Optional AoS fast lane for static geometry

## Goal
- Retire the profile-gated planning and CPU contract slices for the optional
  static-geometry AoS fast lane without changing the default ADR-0022 SoA storage
  model.

## Non-goals
- No default storage-model change; uniform SoA with per-channel streaming
  remains the default.
- No AoS GPU buffer allocation or shader variant adoption in this retired slice.
- No performance claim beyond the recorded local smoke measurement.

## Context
- Status: completed 2026-07-02 at `CPUContracted`.
- Owning subsystem/layer: `graphics/renderer` for `GpuWorld` storage-lane
  planning and future shader variants; `runtime` extraction for the future
  static-to-dynamic promotion trigger.
- Slice A added a PR-fast benchmark/probe comparing current uniform SoA vertex
  fetch against an interleaved AoS probe over a deterministic static scene.
- Slice B added the data-only storage-lane classification and static-to-dynamic
  promotion planning contract. It intentionally did not allocate AoS GPU buffers
  or select shader variants.
- `RUNTIME-139` owns the operational AoS storage/shader variants, promotion
  implementation, and opt-in `gpu;vulkan` parity smoke.

## Required changes
- [x] Add a PR-fast baseline/probe benchmark for current uniform-SoA vertex
      fetch plus an interleaved AoS probe over the same deterministic static
      scene, without enabling the AoS lane or making an adoption claim.
- [x] Add planning-only graphics contract coverage for storage-lane
      classification and first-streaming-edit promotion from optional static AoS
      to default SoA.
- [x] Document that the planning slice allocates no AoS GPU buffers, mutates no
      geometry records, and selects no shader variants.
- [x] Move AoS storage, shader variants, promotion implementation, and
      `gpu;vulkan` parity proof to `RUNTIME-139`.

## Tests
- [x] Benchmark smoke emits SoA/probe vertex-fetch metrics and validates result
      JSON without claiming adoption.
- [x] CPU contract test covers storage-lane classification and conversion-plan
      behavior.
- [x] Leave operational lane parity smoke to `RUNTIME-139`.

## Docs
- [x] Document the rendering vertex-fetch layout smoke benchmark under
      `benchmarks/rendering/README.md`.
- [x] Document the planning-only `GpuWorld` storage/promotion contract without
      marking the AoS lane adopted.
- [x] Keep ADR-0022's default SoA policy unchanged; `RUNTIME-139` must update it
      only if the operational fast lane is adopted.

## Acceptance criteria
- [x] Benchmark infrastructure exists and records comparable SoA/probe metrics
      before any shader-variant work lands.
- [x] CPU/default-gate contracts prove the storage-class and promotion-planning
      decisions without allocating a second GPU lane.
- [x] No AoS adoption claim is made in this task.
- [x] `Operational` AoS allocation/shader/promotion behavior is owned by
      `RUNTIME-139`.

## Verification
```bash
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
build/ci/bin/IntrinsicBenchmarkSmoke /tmp/intrinsic-rendering-benchmarks
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/intrinsic-rendering-benchmarks --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'GpuWorld|GeometryPacker' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

Completed 2026-07-02 at `CPUContracted`.
Commit reference: this commit.

Recorded Slice A smoke evidence on 2026-06-29 (`cpu_reference`, local-dev):
uniform SoA `runtime_ms=13.358873`, interleaved probe
`runtime_ms=10.116964`, `interleaved_to_soa_runtime_ratio=0.757322`,
`quality_error_l2=0.0`, `adoption_claim=false`.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated renderer/runtime feature work.
- Landing shader-variant work without the benchmark gate and `gpu;vulkan`
  parity evidence.

## Maturity
- Target reached: `CPUContracted`.
- `Operational` static AoS storage/shader variants and promote-on-edit behavior
  are owned by `RUNTIME-139`.
