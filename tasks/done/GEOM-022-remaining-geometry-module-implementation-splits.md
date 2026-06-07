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
- Status: done (retired 2026-06-07; implementation split landed in `bfcd2751`).
- Owner/agent: Codex.
- Branch / PR: current branch / TBD.
- Next verification step: none; retired after a clean no-cache rebuild, explicit benchmark-smoke target build, and default CPU gate on 2026-06-07.
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
- [x] Slice A: move audited bodies for targets that already have matching `.cpp` implementation units.
- [x] Slice B: add missing implementation units for compact analytic primitive modules where moved bodies are non-trivial enough to justify `.cpp` files.
- [x] Slice C: split larger numeric/diagnostic modules such as `Geometry.Quadric`, `Geometry.RobustPredicates`, `Geometry.Grid`, `Geometry.Overlap`, and `Geometry.Support` into declarations plus `.cpp` bodies.
- [x] Register any newly added `.cpp` files as private sources in `src/geometry/CMakeLists.txt`.
- [x] Keep templates, compile-time traits, and importer-required constexpr definitions in `.cppm`; record retained exceptions in this task before retirement.
- [x] Remove `inline` from moved non-template exported declarations where no longer required.
- [x] Clean up global-module-fragment includes in touched `.cppm` files, moving implementation-only headers into matching `.cpp` files.
- [x] Audit imports in touched `.cppm` files and keep only public-surface imports in the interfaces.

## Tests
- [x] Existing geometry unit tests remain green for all touched modules.
- [x] Existing contact/overlap/support/robust-predicate tests remain green if the analytic modules are touched.
- [x] Existing MeshSoup/conversion tests remain green if module-inventory or umbrella imports change.
- [x] Add no new behavior tests unless the split exposes an untested contract seam or a bug.

## Docs
- [x] Update geometry architecture docs only if a public contract or module-surface wording changes.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module imports/surfaces change.
- [x] Update this task with per-slice completion notes before retirement.

## Acceptance criteria
- [x] Touched geometry `.cppm` files expose declarations, value types, templates, constexpr importer-required helpers, and small inline accessors only, or record justified retained exceptions.
- [x] Any new `.cpp` implementation units are registered in CMake and compile under the `ci` preset.
- [x] Touched `.cppm` files no longer include/import implementation-only dependencies.
- [x] Focused geometry tests and the default CPU gate pass without changed expectations.
- [x] The change preserves `geometry -> core` layering.

## Progress notes
- 2026-06-07: Current implementation-split slice completed locally: moved non-trivial non-template bodies into matching `.cpp` implementation units, registered new private sources in CMake, and retained importer-visible templates, constexpr helpers, ABI structs, and small accessors in module interfaces where required.
- Focused target builds passed for the touched subsystem, and `docs/api/generated/module_inventory.md` was regenerated with `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- 2026-06-07 retirement verification: the previous rendergraph ASan blocker was reproduced as stale C++23 module layout state from ccache/incremental module artifacts, not a source defect in the implementation split. A `CCACHE_DISABLE=1` `IntrinsicTests` rebuild plus an explicit `IntrinsicBenchmarkSmoke` build restored a clean default CPU gate; final `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed 2816/2816.

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
