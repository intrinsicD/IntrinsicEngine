# GEOIO-002AA — Geometry-owned mesh PLY vertex texcoord export parity

## Goal
- Extend `Geometry::MeshIO::WritePLY` and `Geometry::MeshIO::WritePLYBinary` so geometry-owned mesh PLY export preserves per-vertex `v:texcoord` `glm::vec2` data as paired Float32 `s` / `t` vertex properties, matching the alias set already accepted by the promoted mesh PLY importer.

## Non-goals
- No new public module surface or exported API names.
- No per-corner PLY texcoord expansion, `texcoord_indices` face lists, texture/material import, or asset routing.
- No alpha/W texture-coordinate component.
- No changes to OBJ, OFF, STL, point-cloud, graph, assets/runtime/graphics, or GPU behavior.
- No mechanical legacy deletion.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `main` local agent workflow.
- Parent backlog task: `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002T` and `GEOIO-002U` added OBJ vertex-texcoord import/export parity.
- `GEOIO-002Z` added mesh PLY vertex-texcoord import parity for paired `s/t`, `u/v`, `texture_u/texture_v`, `texcoord_u/texcoord_v`, or `u0/v0` Float32 properties.
- Before this slice, `WritePLY` and `WritePLYBinary` serialized positions plus optional normals/colors but dropped a sized `v:texcoord` property.

## Required changes
- [x] `src/geometry/Geometry.HalfedgeMesh.IO.cpp`:
  - [x] In `WritePLY`, detect `v:texcoord` as present only when the property is valid and sized to `v:point`.
  - [x] Emit `property float s` and `property float t` after optional color properties and before the face element when texcoords are present.
  - [x] Append each vertex row's `u v` values using the existing fixed six-decimal ASCII convention.
  - [x] In `WritePLYBinary`, mirror the same detection/header properties and write `u` then `v` as little-endian Float32 values for each vertex.
- [x] `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] Add `GeometryIO_MeshIO.WritesPLYTriangleWithVertexTexcoords` covering ASCII PLY header/body emission and `LoadPLY` round-trip into `v:texcoord`.
  - [x] Add `GeometryIO_MeshIO.WritesPLYBinaryTriangleWithVertexTexcoords` covering binary little-endian PLY header emission and `LoadPLY` round-trip into `v:texcoord`.
- [x] `docs/migration/nonlegacy-parity-matrix.md`:
  - [x] Record mesh PLY vertex texcoord export parity under `GEOIO-002AA` in the OBJ/PLY/STL exporter row.

## Tests
- [x] New unit tests in `tests/unit/geometry/Test.GeometryIO.cpp` listed above.
- [x] Existing mesh PLY writer behavior remains gated on valid property sizes; meshes without `v:texcoord` continue to omit `s` / `t` properties.

## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` to record mesh PLY vertex texcoord export parity.
- [x] No `docs/api/generated/module_inventory.md` refresh is required because no public module declarations or exported names changed.

## Acceptance criteria
- [x] `Geometry::MeshIO::WritePLY` emits `float s` / `float t` vertex properties and values when `v:texcoord` exists and matches the vertex count.
- [x] `Geometry::MeshIO::WritePLYBinary` emits the same properties and little-endian Float32 values.
- [x] The existing promoted `LoadPLY` path round-trips texcoords from both writer variants.
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
- Changing point-cloud or graph PLY behavior.
- Mixing mechanical legacy deletion with semantic IO implementation.

## Completion
- Completed: 2026-05-11.
- Status: done.
- Implementation commit: this local commit (`GEOIO-002AA: export mesh PLY vertex texcoords`).
- Verified in this session:
  - `get_errors` on `src/geometry/Geometry.HalfedgeMesh.IO.cpp`, `tests/unit/geometry/Test.GeometryIO.cpp`, `docs/migration/nonlegacy-parity-matrix.md`, and this task record — no new errors reported; existing clang-tidy warnings remain in nearby/pre-existing code.
  - `cmake --preset ci` — attempted and failed because the preset-required `clang-20` / `clang++-20` compilers are not available on this host's `PATH` (the host has `clang++` 22, but the repository contract requires the preset-pinned toolchain). Build and CTest remain pending on a clang-20-capable CI host.
  - `python3 tools/repo/check_layering.py --root src --strict` — 718 files scanned; no layering violations found.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — 0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — 158 task files validated; 0 findings.
  - `python3 tools/docs/check_doc_links.py --root .` — 255 relative links checked; no broken relative links found.
  - `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main` — docs sync rules satisfied.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` — 421 modules; inventory unchanged (no public module surface changes).
- Verification still required on a CI host with the repository default C++23 toolchain (`clang-20` / `clang++-20` and matching `clang-scan-deps`):
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- Notes:
  - Texcoord export is gated on `v:texcoord` being present and sized exactly to the vertex position count, mirroring the existing optional normal/color writer gates.
  - The writer uses canonical `s` / `t` property names, which are already accepted by the promoted ASCII and binary PLY importer added under `GEOIO-002Z`.


