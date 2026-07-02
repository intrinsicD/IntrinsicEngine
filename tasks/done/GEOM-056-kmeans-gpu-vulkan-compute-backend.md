---
id: GEOM-056
theme: F
depends_on: [GEOM-052]
maturity_target: ParityProven
completed_on: 2026-07-02
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
- GEOM-056 uses the runtime `AsyncBufferReadback` helper to drain results without
  `vkDeviceWaitIdle` and uses a shader-local shared-memory privatized centroid
  accumulation path for `k <= 256`, with a direct global-atomic fallback. The
  reusable `GRAPHICS-111` float segmented-reduction primitive remains a
  shared-graphics follow-up rather than a dependency for this local KMeans path.
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
- **Slice A.** Add `Extrinsic.Runtime.KMeansGpuBackend` with the
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
  `vkDeviceWaitIdle`). Centroid accumulation is shader-local shared-memory
  privatized for bounded `k`, with a global-atomic fallback.
- **Slice D.** `gpu;vulkan` parity tests vs the CPU reference (inertia/label
  tolerance; deterministic mode) + benchmark manifest with `gpu_time_ms` and a
  CPU-vs-GPU speedup diagnostic, baseline-compared. Closes `Operational →
  ParityProven`.

## Required changes
- [x] Slice A: `Extrinsic.Runtime.KMeansGpuBackend` planning module (layout,
      structs, dispatch plan, desc builders, resolve/telemetry) with no RHI
      execution.
- [x] Slice A: route `Extrinsic.Runtime.KMeansBackend`'s `Backend::GPU` branch
      through `ResolveKMeansGpuRequest`, preserving honest CPU fallback telemetry.
- [x] Slice B: shader assets (`kmeans_reset/assign/update.comp`) + fail-closed
      dispatch recording (`RecordKMeansGpuPasses` + `BuildKMeansGpuStateRecord`).
- [x] Slice C: persistent buffer cache + one-time SoA/seed upload +
      `RecordKMeansGpuPasses` execution wrapper + async readback drain +
      shader-local privatized centroid accumulation.
- [x] Slice D: parity tests + benchmark.

## Tests
- [x] Slice A/C default-gate contract: layout sizes/overflow, dispatch-plan group
      counts, resolve reports DeviceUnavailable on a non-operational device,
      explicit execution allocates/reuses buffers and publishes post-submit
      readback resources,
      and `KMeansBackend` returns the CPU result with honest telemetry.
- [x] Slice D `gpu;vulkan` parity: GPU reproduces the CPU reference within the
      declared tolerance on shared fixtures; `ActualBackend == GPU` when operational.
- [x] Default-gate fallback: on the Null device a `Backend::GPU` request returns
      the CPU result with `ActualBackend == CPU`.
- [x] Focused default-gate KMeans/runtime fallback coverage passes; the full
      default CPU gate status is recorded below.

## Docs
- [x] Update `docs/architecture/algorithm-variant-dispatch.md` current-exemplar
      status for the explicit GPU execution surface; keep parity proof as Slice D.
- [x] Document the backend execution surface, async drain, and thin-overload
      fallback in `src/runtime/README.md`.
- [x] Regenerate `docs/api/generated/module_inventory.md` on module-surface changes.

## Acceptance criteria
- [x] GPU backend reproduces the CPU reference within the documented parity
      tolerance on the tested datasets, with persistent buffers reused across
      iterations and no per-iteration CPU/GPU I/O.
- [x] Backend identity and parity deltas reported; clean CPU fallback on a
      non-operational device.
- [x] Method layering holds (CPU entry geometry-pure; GPU overload at runtime);
      no CUDA; no `Vk*` leakage through RHI.
- [x] `gpu;vulkan` parity tests pass under `ci-vulkan`; CPU fallback passes the default gate.

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
ctest --test-dir build/ci-vulkan --output-on-failure -R 'IntrinsicKMeansGpuBenchmarkSmoke' -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
```

Completed 2026-07-02. Commit reference: pending local commit.

2026-07-02 verification evidence:
- `cmake --preset ci` passed with Clang 23 and vcpkg manifest dependencies.
- `cmake --build --preset ci --target IntrinsicTests -- -j$(nproc)` passed.
- `ctest --test-dir build/ci --output-on-failure -R 'KMeans|AsyncBufferReadback' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed: 35/35.
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` completed with one unrelated pre-existing runtime registration failure: `SandboxEditorUi.RegistrationCommandAlignsAcrossEntityTransforms` (`FinalRMSE` 0.309120624 vs `< 1.0e-3`); all KMeans/readback coverage passed.
- `cmake --preset ci-vulkan` passed.
- `cmake --build --preset ci-vulkan --target IntrinsicTests -- -j$(nproc)` passed.
- `python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders --require kmeans_reset.comp.spv --require kmeans_assign.comp.spv --require kmeans_update.comp.spv` passed.
- `ctest --test-dir build/ci-vulkan --output-on-failure -R 'KMeansGpuBackendGpuSmoke|IntrinsicKMeansGpuBenchmarkSmoke' -L 'gpu' -L 'vulkan' --timeout 120` passed: 3/3.
- `ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120` passed: 249/249, with one skipped async-histogram smoke.
- `python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict` passed.
- `python3 tools/repo/check_layering.py --root src --strict`, `python3 tools/repo/check_test_layout.py --root . --strict`, and `python3 tools/docs/check_doc_links.py --root .` passed.

## Forbidden changes
- Changing the CPU reference semantics to make the GPU path "match".
- Introducing CUDA or leaking `Vk*` types through RHI.
- Fabricating a GPU path in telemetry; `ActualBackend` must be honest.
- Claiming speedups without a baseline comparison.

## Maturity
- Retired at `ParityProven` on this Vulkan-capable host. CPU-only hosts retain
  the default-gate fallback contract, while the opt-in `ci-vulkan` parity smoke
  and benchmark provide the GPU proof.
