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
- [x] Slice 1: Define robust predicate APIs for orientation in 2D/3D, signed distance/classification, barycentric classification for in-plane triangle queries, and scale-aware comparisons. (`Geometry.RobustPredicates`)
- [ ] Slice 2: Define intersection classification records for segment-segment, segment-triangle, ray-triangle, triangle-triangle, and point/edge/face incidence cases.
- [x] Slice 1: Add diagnostic enums that distinguish no intersection, proper intersection, touching, overlap, coplanar, degenerate input, and numerically uncertain cases (predicate side: `Sign`, `Certainty`, `BarycentricRegion`).
- [x] Slice 1: First implementation is filtered double-precision evaluated from `glm::vec*<float>` inputs; exact/adaptive escalation is Slice 4.
- [x] Slice 1: Add conversion helpers that interoperate with existing `glm::vec*` primitives without changing public storage types.
- [x] Slice 1: Document limitations and follow-up tasks for exact kernels, arrangements, and robust booleans (architecture doc + module header comment).
- [x] Slice 1: Update `src/geometry/CMakeLists.txt` and the generated module inventory; the umbrella `Geometry.cppm` re-export is intentionally **not** added per the api-style policy on advanced narrow modules.

## Tests
- [x] Slice 1: Add `tests/unit/geometry/Test.RobustPredicates.cpp` using the `Test.<Name>.cpp` naming style.
- [x] Slice 1: Cover ordinary, degenerate, near-degenerate, coplanar, collinear, duplicate, and large/small scale cases.
- [ ] Slice 2/3: Add regression-style cases for triangle/segment classification and barycentric boundary classification at callsite-adoption time.
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

