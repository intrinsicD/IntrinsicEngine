---
id: GEOM-014
theme: I
depends_on: []
completed_on: 2026-07-15
---
# GEOM-014 — Feature-aware quadric error mesh simplification

## Goal
- Extend `Geometry.HalfedgeMesh.Simplification` with a scoped, paper-inspired
  feature-aware quadric error adaptation that preserves sharp boundaries,
  normal discontinuities, and attribute discontinuities under aggressive
  decimation. The feature-aware metric is the documented default and the
  existing classical-QEM contract remains an explicit parity fallback.

## Non-goals
- No replacement of `Geometry.HalfedgeMesh.Simplification` — this is an in-place extension.
- No GPU backend.
- No neural / learned simplification.
- No progressive-mesh / view-dependent LOD machinery; no task is owned here.

## Context
- Status: implemented in commit `d4aab123` and verified for retirement on
  2026-07-15 with the repository `ci` preset and focused CTest gate.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 2 #8.
- Targets the existing module `Geometry.Simplification` (module name; files
  `src/geometry/Geometry.HalfedgeMesh.Simplification.cppm` + `.cpp`). The task
  sketch's `Geometry::HalfedgeMesh::Simplification` namespace and
  `Core::Expected` return were aligned to the real surface
  (`Geometry::Simplification`, `std::optional<Result>`, in-place mutation).
- Test target: `tests/unit/geometry/Test_Simplification.cpp` (existing; extend, do not replace).

## Status

- Implemented: `Metric` enum (`ClassicalQEM` parity path / `FA_QEM` default),
  feature-aware sharp-corner + crease pinning, normal-consistency cost penalty,
  curvature-thresholded boundary-corner pinning, UV-seam pinning over
  `v:texcoord`, and `Result` diagnostics counters. `ClassicalQEM` selects the
  legacy quadric-only path; its contract test proves all FA-only controls are
  ignored and produce identical compacted topology under that selection.
- This is a scoped adaptation inspired by Bhosikar, Savalia, Tiwari, and
  Bhowmick (arXiv:2605.14029, 2026), not an equation-level reproduction of the
  paper's multi-term quadric or optimal-placement formulation. The
  boundary-curvature mechanism is realized as a turning-angle-thresholded pin
  (scaled by `BoundaryWeight`/`CurvatureWeight`) rather than a separate additive
  boundary quadric — simpler, deterministic, and avoids a second per-vertex
  quadric store. Recorded here as the deliberate deviation from the sketch.
- Retirement verification: `cmake --preset ci` selected Clang 23 with
  ASan/UBSan, `IntrinsicTests` built successfully, and the exact
  single-binary `Simplification*` selection passed 29/29; all 93 registered
  `Simplification` matches passed inside the full 9250/9250 CPU gate. The
  manifest-backed benchmark run and validation passed 2/2, all 24 benchmark
  manifests and 20 emitted result payloads validate strictly, and this workload
  emitted `status: passed` with both variants at 24 faces, 8 immutable corners,
  0 failed measured iterations, and a 1255.539 ms mean below its 5000 ms smoke
  budget. Layering, test-layout, task-policy, task-state, and documentation
  checks pass; the generated module inventory is current.
- Editor/runtime execution wiring retired separately under `UI-028`; its
  `Mesh > Processing > Simplify` path consumes this geometry-owned kernel.

## Variants and default selection

Mark `[x]` next to the variant that should become the **default error metric**. Other metrics remain selectable via the `Params::metric` enum.

- **A — Classical QEM (Garland & Heckbert, SIGGRAPH 1997).** Plane-distance
  quadric only. Fastest, smallest code; retained as the legacy behavior.
- [x] **B — Scoped adaptation inspired by “Fast and Robust Mesh Simplification
  for Generated and Real-World 3D Assets” (Bhosikar, Savalia, Tiwari, and
  Bhowmick, arXiv:2605.14029, 2026).** The paper jointly encodes geometric
  deviation, boundary curvature, and normal consistency; the landed engine
  adaptation uses feature/turning-angle pinning plus a normal-consistency cost.
  **Selected as the new default per the recommendation below.**
- **C — Surface Simplification Using Intrinsic Error Metrics (arXiv:2305.06410).** Intrinsic-Delaunay-aware error metric; pairs well with future intrinsic-triangulation work. Pick if intrinsic geometry is a year-1 priority.
- **D — Line-quadric variant (Hsueh-Ti Derek Liu et al., controlling QEM with line constraints).** Adds line-feature pinning. Useful for CAD / architectural assets.

Default recommendation: **B** (the scoped FA-QEM adaptation) — directly
relevant to LOD / asset pipelines, with no new dependencies beyond what the
existing simplification module already uses.

## Required changes

