# GEOIO-002AB — Geometry binary PLY double-scalar import hardening

## Goal
- Harden geometry-owned binary PLY import so mesh and point-cloud vertex positions, normals, and mesh texcoords accept `double`/`float64` scalar fields in addition to `float`/`float32`.

## Non-goals
- No new asset/runtime/graphics import routing.
- No GLTF/GLB ingest or scene/model ownership.
- No mechanical legacy cleanup.

## Context
- Owner: `geometry -> core` only.
- Parent task: `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
- Existing binary PLY import already parses `float32` vertex fields and rejects unsupported topology/header failures with `Core::ErrorCode::InvalidFormat`.

## Required changes
- [x] Update `Geometry.HalfedgeMesh.IO` binary PLY mesh import to read `float32` or `float64` scalar vertex positions, normals, and texcoords.
- [x] Update `Geometry.PointCloud.IO` binary PLY point-cloud import to read `float32` or `float64` scalar vertex positions and normals.
- [x] Preserve PLY color and face-list behavior.

## Tests
- [x] Add `unit;geometry` coverage for binary PLY mesh import with double positions and texcoords.
- [x] Add `unit;geometry` coverage for binary PLY point-cloud import with double positions and normals.

## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the completed hardening slice.
- [x] Do not regenerate module inventory because no module surface is added, removed, or renamed.

## Acceptance criteria
- [x] Binary PLY import accepts common `double` vertex scalar declarations without changing public APIs.
- [x] Malformed or unsupported binary PLY topology still returns deterministic invalid-format failures.
- [x] `src/geometry/*` dependency boundaries remain unchanged.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO_.*Binary.*PLY.*Double|GeometryIO_MeshIO.LoadsBinaryPLYTriangleWithDoubleScalars|GeometryIO_PointCloudIO.LoadsBinaryPLYPointCloudWithDoubleScalars' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Completed verification:
- `python3 tools/repo/check_layering.py --root src --strict` — passed.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed in warning mode with no broken relative links.
- `cmake --build cmake-build-debug --target IntrinsicGeometryTests` — passed as supplemental verification using `clang-22`/`clang++-22` and `clang-scan-deps-22`.
- `ctest --test-dir cmake-build-debug --output-on-failure -R 'GeometryIO_(MeshIO|PointCloudIO)\.LoadsBinaryPLY.*DoubleScalars' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — passed, 2/2.
- `ctest --test-dir cmake-build-debug --output-on-failure -R 'GeometryIO_' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — passed, 141/141.

Blocked verification:
- `cmake --preset ci` and `cmake --build --preset ci --target IntrinsicGeometryTests` could not complete because this shell cannot find the preset-pinned `clang-20`/`clang++-20` binaries on `PATH` or under `/usr`/`/opt`.

## Forbidden changes
- Adding assets/runtime/graphics/RHI imports to `src/geometry/*`.
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

## Completion
- Completed: 2026-05-13.
- Status: done.
- Implementation commit: this local change (`GEOIO-002AB: accept binary PLY double scalars`).

