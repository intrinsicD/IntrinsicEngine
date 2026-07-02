---
id: GEOM-056
theme: F
depends_on: [GEOM-052, RUNTIME-137, GRAPHICS-111]
---
# GEOM-056 — KMeans GPU (Vulkan compute) backend + parity

## Goal
- Implement the real Vulkan-compute GPU backend for `Geometry.KMeans` behind the
  `Extrinsic.Runtime.KMeansBackend` seam that GEOM-052 installed, reproducing the
  CPU reference (`Geometry::KMeans::Cluster`) within a declared parity tolerance,
  reusing persistent GPU buffers across Lloyd iterations to avoid per-iteration
  CPU/GPU I/O, and reporting backend identity plus parity deltas — auto-falling
  back to the CPU reference when no operational device is present.

## Non-goals
- No change to the CPU reference semantics (`Geometry.KMeans` is canonical truth).
- No CUDA path; Vulkan compute only (GRAPHICS-086).
- No interactive backend-toggle UI here; the sandbox K-Means command already
  carries a backend field (GEOM-052 / UI-004). This task wires execution, not new UI.
- No GPU k-means++ seeding in the first operational slice; CPU seeding (one
  upload) matches the reference deterministically.

## Context
- Owning subsystem/layer: the GPU dispatch seam lives in `runtime`
  (`Extrinsic.Runtime.KMeansGpuBackend`), following the
  `docs/architecture/algorithm-variant-dispatch.md` / GEOM-052 pattern and the
  METHOD-013 exemplar. The CPU entry stays geometry-pure.
- Design: `docs/migration/kmeans-gpu-vulkan-compute-proposal.md` (buffer table,
  kernel specs, barrier vocabulary, zero-per-iteration-I/O loop).
- Depends on `RUNTIME-137` (async readback helper — drain results without
  `vkDeviceWaitIdle`) and `GRAPHICS-111` (float segmented reduction — centroid
  accumulate-and-divide). The assignment-compaction/indirect half can reuse
  `Extrinsic.Graphics.ComputeParallelPrimitives` (GRAPHICS-108).
- Parity must follow the IntrinsicEngine reference, including empty-cluster
  reseed to the global farthest point and convergence on label-stability OR
  `maxShift² ≤ tol²` — NOT the Framework24 CUDA behavior.

## Control surfaces
- Config/command: the existing sandbox K-Means backend field (GEOM-052/UI-004);
  `Geometry::KMeans::KMeansParams::Compute`.
- UI: backend status readout only; interactive selection is a later UI task.

## Backends
- Backend axis: `cpu_reference` vs `gpu_vulkan_compute`.
- Result reports `RequestedBackend`, `ActualBackend`, `FellBackToCPU`.

## Slice plan
- **Slice A (this slice).** Add `Extrinsic.Runtime.KMeansGpuBackend` with the
  pure, CPU-testable planning core: buffer-role/layout computation with overflow
  checks, BDA state-record + push-constant structs (`static_assert` sizes),
  per-iteration dispatch-plan computation, buffer/pipeline `Desc` builders, and a
  `ResolveKMeansGpuRequest` that reports GPU availability/telemetry and
  recommends CPU fallback on a non-operational device. Route the
  `Runtime.KMeansBackend` GPU branch through the resolve seam; execution still
  falls back to the CPU reference. Closes `Scaffolded → CPUContracted`.
- **Slice B.** Add `kmeans_reset/assign/update` shader assets and fail-closed
  RHI dispatch recording (`ICommandContext`), with the single-submission
  barrier-chained Lloyd loop; upload once, keep buffers resident.
- **Slice C.** Persistent buffer leasing/caching keyed by `(n,k)`, centroid
  ping-pong, and result drain through `RUNTIME-137` `AsyncBufferReadback` (no
  `vkDeviceWaitIdle`). Centroid accumulation via `GRAPHICS-111`.
- **Slice D.** `gpu;vulkan` parity tests vs the CPU reference (inertia/label
  tolerance; deterministic mode) + benchmark manifest with `gpu_time_ms` and a
  CPU-vs-GPU speedup diagnostic, baseline-compared. Closes `Operational →
  ParityProven`.

## Required changes
- [ ] Slice A: `Extrinsic.Runtime.KMeansGpuBackend` planning module (layout,
      structs, dispatch plan, desc builders, resolve/telemetry) with no RHI
      execution.
- [ ] Slice A: route `Extrinsic.Runtime.KMeansBackend`'s `Backend::GPU` branch
      through `ResolveKMeansGpuRequest`, preserving honest CPU fallback telemetry.
- [x] Slice B: shader assets (`kmeans_reset/assign/update.comp`) + fail-closed
      dispatch recording (`RecordKMeansGpuPasses` + `BuildKMeansGpuStateRecord`).
- [ ] Slice C: persistent buffer set + async readback drain + float reduction.
- [ ] Slice D: parity tests + benchmark.

## Tests
- [ ] Slice A default-gate contract: layout sizes/overflow, dispatch-plan group
      counts, resolve reports DeviceUnavailable/PlanningOnly on a non-operational
      device, and `KMeansBackend` returns the CPU result with honest telemetry.
- [ ] Slice D `gpu;vulkan` parity: GPU reproduces the CPU reference within the
      declared tolerance on shared fixtures; `ActualBackend == GPU` when operational.
- [ ] Default-gate fallback: on the Null device a `Backend::GPU` request returns
      the CPU result with `ActualBackend == CPU`.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update `docs/architecture/algorithm-variant-dispatch.md` current-exemplar
      status once the GPU path is operational; cross-link the proposal.
- [ ] Document the backend, parity tolerance, and fallback in `src/runtime/README.md`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` on module-surface changes.

## Acceptance criteria
- [ ] GPU backend reproduces the CPU reference within the documented parity
      tolerance on the tested datasets, with persistent buffers reused across
      iterations and no per-iteration CPU/GPU I/O.
- [ ] Backend identity and parity deltas reported; clean CPU fallback on a
      non-operational device.
- [ ] Method layering holds (CPU entry geometry-pure; GPU overload at runtime);
      no CUDA; no `Vk*` leakage through RHI.
- [ ] `gpu;vulkan` parity tests pass under `ci-vulkan`; CPU fallback passes the default gate.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'KMeans' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
# Vulkan-capable host (Slice D):
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders --require kmeans_assign.comp.spv
ctest --test-dir build/ci-vulkan --output-on-failure -R 'KMeansGpu' -L 'gpu' --timeout 120
```

## Forbidden changes
- Changing the CPU reference semantics to make the GPU path "match".
- Introducing CUDA or leaking `Vk*` types through RHI.
- Fabricating a GPU path in telemetry; `ActualBackend` must be honest.
- Claiming speedups without a baseline comparison.

## Maturity
- Target: `Operational` → `ParityProven` on Vulkan-capable hosts; `CPUContracted`
  everywhere else. Slice A closes `Scaffolded → CPUContracted`; `Operational`
  owned by `GEOM-056` (its own Slices B–D).
