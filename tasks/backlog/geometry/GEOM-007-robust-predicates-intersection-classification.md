# GEOM-007 — Robust predicates and intersection classification foundation

## Goal
- Add a reusable robust-predicate and intersection-classification foundation for geometry algorithms that need reliable orientation, incidence, barycentric, and primitive-intersection decisions.

## Non-goals
- No full mesh boolean rewrite.
- No tetrahedralization implementation.
- No arrangement/BSP kernel beyond foundational predicates and classification records.
- No dependency on broad external geometry frameworks such as CGAL/libigl in the core geometry layer.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
- Current geometry code has many collision/query algorithms but lacks a common robust predicate policy for degeneracies, nearly coincident elements, and exact/filtered decisions.
- This task should be completed before expanding booleans, remeshing, tetrahedralization, arrangements, or reconstruction robustness.

## Required changes
- [ ] Define robust predicate APIs for orientation in 2D/3D, incircle/insphere where scoped, signed distance/classification, barycentric classification, and epsilon/scale-aware comparisons.
- [ ] Define intersection classification records for segment-segment, segment-triangle, ray-triangle, triangle-triangle, and point/edge/face incidence cases.
- [ ] Add diagnostic enums that distinguish no intersection, proper intersection, touching, overlap, coplanar, degenerate input, and numerically uncertain cases.
- [ ] Decide whether the first implementation is filtered floating point, adaptive exact predicates, or a staged policy with clear limitations.
- [ ] Add conversion helpers that interoperate with existing `glm::vec*` primitives without changing public storage types.
- [ ] Document limitations and follow-up tasks for exact kernels, arrangements, and robust booleans.
- [ ] Update `src/geometry/CMakeLists.txt`, `Geometry.cppm`, and generated module inventory if a new module surface is added.

## Tests
- [ ] Add `tests/unit/geometry/Test.RobustPredicates.cpp` using the `Test.<Name>.cpp` naming style.
- [ ] Cover ordinary, degenerate, near-degenerate, coplanar, collinear, duplicate, and large/small scale cases.
- [ ] Add regression-style cases for triangle/segment classification and barycentric boundary classification.
- [ ] Compare deterministic classification outputs against documented expectations.

## Docs
- [ ] Update `docs/architecture/geometry.md` or the GEOM-005 policy doc with predicate precision and degeneracy semantics.
- [ ] Document known numerical limitations and when callers must treat results as uncertain.
- [ ] Update `docs/api/generated/module_inventory.md` after module surface changes.

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

