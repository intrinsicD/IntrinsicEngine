---
id: GEOM-043
theme: none
depends_on: [GEOM-039, GEOM-040]
maturity_target: CPUContracted
---

# GEOM-043 — Remeshing surface reprojection and error-bounded adaptive sizing

## Goal

- [ ] Add optional reference-surface reprojection to the **uniform** isotropic remeshing path (`Geometry.Remeshing`), so remeshed vertices are projected back onto a frozen copy of the input surface after each remeshing step, bounding drift away from the reference geometry.
- [ ] Add an **error-bounded Taubin-curvature sizing law** option to **adaptive** remeshing (`Geometry.HalfedgeMesh.AdaptiveRemeshing`): the alternative law `h = sqrt(6*e*r - 3*e^2)` driven by the Taubin curvature radius `r` (reciprocal of principal curvature from GEOM-040) and an approximation-error bound `e`, offered alongside the existing mean-curvature law `L = L_base / (1 + alpha*|H|)`.
- [ ] Reuse existing machinery: the uniform path must drive reprojection through `AdaptiveRemeshing::ReferenceProjector` and the GEOM-039 accelerated closest-face query, not a private linear scan; the new sizing option must keep IntrinsicEngine's existing safety caps (`MinEdgeLength` / `MaxEdgeLength` clamp, `MaxOpsPerIteration`, `MaxTopologyGrowthFactor`) that bcg lacks.

## Non-goals

- No rewrite of the existing remeshing operators (split / collapse / flip / tangential smoothing) on either the uniform or adaptive path; this task only adds an optional projection step and an alternative sizing law.
- No GPU backend; this is a CPU-only contract under the default correctness gate.
- No UI / editor work; `UI-025` owns the editor remesh/subdivide windows and is the only consumer permitted to surface these options in the editor.
- No new geometric primitive types and no change to the existing mean-curvature sizing law's numerics when the new option is not selected.
- No performance claim; this task asserts correctness and tolerance bounds only.

## Context

- Status: backlog. Owning subsystem/layer: `geometry` (`geometry -> core` only). No runtime/graphics/ecs/assets/app coupling.
- Target modules: `Geometry.Remeshing` (uniform; interface `src/geometry/Geometry.HalfedgeMesh.Remeshing.cppm`, implementation `src/geometry/Geometry.HalfedgeMesh.Remeshing.cpp`, namespace `Geometry::Remeshing`) and `Geometry.HalfedgeMesh.AdaptiveRemeshing` (adaptive; interface `src/geometry/Geometry.HalfedgeMesh.AdaptiveRemeshing.cppm`, implementation `src/geometry/Geometry.HalfedgeMesh.AdaptiveRemeshing.cpp`, namespace `Geometry::AdaptiveRemeshing`).
- `Geometry::Remeshing::RemeshingParams` currently carries only `TargetLength`, `Iterations`, `Lambda`, and `PreserveBoundary`. It has **no** reprojection support; reprojection exists only on the adaptive path, where `AdaptiveRemeshingParams` already exposes `EnableReferenceProjection`, `ReferenceProjectionK`, `MaxReferenceProjectionDistance`, `ProjectSplitVertices`, and `ProjectAfterSmoothing`, backed by a `ReferenceProjector` defined in `Geometry.HalfedgeMesh.AdaptiveRemeshing.cpp`.
- Adaptive sizing currently uses only the mean-curvature law `L(v) = L_base / (1 + alpha*|H(v)|)` clamped to `[MinEdgeLength, MaxEdgeLength]`. There is no error-bounded Taubin-radius law.
- Features ported from bcg: uniform reprojection (`bcg RemeshingUniform`) and the error-bounded adaptive sizing law (`bcg RemeshingAdaptive`, `h = sqrt(6*e*r - 3*e^2)`). bcg lacks the operation/topology-growth safety caps this engine already has; those caps must remain in force under the new law.
- Dependency note (soft-depends): Slice A soft-depends on **GEOM-039** (accelerated closest-face query) and Slice B soft-depends on **GEOM-040** (Taubin curvature tensor / principal-curvature magnitudes). Each slice can fall back to a local projector / local curvature estimate if its dependency has not yet landed: if GEOM-039 is unlanded, Slice A reuses the existing `ReferenceProjector`'s current closest-face implementation; if GEOM-040 is unlanded, Slice B derives the curvature radius `r` from the existing scalar principal-curvature outputs already available to adaptive remeshing. The fallback must be deterministic and fail-closed identically to the accelerated path.
- GEOM-005 (API/numeric policy) and GEOM-007 (robust-predicate/tolerance policy): both additions must be deterministic, fail-closed on degenerate/empty/non-finite input with explicit diagnostics, no asserts, no NaNs.

## Slice plan

