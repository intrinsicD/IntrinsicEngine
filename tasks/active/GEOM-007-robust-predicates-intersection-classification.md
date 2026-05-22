# GEOM-007 — Robust predicates and intersection classification foundation

## Goal
- Add a reusable robust-predicate and intersection-classification foundation for geometry algorithms that need reliable orientation, incidence, barycentric, and primitive-intersection decisions.

## Non-goals
- No full mesh boolean rewrite.
- No tetrahedralization implementation.
- No arrangement/BSP kernel beyond foundational predicates and classification records.
- No dependency on broad external geometry frameworks such as CGAL/libigl in the core geometry layer.

## Context
- Status: in-progress (Slice 3.3.c — Containment frustum migration landed; remaining Slice 3.3 work is the deferred Slice 3.3.d GJK termination follow-up, otherwise Slice 3 callsite adoption is at its planned stopping point).
- Owner/agent: copilot.
- Branch: main (single-commit slices; promote to a feature branch if a slice batches multiple commits).
- Next verification step: run focused ctest filter `RobustPredicates|IntersectionClassification|RayTriangleClassify|RaycastClassify` plus the layering / test-layout / doc-links / task-policy structural checks for Slice 3.3.a; extend the filter to `Overlap` / `Containment` (and add the new parity-battery test names) at the corresponding 3.3.b / 3.3.c commits.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md) and called out by [`docs/architecture/geometry-api-style.md`](../../docs/architecture/geometry-api-style.md) §"Numeric policy".
- Current geometry code has many collision/query algorithms but lacks a common robust predicate policy for degeneracies, nearly coincident elements, and exact/filtered decisions.
- This task should be completed before expanding booleans, remeshing, tetrahedralization, arrangements, or reconstruction robustness.

## Slice plan

- **Slice 1 (landed `1ac8720d`) — predicate foundation.** Narrow
  `Geometry.RobustPredicates` module with orientation 2D/3D, signed-distance,
  in-plane triangle barycentric classification, scale-aware epsilon helpers,
  and `Sign`/`Certainty`/`BarycentricRegion` diagnostic enums plus the unit
  test suite. No callsite refactors.
- **Slice 2 (landed `a2f39b30`) — intersection classification records.**
  Sibling module `Geometry.IntersectionClassification` defining the result
  records and intersection-kind enums for segment-segment, segment-triangle,
  ray-triangle, triangle-triangle, and point-triangle incidence cases.
  Records only; no intersection algorithms shipped, no existing caller
  refactored.
