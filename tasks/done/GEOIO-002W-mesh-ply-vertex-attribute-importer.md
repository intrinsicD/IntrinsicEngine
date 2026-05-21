# GEOIO-002W — Geometry-owned mesh PLY vertex attribute import parity

## Goal
- Extend `Geometry::MeshIO::LoadPLY` to preserve mesh vertex normals and colors from supported ASCII and binary PLY vertex properties.

## Non-goals
- No new public module surface or exported API names.
- No mesh PLY writer changes; existing `WritePLY` and `WritePLYBinary` output remains unchanged.
- No point-cloud, OBJ, OFF, STL, graph, assets/runtime/graphics, or GPU behavior changes.
- No PLY texture coordinate or per-face/per-corner attribute import in this slice.
- No mechanical legacy deletion.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `main` local workspace.
- Parent task: `tasks/done/GEOIO-002-geometry-io-parity-hardening.md`.
- Mesh PLY import already parsed vertex positions and face topology for ASCII and binary PLY, but dropped vertex normal/color properties even when geometry-owned PLY writers emitted normals and point-cloud PLY loaders preserved colors.
- This slice keeps the existing `MeshIOResult` property contract: positions in `v:point`, optional normals in `v:normal`, and optional colors in `v:color`.

## Required changes
- [x] `src/geometry/Geometry.HalfedgeMesh.IO.cpp`:
  - [x] Add mesh PLY color normalization for `red green blue [alpha]` values.
  - [x] Teach the ASCII PLY mesh parser to locate vertex properties by header name rather than assuming `x y z` are the first three tokens only.
  - [x] Populate `v:normal` when `nx ny nz` are present on every vertex row.
  - [x] Populate `v:color` when `red green blue` are present, defaulting alpha to opaque and honoring an optional `alpha` channel.
  - [x] Teach the binary PLY mesh parser to preserve `float32` `nx ny nz` and `uint8` `red green blue [alpha]` properties while continuing to skip unrelated scalar vertex properties.
- [x] `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] Add ASCII mesh PLY coverage for vertex normals and colors.
  - [x] Strengthen ASCII and binary PLY writer-normal round-trip checks.
  - [x] Strengthen the existing binary mesh PLY extra-vertex-property fixture to assert imported vertex colors.

## Tests
- [x] Added `GeometryIO_MeshIO.LoadsASCIIPLYTriangleWithVertexNormalsAndColors`.
- [x] Updated `GeometryIO_MeshIO.WritesPLYTriangleWithNormals` to assert imported `v:normal` data.
- [x] Updated `GeometryIO_MeshIO.WritesPLYBinaryTriangleWithNormals` to assert imported `v:normal` data.
- [x] Updated `GeometryIO_MeshIO.LoadsBinaryLittleEndianPLYWithExtraVertexProperties` to assert imported `v:color` data.

## Docs
- [x] Updated `docs/migration/nonlegacy-parity-matrix.md` to record mesh PLY vertex normal/color import parity under `GEOIO-002W`.
- [x] No `docs/api/generated/module_inventory.md` refresh required because no public module declarations, module files, or exported names changed.

## Acceptance criteria
- [x] ASCII mesh PLY files with `nx ny nz` and `red green blue` vertex properties populate `v:normal` and `v:color`.
- [x] Binary mesh PLY files with `float32` normal properties or `uint8` color properties populate `v:normal` and `v:color`.
- [x] Existing mesh PLY topology, binary endian, and negative-path tests continue to pass.
- [x] `src/geometry/*` imports only allowed lower-layer dependencies and remains independent of assets/runtime/graphics.
- [x] The migration matrix records the new parity evidence.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding assets/runtime/graphics/RHI imports to `src/geometry/*`.
- Changing `Geometry::MeshIO` public signatures or status enums.
- Adding per-corner PLY attribute expansion, texture/material import, or asset routing.
- Changing point-cloud PLY behavior.
- Mixing mechanical legacy deletion with semantic IO implementation.

## Completion
- Completed: 2026-05-11.
- Status: done.
- Implementation commit: `8ba4429` (`GEOIO-002W: import mesh PLY vertex attributes`).
- Verified in this session:
  - `cmake --preset ci` — failed before configure because this host does not have the preset-required `clang-20` / `clang++-20` on `PATH`; therefore `cmake --build --preset ci --target IntrinsicTests` and the `build/ci` CTest gate could not run locally in this session.
  - Supplemental focused evidence: existing non-default `cmake-build-debug` cache uses `/usr/bin/clang++-22`, `/usr/bin/clang-22`, and `/usr/bin/clang-scan-deps-22`.
  - Supplemental focused evidence: `/home/alex/.local/share/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/cmake --build cmake-build-debug --target IntrinsicGeometryTests` succeeded.
  - Supplemental focused evidence: `/home/alex/.local/share/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/ctest --test-dir cmake-build-debug --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed 130/130 focused `GeometryIO` tests.
  - `python3 tools/repo/check_layering.py --root src --strict` — no layering violations found.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — 0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — 152 task files validated; 0 findings.
  - `python3 tools/docs/check_doc_links.py --root .` — 255 relative links checked; no broken relative links found.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` — 420 modules; no diff.
- Verification still required on a CI host with the repository C++23 toolchain:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- Notes:
  - Mesh PLY import still intentionally maps only lockstep vertex attributes that fit the current `MeshIOResult` property model. Per-corner texture coordinates and richer material/element metadata remain outside this slice.



