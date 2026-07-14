---
id: GEOM-052
theme: F
depends_on: [DOCS-003]
maturity_target: CPUContracted
completed_on: 2026-06-28
---
# GEOM-052 — Shared CPU/GPU backend seam + fix the KMeans phantom GPU exemplar

## Goal
- [x] Establish the reusable Strategy×Backend seam so a new data-parallel algorithm
  gets CPU + GPU *hooks* for free, and fix `Geometry.KMeans` to that seam as the
  worked exemplar — reporting `ActualBackend` honestly, with no fabricated GPU
  path (P4; honors AGENTS.md §6 CPU-reference-first).

## Non-goals
- Implementing a real GPU/Vulkan-compute backend for KMeans (a later
  parity-gated slice; the seam + honest telemetry is the deliverable now).
- A global backend-preference map across families (premature with one family).
- Converting other geometry algorithms (each is its own later task).

## Context
- Before this task, the only extant `Backend` axis in `src/geometry` was
  `Geometry.KMeans`, and it
  is a phantom: `enum class Backend { CPU, CUDA }`, `Params.Compute` defaults to
  `Backend::CPU`, and `Geometry.KMeans.cpp` always sets
  `result.ActualBackend = Backend::CPU`. The UI hardcodes `Backend::CPU`. There
  is no reusable seam giving new algorithms CPU+GPU hooks.
- `DOCS-003` defines the canonical seam shape (CPU-only free function +
  `RHI::IDevice&` GPU-capable overload + `ActualBackend` diagnostic, enum tokens
  per `docs/methods/backend-policy.md`). This task implements against it.
- Owner/layer: `geometry` (CPU reference + types; the GPU-capable overload lives
  where RHI is available, per the dispatch doc's link-boundary design).
- Slice per `docs/architecture/backend_integration_slicing_policy.md` so seam,
  exemplar, and UI/config wiring are separately bisectable.
- Status: completed 2026-06-28 by Codex. Commit: this commit (`Implement KMeans backend seam telemetry`).
- `Geometry.KMeans::Backend` now exposes `{CPU, GPU}`; `KMeansResult` reports
  `RequestedBackend`, `ActualBackend`, and `FellBackToCPU`; and the CPU entry
  point reports CPU fallback honestly when `params.Compute == Backend::GPU`.
- `Extrinsic.Runtime.KMeansBackend` is the RHI-visible integration seam. Its
  `ClusterKMeans(...)` overloads accept `Extrinsic::RHI::IDevice&`, evaluate
  `IDevice::IsOperational()` for GPU requests, and fall back to the geometry CPU
  reference because no real KMeans GPU kernel exists in this slice.

## Required changes
- [x] Introduce the shared seam shape (CPU-only free function + optional
      GPU-capable `RHI::IDevice&` overload + `ActualBackend` diagnostic; enum
      `{ CPU, GPU }` using backend-policy tokens, not `CUDA`).
- [x] Rework `Geometry.KMeans` to that shape: resolve to the CPU reference today
      and report `ActualBackend` honestly (no fabricated GPU path).
- [x] Surface a typed `Backend` field on the command/config struct, defaulting to
      the operational reference backend, failing closed to the reference when a
      requested backend is non-operational (`IsOperational` policy).

## Tests
- [x] CPU test: KMeans exposes the seam and reports `ActualBackend`
      deterministically; requesting GPU on a non-operational host resolves to
      CPU with honest telemetry (no fabricated GPU claim).
- [x] CPU test: the seam's requested-vs-resolved backend telemetry is asserted.
- [x] Default CPU gate stays green.

## Docs
- [x] Cross-link the implemented seam from
      `docs/architecture/algorithm-variant-dispatch.md` (reconciled by `DOCS-003`).
- [x] Note KMeans as the canonical backend-seam exemplar in
      `docs/architecture/geometry.md` (there is no `src/geometry/README.md` in
      this tree) and document the runtime adapter in `src/runtime/README.md`.

## Acceptance criteria
- [x] A new data-parallel algorithm can adopt the seam to get CPU+GPU hooks.
- [x] `Geometry.KMeans` reports `ActualBackend` honestly; no fabricated GPU path.
- [x] Backend selection is read from the command/config struct and fails closed
      to the reference backend.
- [x] No GPU dependency in the CPU gate; gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Fabricating a GPU/CUDA path to satisfy the principle (AGENTS.md §6).
- Building a global backend-preference map.
- Converting other geometry algorithms in this task.

## Maturity
- This task closes `Scaffolded → CPUContracted` (seam + CPU reference + honest
  `ActualBackend` telemetry). A real GPU/Vulkan-compute backend is a separate
  later parity-gated task; no `Operational` follow-up is owed by this task.