- **Slice 3 — callsite adoption (in progress).** Migrate existing geometry
  callsites one at a time onto the Slice 1 predicates and Slice 2 records,
  with parity tests. Each callsite is a separate reviewable commit.
  - **Slice 3.1 (this PR) — `Geometry::RayTriangle_Classify`.** Add a
    classifying companion to `RayTriangle_Watertight` that returns
    `Intersection::RayTriangleResult` and shares the watertight numerical
    kernel so the geometric fields are bit-exact identical. Existing
    callers (`src/legacy/Runtime/Runtime.Selection.cpp`, `Test_Raycast.cpp`)
    stay on the legacy API; their migration is queued as Slice 3.2.
  - **Slice 3.2 (landed this PR) — Runtime.Selection adoption.** Migrated
    all five `RayTriangle_Watertight` callsites in
    `src/legacy/Runtime/Runtime.Selection.cpp` onto
    `RayTriangle_Classify` so picking/refinement consumes the
    diagnostic vocabulary (`Intersection::HasIntersection`, `hit.Point`,
    `hit.WA`/`WB`). Legacy `RayTriangle_Watertight` stays as a thin
    alias until the last out-of-tree caller is gone.
  - **Slice 3.3 — Overlap / Containment / GJK migration.** Repeat the
    `Geometry.RobustPredicates` adoption pattern at the next-highest-
    traffic callsites; each gets its own commit. Sub-sliced because the
    candidate callsites use different evaluation paths (Hessian-form
    plane sign, OBB-vs-OBB SAT epsilon, GJK termination epsilon) and
    each has a different behavior-change blast radius.
    - **Slice 3.3.a (landed this PR) — Hessian-plane predicate helper.**
      Added `RobustPredicates::SignedDistanceToHessianPlane(planeNormal,
      planeOffset, query)` companion to the existing
      `SignedDistanceToPlane(origin, normal, query)` that evaluates the
      Hessian-form plane equation `dot(N, q) + d` in double precision
      with the same `SignedResult` + filter-bound diagnostic shape.
      `Test.RobustPredicates.cpp` gains exact-on-plane, above/below
      certainty, parity-with-origin-form (for unit `N`), scaled-normal,
      and large/small scale decidability cases. No callsite migration
      in this sub-slice; just the predicate foundation Slices 3.3.b /
      3.3.c consume.
    - **Slice 3.3.b (landed this PR) — `Overlap` frustum plane tests.**
      Migrated the two `SDF::Math::Sdf_Plane(...) < 0` /
      `< -s.Radius` callsites in
      `Geometry::Internal::Overlap_Analytic(Frustum, AABB)` and
      `Overlap_Analytic(Frustum, Sphere)` (`src/geometry/Geometry.Overlap.cppm`)
      onto `RobustPredicates::SignedDistanceToHessianPlane`. Decision
      policy: AABB path culls only when `Sign::Negative` AND
      `Certainty::Certain`; sphere path culls only when
      `signed.Value < -radius - signed.FilterBound`. Both are
      conservative-inside — uncertain near-boundary cases stay visible.
      `Test_Overlap.cpp` gains a `Frustum`-vs-`AABB` test set
      (Inside / OutsideBehindCamera / OutsideFarBeyondFar /
      StraddlingNearPlane / OffToTheSide) and parity batteries
      (`FrustumAABBBatteryAgreesWithLegacyOracle` and
      `FrustumSphereBatteryAgreesWithLegacyOracle`) that sweep boxes /
      spheres across the view volume and assert the migrated
      implementation never culls geometry the legacy `Sdf_Plane`-based
      oracle kept (false-negative is the dangerous direction). The
      reverse direction is documented and allowed by the
      conservative-inside policy.
    - **Slice 3.3.c (landed this PR) — `Containment` frustum strict-containment.**
      Migrated the two `Sdf_Plane(...) < 0` / `< s.Radius` callsites in
      `Geometry::Internal::Contains_Analytic(Frustum, AABB)` and
      `Contains_Analytic(Frustum, Sphere)`
      (`src/geometry/Geometry.Containment.cppm`) onto
      `RobustPredicates::SignedDistanceToHessianPlane`. Decision policy
      for strict containment is the inverse of 3.3.b: AABB path reports
      contained only when every plane is `Certainty::Certain` AND
      `Sign` is non-negative; sphere path requires
      `signed.Value >= radius + signed.FilterBound`. Both are
      conservative-EXCLUDE — uncertain near-boundary primitives are
      reported as "not contained" so callers do not get false positives.
      `Test_Containment.cpp` gains
      `FrustumAABB_DeepInsideMatchesLegacy`,
      `FrustumAABB_StraddlingNearPlaneIsNotContained`,
      `FrustumAABB_OutsideMatchesLegacy`,
      `FrustumSphere_DeepInsideMatchesLegacy`,
      `FrustumSphere_StraddlingFarPlaneIsNotContained`, and
      `*BatteryNeverOverReportsContainment` sweeps that pin the
      false-positive direction (the migrated impl never reports
      containment the legacy `Sdf_Plane` oracle rejected).
    - **Slice 3.3.d — GJK termination diagnostics (deferred to GEOM-015).**
      GJK's `GJK_EPSILON = 1e-6f` termination test is a convergence
      guard, not an orientation/incidence decision, so it does not map
      onto the current `RobustPredicates` predicate surface. Tracked as
      a dedicated backlog task at
      [`tasks/backlog/geometry/GEOM-015-gjk-termination-diagnostics.md`](../backlog/geometry/GEOM-015-gjk-termination-diagnostics.md)
      so the GEOM-007 foundation can close without owning a downstream
      algorithm-policy rewrite. Do not re-fold this into GEOM-007.
