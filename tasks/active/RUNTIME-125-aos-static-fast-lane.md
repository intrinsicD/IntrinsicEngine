---
id: RUNTIME-125
theme: B
depends_on: [RUNTIME-122]
maturity_target: Operational
---
# RUNTIME-125 — Optional AoS fast lane for static geometry (profile-gated)

## Goal
- Add an opt-in interleaved-AoS storage lane for proven-static, vertex-fetch-bound
  geometry, with promote-on-edit conversion to the default SoA lane, only if a
  benchmark demonstrates a vertex-fetch bottleneck under uniform SoA.

## Non-goals
- No change to the default storage model: per ADR-0022 the default is uniform
  SoA with per-channel streaming. This task is an optimization, not a re-layout.
- No work begins without a baseline benchmark showing SoA vertex fetch is the
  bottleneck for a representative static-geometry scene.

## Context
- Status: in-progress; owner/agent: Codex; branch: `main` local iteration.
- Current slice: Slice B planning contract only. This slice consumes the
  PR-fast CPU/reference benchmark signal to add data-only storage-lane
  classification and static-to-dynamic promotion planning, but still records no
  AoS adoption claim and does not add GPU storage or shader variants.
- Local Slice A smoke run (2026-06-29, `cpu_reference`, local-dev): uniform SoA
  `runtime_ms=13.358873`, interleaved probe `runtime_ms=10.116964`,
  `interleaved_to_soa_runtime_ratio=0.757322`, `quality_error_l2=0.0`,
  `adoption_claim=false`.
- Next verification step: build/run `IntrinsicBenchmarkSmoke`, validate
  benchmark manifests/results, then run task/docs structural checks.
- Owning subsystem/layer: `src/graphics/renderer` (`GpuWorld`, geometry record)
  and the GpuScene vertex shaders; `src/runtime` extraction for the
  static→dynamic promotion trigger.
- ADR-0022 deferred the AoS fast lane (storage-strategy options A/B) behind a
  profile gate: uniform SoA is the foundation; AoS is added only where measured.
- An AoS lane reintroduces a second vertex layout, so it needs a second shader
  fetch path (pipeline variants) — this cost is only justified by a benchmark.
- Promote-on-edit: when a static-lane geometry first receives a
  `DirtyVertexAttributes` change that wants streaming, convert it to the SoA lane
  (allocate SoA, de-interleave, free AoS, rebind) so streaming stays uniform.

## Required changes
- [x] Add a PR-fast baseline/probe benchmark for current uniform-SoA vertex fetch
      and an interleaved AoS probe over the same deterministic static scene,
      without enabling the AoS lane or making a performance claim.
- [x] Add a planning-only graphics contract for storage-lane classification and
      first-streaming-edit promotion from the optional static AoS lane to the
      default SoA lane, without allocating AoS GPU buffers or selecting shader
      variants.
- [ ] Add a per-geometry storage-class hint (Static=AoS / Dynamic=SoA) and an AoS
      managed sub-allocation path in `GpuWorld`.
- [ ] Add the AoS pipeline variants for the affected passes and a geometry-record
      flag the shader selects on.
- [ ] Implement static→dynamic promotion (AoS→SoA conversion + rebind) on first
      streaming edit.

## Tests
- [x] Benchmark (baseline first): SoA vs AoS vertex-fetch for a representative
      static scene; record metrics per the benchmark protocol. Slice A records
      a CPU/reference smoke baseline only; it does not justify AoS adoption.
- [x] CPU contract test for the promotion path (classification + conversion plan).
- [ ] Opt-in `gpu;vulkan` smoke proving both lanes render identically.

## Docs
- [x] Document the rendering vertex-fetch layout smoke benchmark under
      `benchmarks/rendering/README.md`.
- [x] Document the planning-only `GpuWorld` storage/promotion contract without
      marking the AoS lane adopted.
- [ ] Update `src/graphics/renderer/README.md` and ADR-0022 (mark the fast lane
      realized) if adopted.

## Acceptance criteria
- [ ] A benchmark justifies the AoS lane before any shader-variant work lands.
- [ ] Static geometry renders via AoS; first streaming edit promotes to SoA with
      no visual change; the `gpu;vulkan` smoke is cited.

## Verification
```bash
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
build/ci/bin/IntrinsicBenchmarkSmoke /tmp/intrinsic-rendering-benchmarks
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/intrinsic-rendering-benchmarks --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'GpuWorld|GeometryPacker' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
# Operational: cite a ci-vulkan gpu;vulkan smoke run and the benchmark baseline.
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Landing shader-variant work without the gating benchmark (ADR-0022).

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` elsewhere.
- Slice A (done) closes the benchmark gate at `Scaffolded`: the harness
  exists and emits comparable SoA/probe metrics, but no storage or shader work
  is justified yet.
- Slice B (this slice) closes `Scaffolded -> CPUContracted` for the
  promotion-plan contract only; `Operational` is owned by later `RUNTIME-125`
  slices via the cited `gpu;vulkan` smoke and benchmark baseline.

## Slice plan
- **Slice A (done).** Add the PR-fast benchmark manifest/runner wiring for
  current uniform-SoA vertex fetch plus an interleaved AoS probe. Preserve the
  default renderer storage model and make no performance/adoption claim.
- **Slice B (this slice).** Use the profile signal to add the CPU contract for
  storage-class classification and static-to-dynamic promotion planning. This
  slice is planning-only: it must not allocate AoS GPU buffers, mutate geometry
  records, or add shader variants.
- **Slice C.** Add AoS storage/shader variants and `gpu;vulkan` parity smoke.
