---
id: GRAPHICS-111
theme: B
depends_on: []
---
# GRAPHICS-111 — Float segmented/per-key reduction primitive in ComputeParallelPrimitives

## Goal
- Add a reusable GPU float segmented reduction (sum, and mean via sum/count) over
  a key→value stream to `Extrinsic.Graphics.ComputeParallelPrimitives`, with a
  deterministic CPU reference, so per-cluster/per-segment accumulation
  (e.g. k-means centroid `Σx / count`) is a shared primitive rather than each
  backend hand-rolling float atomics.

## Non-goals
- No change to the existing prefix-scan / stream-compaction contracts.
- No k-means backend wiring here; current KMeans GPU execution uses a portable
  per-cluster scan until this primitive is available. This task ships the
  primitive + its parity contract only.
- No arbitrary-associative-operator reduction framework; scope is float sum and
  count-normalized mean over a bounded key range (segment count `k`).

## Context
- Owning subsystem/layer: `graphics/renderer`
  (`Extrinsic.Graphics.ComputeParallelPrimitives`); imports RHI contracts only,
  no ECS/runtime/app/Vulkan-native handles (per
  `docs/architecture/compute-parallel-primitives.md`).
- Origin: `docs/reviews/2026-07-01-gpu-geometry-backend-io-audit.md` Finding 3.
  The module is a good substrate for the assignment-compaction and
  count→dispatch-indirect half of a clustering iteration, but has **no float
  segmented/per-key reduction** — exactly the centroid accumulate-and-divide
  step. Keys/values are `uint32`-only today (`parallel_prefix_scan.comp:23`).
- Preferred implementation: workgroup-privatized per-segment accumulators in
  shared memory flushed once to global (bounded `k` fits shared memory), with a
  deterministic fixed-point / two-pass fallback when `k` exceeds the shared-memory
  budget or optional float-atomic features are unavailable. Any float-atomic fast
  path must be capability-gated before pipeline creation because the Sandbox
  KMeans GPU path must not create shaders that require unsupported Vulkan
  features.
- Consumes the same caller-provided-scratch + owned-lease-fallback pattern the
  existing primitives already use, so callers reuse buffers across dispatches.

## Required changes
- [ ] Add a segmented-reduction record API (planning + RHI recording) that takes
      a per-element segment key and float value stream and produces per-segment
      sums and counts, plus a count-normalized mean, into caller-owned buffers.
- [ ] Add the shader asset(s) (BDA/scalar-push convention, `local_size_x=256`,
      shared-memory privatization + deterministic fallback for large `k` or
      missing optional float-atomic support).
- [ ] Add a deterministic CPU reference (`...ReduceBySegmentCpu`) mirroring the
      existing CPU reference helpers, and a declared parity tolerance for the
      float-atomic GPU path.
- [ ] Reuse the caller-provided-scratch + owned-`BufferLease`-fallback pattern;
      no per-call allocation churn when scratch is supplied.

## Tests
- [ ] Default CPU-gate contract test: CPU reference sums/means match a hand
      computed fixture; empty segments report count 0 and a defined mean.
- [ ] Default CPU-gate contract test: fail-closed GPU record status on
      non-operational device / invalid handles (mirroring the existing
      primitives' fail-closed contract).
- [ ] Opt-in `gpu;vulkan` smoke: GPU segmented sum/mean matches the CPU reference
      within the declared tolerance; deterministic mode is bit-stable across runs.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Extend `docs/architecture/compute-parallel-primitives.md` with the
      segmented-reduction contract, scratch layout, and parity tolerance.
- [ ] Cross-link `docs/migration/kmeans-gpu-vulkan-compute-proposal.md` §5 and the
      audit Finding 3.
- [ ] Regenerate `docs/api/generated/module_inventory.md` for the new surface.

## Acceptance criteria
- [ ] A backend can compute per-segment float sums and count-normalized means via
      one shared primitive with reused scratch buffers.
- [ ] GPU path matches the CPU reference within a declared tolerance; a
      deterministic mode is available and bit-stable.
- [ ] Large-`k` / missing-feature fallback path is covered when shared-memory
      privatization or optional float atomics do not fit.
- [ ] No RHI/Vulkan-native leakage; layering holds; default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitives' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
# Vulkan-capable host:
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ComputeParallelPrimitivesGpuSmoke' -L 'gpu' --timeout 120
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Leaking `Vk*`/Vulkan-native handles through the graphics API.
- Claiming parity without a declared tolerance and a CPU reference.

## Maturity
- Target: `Operational` on Vulkan-capable hosts (opt-in `gpu;vulkan` parity
  smoke); `CPUContracted` everywhere else via the CPU reference + fail-closed
  contract tests.
