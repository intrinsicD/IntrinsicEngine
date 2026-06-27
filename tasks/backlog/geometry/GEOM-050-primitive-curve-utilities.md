---
id: GEOM-050
theme: none
depends_on: []
maturity_target: CPUContracted
---

# GEOM-050 — Primitive and curve utilities — Bezier, triangle metrics, sphere fit, AABB cubify

## Goal

- [ ] Add a new `Geometry.Curve` module providing a parametric Bezier curve type (Bernstein basis evaluation plus de Casteljau evaluation) of arbitrary degree, the first parametric curve type in the engine.
- [ ] Extend `Geometry.Triangle` with a stable, edge-length-driven metric suite: per-vertex `Angles`, `EdgeLengths`, `Perimeter`, stable-Heron `Area`, and a clamped `SafeAcos` helper.
- [ ] Add an iterative geometric sphere fit (fixed-point center refinement approximating true least-squares of point-to-surface distances) to `Geometry.Sphere`, alongside the existing algebraic least-squares, bounding, and hybrid fits.
- [ ] Extend `Geometry.AABB` with `MakeCubic` (expand a box to an axis-aligned cube) and octant/child box-center navigation helpers.

## Non-goals

- No spline or NURBS support beyond a single basic Bezier curve; no rational curves, no knot vectors, no curve fitting/interpolation solvers.
- No GPU backend, compute path, or shader work for any of these utilities.
- No UI, editor, or visualization surfaces.
- No changes to the existing `ToSphere` algebraic/bounding/hybrid fitting code paths beyond adding the new method enumerator and its dispatch.
- No new CTest labels.

## Context

- The geometry layer already has `Geometry.Triangle`, `Geometry.Sphere`, and `Geometry.AABB` as small primitive modules under `src/geometry/`. These are small, self-contained gaps that round out the primitive layer; the engine has no parametric curve type at all.
- `Geometry.Triangle` (`src/geometry/Geometry.Triangle.cppm`) currently exposes only `Triangle{A,B,C}` with `GetCentroid`/`GetNormal`/`GetArea` (cross-product area) plus free functions `ClosestPoint`/`SquaredDistance`/`Distance`. It has no edge-length, angle, perimeter, or stable-Heron area metrics.
- `Geometry.Sphere` (`src/geometry/Geometry.Sphere.cppm`) exposes `Sphere{Center,Radius}` and `ToSphere(std::span<const glm::vec3>, FittingParams)` with `FittingParams::FittingMethod { None, LeastSquares, Bounding, Hybrid }`. There is no iterative geometric (true least-squares-of-distances) fit.
- `Geometry.AABB` (`src/geometry/Geometry.AABB.cppm`) exposes `AABB{Min,Max}` with `GetCenter`/`GetExtents`/`GetSize`/`GetCorners` plus `Union`/`Intersection`/containment-style helpers. It has no cubify or octant-navigation helpers.
- Modules are registered in `src/geometry/CMakeLists.txt` via `intrinsic_add_module_library(IntrinsicGeometry)` with `.cppm` interfaces under a `FILE_SET CXX_MODULES` and `.cpp` implementation units in the sources list.
- Tests live under `tests/unit/geometry/` (e.g. `Test_SphereFitting.cpp`) and carry the `unit;geometry` label.
- Source naming for the ported functionality follows bcg references: `bcg_curve_bezier`, `bcg_triangle_utils`, `bcg_sphere_utils` (`FitUsingLengths`), `bcg_aabb_utils`.
- This is a geometry-layer task: geometry depends only on core. No asset/runtime/graphics/rhi/ecs/app dependencies are permitted.

## Slice plan

- [ ] Slice A — Bezier curve module + triangle metric suite: add `Geometry.Curve` (`.cppm`/`.cpp`), extend `Geometry.Triangle`, register both in `src/geometry/CMakeLists.txt`, and land their unit tests.
- [ ] Slice B — iterative sphere fit + AABB cubify/octant helpers: extend `Geometry.Sphere` (new fitting method) and `Geometry.AABB` (cubify + octant navigation), and land their unit tests.

## Required changes

