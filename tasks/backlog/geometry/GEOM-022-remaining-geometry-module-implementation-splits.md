# GEOM-022 — Remaining geometry module implementation splits

## Goal
- Move worthy non-trivial, non-template implementations out of remaining promoted geometry `.cppm` interfaces into matching `.cpp` implementation units, and clean up includes/imports that become implementation-only.

## Non-goals
- No geometry algorithm behavior changes.
- No public geometry API, numeric-policy, tolerance-policy, or diagnostics-severity changes.
- No MeshSoup cleanup; that is owned by `GEOM-021`.
- No renderer/runtime/ECS/assets/platform/app dependencies.
- No attempt to move templates or constexpr bodies that must remain visible to importers.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- `AGENTS.md` requires non-trivial algorithm/control-flow bodies, topology/container traversal, diagnostics assembly, and implementation-only imports to live in `.cpp` implementation units when importer visibility is not required.
- Static audit on 2026-06-06 identified these promoted geometry cleanup targets outside `Geometry.MeshSoup`:
  `Geometry.AABB`, `Geometry.BVH`, `Geometry.Capsule`, `Geometry.Circulators`,
  `Geometry.ContactManifold`, `Geometry.Containment`, `Geometry.ConvexHull`,
  `Geometry.Cylinder`, `Geometry.EPA`, `Geometry.Ellipsoid`, `Geometry.Frustum`,
  `Geometry.GJK`, `Geometry.Grid`, `Geometry.IO`,
  `Geometry.IntersectionClassification`, `Geometry.OBB`, `Geometry.Overlap`,
  `Geometry.Plane`, `Geometry.Properties`, `Geometry.Quadric`, `Geometry.Queries`,
  `Geometry.RobustPredicates`, `Geometry.Segment`, `Geometry.Sphere`,
  `Geometry.Support`, `Geometry.Triangle`, and `Geometry.Validation`.
- Prefer moving bodies into existing `.cpp` files first (`Geometry.BVH`,
  `Geometry.EPA`, `Geometry.Properties`, `Geometry.Sphere`, and any other target
  that already has a matching implementation unit) before creating new files.

## Required changes
- [ ] Slice A: move audited bodies for targets that already have matching `.cpp` implementation units.
- [ ] Slice B: add missing implementation units for compact analytic primitive modules where moved bodies are non-trivial enough to justify `.cpp` files.
- [ ] Slice C: split larger numeric/diagnostic modules such as `Geometry.Quadric`, `Geometry.RobustPredicates`, `Geometry.Grid`, `Geometry.Overlap`, and `Geometry.Support` into declarations plus `.cpp` bodies.
- [ ] Register any newly added `.cpp` files as private sources in `src/geometry/CMakeLists.txt`.
- [ ] Keep templates, compile-time traits, and importer-required constexpr definitions in `.cppm`; record retained exceptions in this task before retirement.
- [ ] Remove `inline` from moved non-template exported declarations where no longer required.
- [ ] Clean up global-module-fragment includes in touched `.cppm` files, moving implementation-only headers into matching `.cpp` files.
- [ ] Audit imports in touched `.cppm` files and keep only public-surface imports in the interfaces.

## Tests
- [ ] Existing geometry unit tests remain green for all touched modules.
- [ ] Existing contact/overlap/support/robust-predicate tests remain green if the analytic modules are touched.
- [ ] Existing MeshSoup/conversion tests remain green if module-inventory or umbrella imports change.
- [ ] Add no new behavior tests unless the split exposes an untested contract seam or a bug.

## Docs
- [ ] Update geometry architecture docs only if a public contract or module-surface wording changes.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module imports/surfaces change.
- [ ] Update this task with per-slice completion notes before retirement.

## Acceptance criteria
- [ ] Touched geometry `.cppm` files expose declarations, value types, templates, constexpr importer-required helpers, and small inline accessors only, or record justified retained exceptions.
- [ ] Any new `.cpp` implementation units are registered in CMake and compile under the `ci` preset.
- [ ] Touched `.cppm` files no longer include/import implementation-only dependencies.
- [ ] Focused geometry tests and the default CPU gate pass without changed expectations.
- [ ] The change preserves `geometry -> core` layering.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'AABB|BVH|Capsule|Contact|Containment|Convex|Cylinder|Frustum|GJK|Grid|Intersection|OBB|Overlap|Plane|Properties|Quadric|Robust|Segment|Sphere|Support|Triangle|Validation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not change geometry algorithm outputs, validation/tolerance semantics, or diagnostics policies.
- Do not move templates or importer-required constexpr definitions into `.cpp`.
- Do not introduce dependencies outside the allowed `geometry -> core` boundary.
- Do not bundle `GEOM-021` MeshSoup work into this task.
- Do not claim performance improvements from compile-structure cleanup.
