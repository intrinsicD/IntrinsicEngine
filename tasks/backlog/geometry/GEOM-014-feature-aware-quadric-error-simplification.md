---
id: GEOM-014
theme: I
depends_on: []
---
# GEOM-014 — Feature-aware quadric error mesh simplification

## Goal
- Extend `Geometry.HalfedgeMesh.Simplification` with a feature-aware quadric error formulation that preserves sharp boundaries, normal discontinuities, and attribute discontinuities under aggressive decimation. Adds new error terms behind opt-in flags; the existing classical-QEM behaviour remains as the off-by-default fallback.

## Non-goals
- No replacement of `Geometry.HalfedgeMesh.Simplification` — this is an in-place extension.
- No GPU backend.
- No neural / learned simplification.
- No progressive-mesh / view-dependent LOD machinery in this task (separate follow-up).

## Context
- Status: implemented on branch `claude/intrinsic-engine-framework24-migrate-cew18n`;
  full `ctest` gate deferred to CI (see Status below).
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 2 #8.
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
  `v:texcoord`, and `Result` diagnostics counters. `ClassicalQEM` reproduces the
  pre-GEOM-014 behaviour exactly.
- The boundary-curvature term is realized as a turning-angle-thresholded pin
  (scaled by `BoundaryWeight`/`CurvatureWeight`) rather than a separate additive
  boundary quadric — simpler, deterministic, and avoids a second per-vertex
  quadric store. Recorded here as the deliberate deviation from the sketch.
- Verified in-session: `check_layering`, `check_test_layout`, `check_doc_links`,
  `check_task_policy` (all pass) and a standalone clang-20 + glm sanity check of
  the new feature/turning-angle/penalty math.
- Deferred to CI: `cmake --preset ci` + `ctest -R Simplification`. The sandbox
  cannot bootstrap vcpkg (cloning `microsoft/vcpkg` is denied by the egress
  policy), so the full C++23-module build/test gate must run in CI before this
  task retires to `tasks/done/`.
- Editor/runtime execution wiring (a Simplification executor + the FA_QEM metric
  panel in the sandbox editor) is owned by follow-up `UI-028`; today the editor
  only lists Simplification as a capability and does not execute it.

## Variants and default selection

Mark `[x]` next to the variant that should become the **default error metric**. Other metrics remain selectable via the `Params::metric` enum.

- [ ] **A — Classical QEM (Garland & Heckbert, SIGGRAPH 1997).** Plane-distance quadric only. Fastest, smallest code. Current behaviour.
- [x] **B — FA-QEM: Feature-Aware QEM (Cao et al., arXiv:2605.14029, 2025).** Joint encoding of geometric deviation, boundary curvature, and surface normal consistency. **Selected as the new default per the recommendation below.**
- [ ] **C — Surface Simplification Using Intrinsic Error Metrics (arXiv:2305.06410).** Intrinsic-Delaunay-aware error metric; pairs well with future intrinsic-triangulation work. Pick if intrinsic geometry is a year-1 priority.
- [ ] **D — Line-quadric variant (Hsueh-Ti Derek Liu et al., controlling QEM with line constraints).** Adds line-feature pinning. Useful for CAD / architectural assets.

Default recommendation: **B** (FA-QEM) — the survey's strongest "easy win," directly relevant to LOD / asset pipelines, no new dependencies beyond what the existing simplification module already uses.

## Required changes

- [x] Extend the public surface in `src/geometry/Geometry.HalfedgeMesh.Simplification.cppm`
  (aligned to the real `Geometry::Simplification` surface): added
  `enum class Metric { ClassicalQEM, FA_QEM }`, FA_QEM `Params` fields
  (`Metric` default `FA_QEM`, `FeatureAngleThresholdDegrees`, `NormalWeight`,
  `BoundaryWeight`, `CurvatureWeight`, `PreserveSharpFeatures`,
  `PreserveUvSeams`), and `Result` diagnostics
  (`CollapsesRejectedTopology/Quality`, `SharpFeatureVerticesPinned`,
  `SeamVerticesPinned`). Variants C (IntrinsicQEM) / D (LineQEM) are documented
  as out-of-scope follow-ups, not enum stubs.
- [x] Implement FA-QEM terms in `src/geometry/Geometry.HalfedgeMesh.Simplification.cpp`:
  geometric quadric (existing path), boundary-curvature term (turning-angle pin),
  normal-consistency penalty (angle between adjacent face normals before/after).
- [x] Expose weights through `Params`; the normal penalty is folded into the
  collapse cost so it combines additively with the quadric error.
- [x] Keep the classical-QEM path reachable via `Metric::ClassicalQEM` for parity.
- [ ] (Deferred follow-up) `IntrinsicQEM` / `LineQEM` variants — not in scope; no
  enum stub added until a follow-up task owns them.

## Tests
- [x] Extended `tests/unit/geometry/Test_Simplification.cpp` with:
  - `FeatureAwarePreservesCubeCorners`: tessellated-cube fixture, aggressive
    (~10%) decimation preserves all 8 corner vertices under `Metric::FA_QEM`.
  - `FeatureAwareCornerErrorNotWorseThanClassical`: FA-QEM max vertex-to-surface
    error ≤ classical at the same target face count on the cube (the feature-rich
    stand-in for bunny/fertility, which are not checked-in fixtures here).
  - `FeatureAwarePreservesUvSeams`: grid plane with a `v:texcoord` property —
    seam (boundary) vertices remain intact when `PreserveUvSeams = true`.
- [x] `FeatureAwareIsDeterministic`: identical output for fixed input/params.
- [x] `DiagnosticsCountersPopulated`: feature-pin counters populated on the cube.
- [ ] (Deferred to CI) `cmake --preset ci && ctest -R Simplification` — not
      runnable in the authoring sandbox (vcpkg egress blocked).

## Docs
- [x] Updated the module comment block at the top of
      `Geometry.HalfedgeMesh.Simplification.cppm` with the metric options and the
      `FA_QEM` default.
- [x] Added a classical-QEM vs FA-QEM quality-comparison stub to
      `benchmarks/geometry/README.md` (quality metrics only, no perf claims; the
      compiled smoke runner is left to the executor follow-up).
- [x] Regenerated `docs/api/generated/module_inventory.md` (no surface delta —
      the inventory tracks module names; `Geometry.Simplification` pre-existed).

## Acceptance criteria
- [ ] One variant marked default.
- [ ] Cube-corner test passes for the default metric.
- [ ] Hausdorff-distance test shows FA-QEM ≤ classical QEM at the documented target ratio.
- [ ] UV-seam preservation test passes.
- [ ] No public API break for existing callers — old `Params` field defaults preserve previous behaviour if `Metric::ClassicalQEM` is explicitly requested.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Simplification' --timeout 60
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
- Target: `CPUContracted` (in-place extension of the existing CPU simplification module).
- No `Operational` follow-up is owed; this task has no backend seam.