- **Slice 4 — exact / adaptive escalation (optional).** Decide whether to add
  Shewchuk-style adaptive predicates behind the same surface, or keep the
  filtered-only policy and document caller fallback strategies.

## Required changes
- [x] Slice 1: Define robust predicate APIs for orientation in 2D/3D, signed distance/classification, barycentric classification for in-plane triangle queries, and scale-aware comparisons. (`Geometry.RobustPredicates`)
- [x] Slice 2: Define intersection classification records for segment-segment, segment-triangle, ray-triangle, triangle-triangle, and point/edge/face incidence cases. (`Geometry.IntersectionClassification`)
- [x] Slice 1: Add diagnostic enums that distinguish no intersection, proper intersection, touching, overlap, coplanar, degenerate input, and numerically uncertain cases (predicate side: `Sign`, `Certainty`, `BarycentricRegion`).
- [x] Slice 1: First implementation is filtered double-precision evaluated from `glm::vec*<float>` inputs; exact/adaptive escalation is Slice 4.
- [x] Slice 1: Add conversion helpers that interoperate with existing `glm::vec*` primitives without changing public storage types.
- [x] Slice 1: Document limitations and follow-up tasks for exact kernels, arrangements, and robust booleans (architecture doc + module header comment).
- [x] Slice 1: Update `src/geometry/CMakeLists.txt` and the generated module inventory; the umbrella `Geometry.cppm` re-export is intentionally **not** added per the api-style policy on advanced narrow modules.

## Tests
- [x] Slice 1: Add `tests/unit/geometry/Test.RobustPredicates.cpp` using the `Test.<Name>.cpp` naming style.
- [x] Slice 1: Cover ordinary, degenerate, near-degenerate, coplanar, collinear, duplicate, and large/small scale cases.
- [x] Slice 2: Add unit tests `tests/unit/geometry/Test.IntersectionClassification.cpp` covering default construction, enum value stability, and small helpers on the result records.
- [ ] Slice 3: Add regression-style cases for triangle/segment classification and barycentric boundary classification at callsite-adoption time.
  - [x] Slice 3.1: `Test.RaycastClassify.cpp` covers parity with `RayTriangle_Watertight`, miss / tMin / tMax, degenerate triangle, invalid ray, vertex / edge / interior boundary classification, ray-origin vs ray-interior, and batched cross-check.
  - [x] Slice 3.2: Existing runtime selection contract / integration tests (`IntrinsicRuntimeSelectionContractTests`, `Test_RuntimeSelection`, `Test_ElementSelection`) cover the migrated callsites and continue to pass; the Slice 3.1 parity suite remains the numerical guardrail.
- [x] Slice 1: Compare deterministic classification outputs against documented expectations.

## Docs
- [x] Slice 1: Update `docs/architecture/geometry.md` with the new "Robust predicates" section describing precision and degeneracy semantics; refresh the api-style policy bullet to point at the new module.
- [x] Slice 1: Document known numerical limitations (filtered double-precision; near-zero band reported as `Uncertain`) and when callers must treat results as uncertain.
- [x] Slice 1: Update `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [ ] New robust-operation algorithms have one predicate/classification module to depend on.
- [ ] Degenerate and uncertain cases are surfaced explicitly in result records.
- [ ] The implementation preserves `geometry -> core` layering.
- [ ] Focused tests and structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'RobustPredicates|GeometryQueries|Raycast|Overlap|Boolean' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not rewrite mesh booleans in this foundational task.
- Do not add graphics/runtime/ECS/assets/platform/app dependencies.
- Do not hide degenerate cases behind unchecked assertions.