- [ ] Slice A — Uniform reprojection (soft-depends GEOM-039). Add reprojection params to `RemeshingParams`, freeze a reference surface on entry, and project remeshed vertices back to the reference after each step via `AdaptiveRemeshing::ReferenceProjector` + the GEOM-039 nearest-face query (or the local fallback). Defers the sizing law entirely.
- [ ] Slice B — Error-bounded sizing-law option (soft-depends GEOM-040). Add an alternative sizing law `h = sqrt(6*e*r - 3*e^2)` selectable in `AdaptiveRemeshingParams`, driven by the Taubin curvature radius and an error bound `e`, with the existing safety caps preserved. Defers nothing further.

## Required changes

- [ ] Slice A — `src/geometry/Geometry.HalfedgeMesh.Remeshing.cppm`: extend `struct RemeshingParams` with reprojection fields mirroring the adaptive ones (no behavior change when disabled):
  - [ ] `bool ProjectToSurface{false};` — when true, project remeshed vertices back onto a frozen copy of the input surface after each remeshing step.
  - [ ] `std::size_t ReferenceProjectionK{16};` — KD-tree / BVH candidate shortlist size for nearest-face projection.
  - [ ] `double MaxReferenceProjectionDistance{0.0};` — maximum allowed projection distance; `<= 0` disables distance clamping.
  - [ ] Keep declarations and doc-comments only in the interface; the projection body lives in the `.cpp`.
- [ ] Slice A — `src/geometry/Geometry.HalfedgeMesh.Remeshing.cpp`: when `ProjectToSurface` is set, build the frozen reference surface once on entry and project updated vertices after split / collapse / flip / smoothing steps by **calling `AdaptiveRemeshing::ReferenceProjector`** (do not add a private linear nearest-face scan). Route its nearest-face lookup through the GEOM-039 accelerated closest-face query when available, falling back to the existing `ReferenceProjector` path otherwise.
- [ ] Slice A — `src/geometry/Geometry.HalfedgeMesh.AdaptiveRemeshing.cppm`: if `ReferenceProjector` is not already exported, promote the minimal reusable surface (construct-from-mesh + `Project(point) -> {point, FaceHandle, distance}`) so the uniform path can consume it without duplicating the implementation. Export only the small declaration; keep the body in the `.cpp`.
- [ ] Slice B — `src/geometry/Geometry.HalfedgeMesh.AdaptiveRemeshing.cppm`: extend `struct AdaptiveRemeshingParams` with the alternative-law selector and its error bound:
  - [ ] `enum class SizingLaw { MeanCurvature, ErrorBoundedTaubin };` and `SizingLaw Sizing{SizingLaw::MeanCurvature};` (default preserves current behavior).
  - [ ] `double ApproximationError{0.0};` — the error bound `e` for the `h = sqrt(6*e*r - 3*e^2)` law; `<= 0` disables the law (fall back to mean-curvature) with a diagnostic.
- [ ] Slice B — `src/geometry/Geometry.HalfedgeMesh.AdaptiveRemeshing.cpp`: implement the error-bounded sizing branch. Compute the per-vertex curvature radius `r` from the Taubin principal-curvature magnitude (GEOM-040 output, or the local-fallback scalar principal curvature), evaluate `h = sqrt(6*e*r - 3*e^2)`, guard the radicand (`6*e*r - 3*e^2 <= 0` → fall back to `MaxEdgeLength` for near-flat / under-resolved regions, never NaN), then apply the **existing** `[MinEdgeLength, MaxEdgeLength]` clamp and leave `MaxOpsPerIteration` / `MaxTopologyGrowthFactor` caps in force.
- [ ] Both slices — fail closed (GEOM-005 / GEOM-007): empty mesh, fewer than 2 faces, non-triangle faces, zero-area / degenerate faces, non-finite vertex positions or non-finite `e`, and a projection that exceeds `MaxReferenceProjectionDistance` (when set) each yield an explicit diagnostic and a defined outcome (skip-vertex / nullopt), with no NaNs and no asserts.
- [ ] Update the geometry `CMakeLists.txt` (`target_sources(... FILE_SET CXX_MODULES ...)` / `intrinsic_add_module_library`) only if a new translation unit is added; do not introduce a new module library for this work.

## Tests

