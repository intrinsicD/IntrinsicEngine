# GEOIO-001 — Geometry I/O module parity

## Goal
- Promote geometry-owned mesh, point cloud, and graph I/O APIs under `src/geometry` with minimal working loaders and tests.

## Non-goals
- Implement scene/model ingestion or asset/runtime wiring.
- Add GPU upload paths or graphics dependencies.
- Replace the legacy graphics importer registry in this task.

## Context
- `src/geometry/Geometry.MeshIO.cppm` exists as a declaration-only module and is not yet registered in the geometry CMake target.
- Point cloud and graph I/O should be geometry-owned APIs that depend only on `geometry` and `core` error types.
- Legacy graphics importers contain parsing behavior, but new geometry modules must not depend on `graphics`, `assets`, `runtime`, or live legacy importer registries.

## Required changes
- Register and implement `Geometry.MeshIO`.
- Add `Geometry.PointCloudIO` with text point cloud loaders.
- Add `Geometry.GraphIO` with text graph loaders.
- Re-export the new I/O modules from the aggregate `Geometry` module.

## Tests
- Add focused unit coverage for mesh, point cloud, and graph I/O under `tests/unit/geometry`.

## Docs
- Refresh generated module inventory after adding module surfaces.

## Acceptance criteria
- Geometry I/O modules build as part of `IntrinsicGeometry`.
- New loaders return deterministic geometry/property containers and `Core::Expected` failures.
- No new dependency edge from `geometry` to graphics/assets/runtime.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci -R 'GeometryIO' --output-on-failure
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Completion
- Completed: 2026-05-03.
- Commit reference: pending local commit.
- Verified:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicGeometryTests -j 4`
  - `ctest --test-dir build/ci -R 'GeometryIO' --output-on-failure`
  - `ctest --test-dir build/ci -R 'Geometry|PointCloud|Graph|Mesh' --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  - `python3 tools/repo/check_layering.py --root src --strict`
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
- Notes:
  - Geometry-owned mesh, point cloud, and graph I/O modules are implemented, registered, re-exported, tested, and included in the generated module inventory.
  - Follow-up negative-path coverage beyond current `Core::Expected` failure behavior can be tracked separately if desired.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