- [x] Extend the public surface in `src/geometry/Geometry.HalfedgeMesh.Simplification.cppm`
  (aligned to the real `Geometry::Simplification` surface): added
  `enum class Metric { ClassicalQEM, FA_QEM }`, FA_QEM `Params` fields
  (`Metric` default `FA_QEM`, `FeatureAngleThresholdDegrees`, `NormalWeight`,
  `BoundaryWeight`, `CurvatureWeight`, `PreserveSharpFeatures`,
  `PreserveUvSeams`), and `Result` diagnostics
  (`CollapsesRejectedTopology/Quality`, immutable-corner count
  `SharpFeatureVerticesPinned`,
  `SeamVerticesPinned`). Variants C (IntrinsicQEM) / D (LineQEM) are documented
  as unowned future possibilities, not enum stubs.
- [x] Implement the scoped FA-QEM-inspired mechanisms in
  `src/geometry/Geometry.HalfedgeMesh.Simplification.cpp`: geometric quadric
  (existing path), boundary turning-angle pinning, and a normal-consistency
  penalty (angle between adjacent face normals before/after). These are
  deliberately not described as the paper's equation-level formulation.
- [x] Expose weights through `Params`; the normal penalty is folded into the
  collapse cost so it combines additively with the quadric error.
- [x] Keep the classical-QEM path reachable via `Metric::ClassicalQEM` for parity.
- [x] Confirmed `IntrinsicQEM` / `LineQEM` variants are out of scope; no enum
  stubs were added for unowned possibilities.

## Tests
- [x] Extended `tests/unit/geometry/Test_Simplification.cpp` with:
  - `FeatureAwarePreservesCubeCorners`: tessellated-cube fixture, aggressive
    (~10%) decimation preserves all 8 corner vertices under `Metric::FA_QEM`.
  - `FeatureAwareCornerErrorNotWorseThanClassical`: deterministic barycentric
    samples on the original cube surface query the exact closest face on each
    result; FA-QEM sampled maximum surface distance is no worse than classical
    within numerical tolerance after both reach exactly 24 faces. A translated
    result control proves the metric is sensitive rather than identically zero.
  - `FeatureAwarePreservesUvSeams`: grid plane with a `v:texcoord` property —
    seam (boundary) vertices remain intact when `PreserveUvSeams = true`.
- [x] `FeatureAwareIsDeterministic`: identical output for fixed input/params.
- [x] `ClassicalMetricRemainsReachable`: changing all FA-only controls under
      `ClassicalQEM` leaves result diagnostics and compacted topology identical.
- [x] `DiagnosticsCountersPopulated`: feature-pin and evaluated-candidate
      quality-rejection counters are populated on the cube.
- [x] `cmake --preset ci`, `cmake --build --preset ci --target IntrinsicTests`,
      and the focused single-binary `Simplification*` gate pass (29/29); all 93
      registered matches also pass in the full CPU-supported gate.

## Docs
- [x] Updated the module comment block at the top of
      `Geometry.HalfedgeMesh.Simplification.cppm` with the metric options and the
      `FA_QEM` default.
- [x] Added stable benchmark ID
      `geometry.simplification.fa_qem_quality.smoke`, its manifest-backed
      `IntrinsicBenchmarkSmoke` workload, result JSON diagnostics, and the
      classical-QEM versus FA-QEM quality contract to
      `benchmarks/geometry/README.md`; it makes no performance claim.
- [x] Regenerated `docs/api/generated/module_inventory.md` (no surface delta —
      the inventory tracks module names; `Geometry.Simplification` pre-existed).

## Acceptance criteria
- [x] One variant is marked default.
- [x] Cube-corner test passes for the default metric.
- [x] The sensitive sampled original-surface-to-result-surface comparison shows
      FA-QEM no worse than classical QEM within numerical tolerance after both
      reach the same documented 24-face target.
- [x] UV-seam preservation test passes.
- [x] No public API break for existing callers: the classical behavior remains
      reachable by explicitly selecting `Metric::ClassicalQEM`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Simplification' --timeout 60
build/ci/bin/IntrinsicGeometryTests --gtest_filter='Simplification*'
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R '^IntrinsicBenchmarkSmoke\.(Run|Validate)$' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No rewrite of the existing classical-QEM code path; classical-QEM behaviour must remain reachable.
- No silent change of default semantics for existing callers — if `Metric` is unset, document the chosen default in the module header and update one consumer test at a time.
- No neural / learned variant in this task.
- No GPU backend.

## Maturity
- Reached: `CPUContracted` (in-place extension of the existing CPU simplification module, covered by the focused CPU contract gate).
- No `Operational` follow-up is owed; this task has no backend seam.

Completed: 2026-07-15. Implementation commit: `d4aab123`; retirement metadata
and final verification are recorded by this GEOM-014 change set.