- [ ] Uniform reprojection tolerance: on a curved input (e.g. a sphere / torus), remeshing with `ProjectToSurface = true` keeps every output vertex within a bounded max distance of the frozen reference surface, and that max distance is strictly smaller than the drift produced by the un-projected (`ProjectToSurface = false`) run on the same input and seed.
- [ ] Reprojection routing: the uniform path produces results consistent with `AdaptiveRemeshing::ReferenceProjector` (shared projector, not a private scan); with GEOM-039 present the projected positions match the local-fallback positions for the same query points.
- [ ] Error-bounded sizing refinement: on a mesh with mixed curvature, the `ErrorBoundedTaubin` law produces finer triangles (shorter local edges / more faces) in high-curvature regions than in flat regions, and yields a different distribution than the `MeanCurvature` law on the same mesh.
- [ ] Safety caps respected: under the new law, no output edge is shorter than `MinEdgeLength` or longer than `MaxEdgeLength`, and `MaxOpsPerIteration` / `MaxTopologyGrowthFactor` still bound the per-iteration operations and total mesh growth (assert via the result diagnostics).
- [ ] Radicand guard: inputs producing `6*e*r - 3*e^2 <= 0` (near-flat regions, large radius) fall back to `MaxEdgeLength` with no NaN edge length.
- [ ] Determinism: both new paths are deterministic — identical input + params + seed produce byte-identical output topology and positions across repeated runs.
- [ ] Degenerate fail-closed: empty mesh, mesh with fewer than 2 faces, mesh with a non-triangle face, mesh with a zero-area face, non-finite vertex, and non-finite / non-positive `ApproximationError` each return an explicit invalid result (nullopt or skipped vertex) with a diagnostic, no NaNs, no asserts.
- [ ] Default-preserving: with `ProjectToSurface = false` and `Sizing = MeanCurvature`, uniform and adaptive output is bit-for-bit identical to the pre-change behavior on a fixed seed corpus.
- [ ] All new tests carry the `unit;geometry` label; no new CTest labels are introduced.

## Docs

- [ ] Document the uniform `ProjectToSurface` option (semantics, the shared `ReferenceProjector` + GEOM-039 query usage, the distance-clamp behavior, fail-closed cases) in the relevant geometry API doc under `docs/`, referencing GEOM-005 / GEOM-007.
- [ ] Document the `ErrorBoundedTaubin` sizing law (`h = sqrt(6*e*r - 3*e^2)`, the meaning of `e` and `r`, the radicand guard, and that the existing safety caps still apply), and note the soft-dependency / fallback on GEOM-040.
- [ ] Regenerate the module inventory (`docs/api/generated/module_inventory.md`) so the new exported params / enum / projector symbol are recorded.

## Acceptance criteria

- [ ] `RemeshingParams` exposes `ProjectToSurface` (plus `ReferenceProjectionK` / `MaxReferenceProjectionDistance`); enabling it bounds output-vertex distance to the frozen reference surface and reduces drift versus the un-projected run, driven through the shared `ReferenceProjector` and (when present) the GEOM-039 query — with no private linear nearest-face scan added to the uniform path.
- [ ] `AdaptiveRemeshingParams` exposes a `SizingLaw` selector with an `ErrorBoundedTaubin` option and an `ApproximationError` bound; selecting it applies `h = sqrt(6*e*r - 3*e^2)`, refines high-curvature regions more than flat regions, and still honors the `[MinEdgeLength, MaxEdgeLength]` clamp and the `MaxOpsPerIteration` / `MaxTopologyGrowthFactor` caps.
- [ ] With the new options disabled, uniform and adaptive output is bit-for-bit identical to the pre-change baseline on the fixed corpus.
- [ ] All listed degenerate / radicand-guard inputs return explicit invalid or defined results with no NaNs and no asserts; both new paths are deterministic across repeated runs.
- [ ] `check_layering.py --strict` passes: no geometry -> assets/runtime/graphics/rhi/ecs/app dependency is introduced.
- [ ] The full Verification block below passes locally.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Geometry\.(Remeshing|HalfedgeMesh\.AdaptiveRemeshing|HalfedgeMesh\.Curvature)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work outside uniform reprojection and the error-bounded sizing-law option.
- Rewriting or altering the numerics of the existing remeshing operators or the existing mean-curvature sizing law when the new option is not selected.
- Adding a private linear nearest-face scan to the uniform path instead of reusing `AdaptiveRemeshing::ReferenceProjector` / the GEOM-039 query.
- Dropping or weakening the existing safety caps (`MinEdgeLength` / `MaxEdgeLength` clamp, `MaxOpsPerIteration`, `MaxTopologyGrowthFactor`) under the new sizing law.
- Introducing renderer/runtime/ECS/assets/platform/app dependencies into geometry.
- Adding a GPU path or new primitive types.
- Claiming performance improvements without a baseline comparison (this task asserts correctness and tolerance bounds only).
- Introducing new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.

## Maturity

- Stop-state for this task is `CPUContracted`: Slice A lands a contracted, CPU-correct uniform reprojection option that reuses the shared `ReferenceProjector` (and the GEOM-039 query when present, local fallback otherwise) with a proven distance bound and fail-closed degenerate handling; Slice B lands the contracted `ErrorBoundedTaubin` sizing law driven by the Taubin curvature radius (GEOM-040 or local fallback) with the existing safety caps intact, fully covered by tolerance / refinement / cap / determinism / degenerate unit tests. No GPU / optimized backend and no benchmark claim is in scope; reaching `Operational` / `ParityProven` is deferred to a later task.
