# RUNTIME-096 — Runtime module implementation splits

## Goal
- Move worthy non-trivial, non-template implementations out of promoted runtime `.cppm` interfaces into matching `.cpp` implementation units, and clean up includes/imports that become implementation-only.

## Non-goals
- No runtime behavior changes.
- No engine frame-order, camera-controller, gizmo, extraction, or geometry-packer semantic changes.
- No lower-layer dependency changes and no new ownership pushed into graphics/ECS/platform/assets/physics.
- No broad core, geometry, graphics, platform, ECS, physics, or legacy cleanup in this task.
- No attempt to move templates or bodies that must remain visible to importers.

## Context
- Status: backlog.
- Owning subsystem/layer: `runtime` (composition root; may depend on lower layers, but lower layers must not depend on runtime).
- Static audit on 2026-06-06 identified these promoted runtime cleanup targets:
  `src/runtime/Cameras/Runtime.CameraControllers.cppm`,
  `src/runtime/Gizmos/Runtime.GizmoInteraction.cppm`,
  `src/runtime/Runtime.Engine.cppm`,
  `src/runtime/Runtime.GraphGeometryPacker.cppm`,
  `src/runtime/Runtime.MeshGeometryPacker.cppm`,
  `src/runtime/Runtime.MeshPrimitiveViewPacker.cppm`,
  `src/runtime/Runtime.PointCloudGeometryPacker.cppm`, and
  `src/runtime/Runtime.ProceduralGeometryPacker.cppm`.
- `Runtime.CameraControllers.cppm` is the largest target and should be treated as its own slice; it contains concrete controller update/view bodies that do not belong in the public module interface.

## Required changes
- [ ] Slice A: move audited bodies for runtime modules that already have matching `.cpp` implementation units.
- [ ] Slice B: split `Runtime.CameraControllers` into declarations plus an implementation unit, keeping public controller types visible and moving controller update/view math bodies to `.cpp`.
- [ ] Slice C: clean up packer modules and `Runtime.Engine` helper bodies where importer visibility is not required.
- [ ] Register any newly added `.cpp` files as private sources in the relevant `src/runtime/**/CMakeLists.txt` files.
- [ ] Keep public runtime records, interfaces, simple observers, and templates in `.cppm` when importer visibility is required.
- [ ] Remove `inline` from moved non-template exported declarations where no longer required.
- [ ] Clean up global-module-fragment includes in touched `.cppm` files, moving implementation-only headers into matching `.cpp` files.
- [ ] Audit imports in touched `.cppm` files and keep only public-surface imports in interfaces.

## Tests
- [ ] Existing runtime contract tests remain green.
- [ ] Existing camera-controller, gizmo, engine lifecycle, and geometry-packer tests remain green if touched.
- [ ] Existing graphics/runtime extraction tests remain green if packer module imports change.
- [ ] Add no new behavior tests unless the split exposes an untested contract seam or a bug.

## Docs
- [ ] Update runtime architecture docs only if public behavior, ownership, or frame-order wording changes.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module imports/surfaces change.
- [ ] Update this task with per-slice completion notes before retirement.

## Acceptance criteria
- [ ] Touched runtime `.cppm` files expose declarations, public value types, interfaces, templates, and small inline accessors only, or record justified retained exceptions.
- [ ] Any new `.cpp` implementation units are registered in CMake and compile under the `ci` preset.
- [ ] Touched `.cppm` files no longer include/import implementation-only dependencies.
- [ ] Runtime tests and the default CPU gate pass without changed expectations.
- [ ] The change preserves runtime ownership as the composition root and does not push runtime dependencies into lower layers.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Runtime|CameraController|Gizmo|Engine|GraphGeometryPacker|MeshGeometryPacker|MeshPrimitiveView|PointCloudGeometryPacker|ProceduralGeometryPacker' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not change runtime frame order, camera-controller math, gizmo behavior, engine lifecycle, geometry-packer output, or extraction semantics.
- Do not introduce runtime dependencies into lower layers.
- Do not move templates or importer-required definitions into `.cpp`.
- Do not mix this mechanical implementation split with runtime behavior refactors.