- [ ] Create `src/geometry/Geometry.Curve.cppm` exporting `export module Geometry.Curve;` in `namespace Geometry`. Declare a `BezierCurve` type holding ordered control points (`std::vector<glm::vec3>`), with `[[nodiscard]] std::optional<std::uint32_t> GetDegree() const` (degree = control-point count minus one; `std::nullopt` for an empty curve). Declare two evaluators: `[[nodiscard]] std::optional<glm::vec3> EvaluateBernstein(const BezierCurve&, float t)` and `[[nodiscard]] std::optional<glm::vec3> EvaluateDeCasteljau(const BezierCurve&, float t)`. Both fail closed (return `std::nullopt`) on an empty control-point set or non-finite `t`, and clamp/validate `t` to `[0,1]` per GEOM-005. Keep only small declarations/inline in the `.cppm`.
- [ ] Create `src/geometry/Geometry.Curve.cpp` implementing `GetDegree`, `EvaluateBernstein` (Bernstein-basis weighted sum of control points using numerically stable binomial coefficients), and `EvaluateDeCasteljau` (repeated linear interpolation). Both must be deterministic and produce no NaNs; non-finite or empty input yields `std::nullopt`.
- [ ] Extend `src/geometry/Geometry.Triangle.cppm`: add member declarations on `Triangle` — `[[nodiscard]] glm::vec3 EdgeLengths() const` (returns the three edge lengths), `[[nodiscard]] float Perimeter() const`, `[[nodiscard]] glm::vec3 Angles() const` (the three interior angles in radians), and `[[nodiscard]] float StableArea() const` (Kahan stable-Heron area). Add a free function `[[nodiscard]] float SafeAcos(float x)` that clamps its argument to `[-1, 1]` before `std::acos`. Do not remove or change the signatures of existing members/free functions.
- [ ] Extend `src/geometry/Geometry.Triangle.cpp`: implement `EdgeLengths`, `Perimeter`, `Angles` (derived from edge lengths via `SafeAcos`/law of cosines so the three angles sum to π), `StableArea` (Kahan sorted-edge stable Heron formula), and `SafeAcos`. Degenerate triangles (zero-length edge / collinear vertices) must fail closed: return zeroed metrics with finite values, never NaN.
- [ ] Extend `src/geometry/Geometry.Sphere.cppm`: add `IterativeGeometric` to `FittingParams::FittingMethod`, and add iteration controls to `FittingParams` (`std::uint32_t MaxIterations` and `float ConvergenceTolerance`) with documented defaults. The new method is dispatched through the existing `ToSphere` entry point; do not add a competing public entry point.
- [ ] Extend `src/geometry/Geometry.Sphere.cpp`: implement the `IterativeGeometric` branch as a fixed-point center refinement (Coope/Gander-style: repeatedly recompute mean distance and the mean of unit offset directions, update the center, and stop at `ConvergenceTolerance` or `MaxIterations`), seeded from the centroid (or the algebraic fit). Return `std::nullopt` for fewer than four points, non-finite coordinates, or degenerate (coincident) inputs.
- [ ] Extend `src/geometry/Geometry.AABB.cppm`: add member `[[nodiscard]] AABB MakeCubic() const` (returns a cube centered on `GetCenter()` whose half-extent equals the largest half-extent, containing the original box) and octant navigation helpers — `[[nodiscard]] glm::vec3 OctantCenter(std::uint32_t octant) const` and `[[nodiscard]] AABB ChildOctant(std::uint32_t octant) const` for `octant` in `[0,7]` (bitwise x/y/z selection). Out-of-range octant indices and invalid boxes fail closed (return the invalid/default `AABB` or the box center).
- [ ] Extend `src/geometry/Geometry.AABB.cpp`: implement `MakeCubic`, `OctantCenter`, and `ChildOctant` deterministically; an invalid input `AABB` (where `!IsValid()`) must not produce NaNs and must yield a clearly-invalid result.
- [ ] Update `src/geometry/CMakeLists.txt`: add `Geometry.Curve.cppm` to the `FILE_SET CXX_MODULES` list and `Geometry.Curve.cpp` to the implementation sources of `intrinsic_add_module_library(IntrinsicGeometry)`, preserving alphabetical ordering with the existing `Geometry.AABB`/`Geometry.Sphere`/`Geometry.Triangle` entries.

## Tests

