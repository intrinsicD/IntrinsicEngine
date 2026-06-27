---
id: GEOM-052
theme: F
depends_on: [DOCS-003]
maturity_target: CPUContracted
---
# GEOM-052 — Shared CPU/GPU backend seam + fix the KMeans phantom GPU exemplar

## Goal
- Establish the reusable Strategy×Backend seam so a new data-parallel algorithm
  gets CPU + GPU *hooks* for free, and fix `Geometry.KMeans` to that seam as the
  worked exemplar — reporting `ActualBackend` honestly, with no fabricated GPU
  path (P4; honors AGENTS.md §6 CPU-reference-first).

## Non-goals
- Implementing a real GPU/Vulkan-compute backend for KMeans (a later
  parity-gated slice; the seam + honest telemetry is the deliverable now).
- A global backend-preference map across families (premature with one family).
- Converting other geometry algorithms (each is its own later task).

## Context
- The only extant `Backend` axis in `src/geometry` is `Geometry.KMeans`, and it
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

## Required changes
- [ ] Introduce the shared seam shape (CPU-only free function + optional
      GPU-capable `RHI::IDevice&` overload + `ActualBackend` diagnostic; enum
      `{ CPU, GPU }` using backend-policy tokens, not `CUDA`).
- [ ] Rework `Geometry.KMeans` to that shape: resolve to the CPU reference today
      and report `ActualBackend` honestly (no fabricated GPU path).
- [ ] Surface a typed `Backend` field on the command/config struct, defaulting to
      the operational reference backend, failing closed to the reference when a
      requested backend is non-operational (`IsOperational` policy).

## Tests
- [ ] CPU test: KMeans exposes the seam and reports `ActualBackend`
      deterministically; requesting GPU on a non-operational host resolves to
      CPU with honest telemetry (no fabricated GPU claim).
- [ ] CPU test: the seam's requested-vs-resolved backend telemetry is asserted.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Cross-link the implemented seam from
      `docs/architecture/algorithm-variant-dispatch.md` (reconciled by `DOCS-003`).
- [ ] Note KMeans as the canonical backend-seam exemplar in `src/geometry/README.md`.

## Acceptance criteria
- [ ] A new data-parallel algorithm can adopt the seam to get CPU+GPU hooks.
- [ ] `Geometry.KMeans` reports `ActualBackend` honestly; no fabricated GPU path.
- [ ] Backend selection is read from the command/config struct and fails closed
      to the reference backend.
- [ ] No GPU dependency in the CPU gate; gate green.

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