- [ ] Add `tests/unit/geometry/Test_BezierCurve.cpp` (label `unit;geometry`): a Bezier curve interpolates its first control point at `t=0` and its last control point at `t=1`; a degree-1 (two-point) curve equals the straight `lerp` of the two control points for sampled `t`; `EvaluateDeCasteljau` matches `EvaluateBernstein` within tolerance across sampled `t` for degree-2 and degree-3 curves; an empty curve and non-finite `t` both return `std::nullopt`.
- [ ] Add `tests/unit/geometry/Test_TriangleMetrics.cpp` (label `unit;geometry`): for a known triangle (e.g. a 3-4-5 right triangle) `EdgeLengths`, `Perimeter`, and `StableArea` match analytic values within tolerance; `Angles` sum to π (within tolerance) and match the known angles; `SafeAcos(x)` is finite for `x` slightly outside `[-1,1]`; a degenerate (collinear / zero-area) triangle returns finite, non-NaN zeroed metrics.
- [ ] Extend `tests/unit/geometry/Test_SphereFitting.cpp` (label `unit;geometry`): generate noisy samples on a known sphere surface and assert the `IterativeGeometric` fit recovers the known center/radius with residual error less than or equal to the `LeastSquares` (algebraic) fit on the same samples; fewer than four points and non-finite input return `std::nullopt`.
- [ ] Add `tests/unit/geometry/Test_AABBCubify.cpp` (label `unit;geometry`): `MakeCubic` produces equal extents on all three axes and fully contains the original box; the eight `ChildOctant` boxes tile the parent (their union equals the parent and they do not overlap beyond shared faces); each `OctantCenter` lies inside the corresponding child octant; an invalid input `AABB` yields a non-NaN, clearly-invalid result.
- [ ] Register every new test source in `tests/CMakeLists.txt` under the existing `IntrinsicTests` aggregation with the `unit;geometry` label (no new label introduced).

## Docs

- [ ] Regenerate `docs/api/generated/module_inventory.md` via `tools/repo/generate_module_inventory.py` so the new `Geometry.Curve` module and the extended primitive surfaces are reflected.
- [ ] Note the new `Geometry.Curve` module and the added Triangle/Sphere/AABB capabilities in the geometry-layer architecture/overview doc under `docs/architecture/` if one enumerates primitive modules (factual-current-state only; no aspirational claims).

## Acceptance criteria

- [ ] `Geometry.Curve` exists as a registered module with `BezierCurve`, `GetDegree`, `EvaluateBernstein`, and `EvaluateDeCasteljau`; both evaluators agree within tolerance and fail closed (`std::nullopt`) on empty/non-finite input.
- [ ] `Geometry.Triangle` exposes `EdgeLengths`, `Perimeter`, `Angles`, `StableArea`, and `SafeAcos`; for the test triangle the metrics match analytic values, angles sum to π, and degenerate input returns finite zeroed metrics (no NaN).
- [ ] `Geometry.Sphere::ToSphere` supports `FittingMethod::IterativeGeometric`; on noisy surface samples its residual is less than or equal to the algebraic `LeastSquares` fit, and it returns `std::nullopt` for fewer than four points or non-finite input.
- [ ] `Geometry.AABB` exposes `MakeCubic`, `OctantCenter`, and `ChildOctant`; `MakeCubic` yields equal extents containing the original, the eight child octants tile the parent, and invalid input fails closed without NaNs.
- [ ] All new/changed tests pass under the focused CTest invocation in Verification; `check_layering.py --strict`, `check_test_layout.py --strict`, `check_doc_links.py`, and `check_task_policy.py --strict` all succeed.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'BezierCurve|TriangleMetrics|SphereFitting|AABBCubify' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Introducing renderer/runtime/ECS/assets/platform/app dependencies into the geometry layer; `src/geometry/*` must continue to import only core.
- Mixing mechanical file moves or renames with the semantic additions in this task.
- Introducing unrelated feature work (spline/NURBS, GPU paths, UI) under cover of this task.
- Claiming any performance improvement for the iterative sphere fit without a baseline comparison; the only fit claim permitted is the residual-vs-algebraic correctness assertion in the tests.
- Adding new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.
- Altering the existing `ToSphere` algebraic/bounding/hybrid behavior or the existing `Triangle`/`AABB` member signatures.

## Maturity

- Stop-state: CPUContracted. The four utilities (Bezier curve, triangle metric suite, iterative sphere fit, AABB cubify/octant navigation) are fully implemented on the CPU with deterministic, fail-closed behavior on degenerate/empty/non-finite input and are covered by the unit tests above. No GPU/optimized backend, parity harness, or downstream integration is in scope; those would be later Operational/ParityProven work.
