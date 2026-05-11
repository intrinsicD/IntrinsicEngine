# GEOIO-002Z — Geometry-owned mesh PLY vertex texcoord import parity

## Goal
- Extend `Geometry::MeshIO::LoadPLY` (ASCII and binary `binary_little_endian` / `binary_big_endian` paths) to populate a `v:texcoord` `glm::vec2` vertex property when the PLY vertex element carries paired texture-coordinate properties (`s/t`, `u/v`, `texture_u/texture_v`, `texcoord_u/texcoord_v`, or `u0/v0`), so legacy mesh PLY readers can retire while preserving texture-coordinate data on the geometry-owned import path.

## Non-goals
- No new public module surface or exported API names.
- No mesh PLY exporter changes; the writer continues to emit positions, optional `nx ny nz` normals, and optional `red green blue` colors only.
- No per-face texcoord (`texcoord_indices` list) expansion; per-corner UVs remain out of scope.
- No alpha / W component for texture coordinates.
- No point-cloud, OBJ, OFF, STL, graph, assets/runtime/graphics, or GPU behavior changes.
- No mechanical legacy deletion.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-8m8X9`.
- Parent backlog task: `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002W` added mesh PLY vertex normal/color **import** (ASCII and binary readers populate `v:normal` and `v:color`); `GEOIO-002Y` added mesh PLY vertex color **export**. Mesh PLY texture coordinates are not yet imported, so PLY-sourced meshes with per-vertex UVs lose them at the geometry boundary.
- The legacy mesh PLY importer (`src/legacy/Graphics/Importers/Graphics.Importers.PLY.cpp`) recognizes the texcoord alias set `s|u|texture_u|texcoord_u|u0` and `t|v|texture_v|texcoord_v|v0`; this slice mirrors the same alias set for geometry-owned PLY.
- The OBJ importer (`Geometry::MeshIO::LoadOBJ`) already attaches a `v:texcoord` `glm::vec2` property when `vt` lockstep matches the vertex count (`GEOIO-002T`), and the OBJ writer (`Geometry::MeshIO::WriteOBJ`) round-trips that property (`GEOIO-002U`). Adding PLY texcoord import lets mesh data flow between OBJ and PLY without losing UVs through the geometry seam.

## Required changes
- [x] `src/geometry/Geometry.HalfedgeMesh.IO.cpp`:
  - [x] In `ParseAsciiPLY`, after the existing `nx/ny/nz` and `red/green/blue/alpha` property lookups, add a paired texcoord lookup that picks the first matching U-alias from `{s, u, texture_u, texcoord_u, u0}` and the first matching V-alias from `{t, v, texture_v, texcoord_v, v0}`. Treat texcoords as present only when both indices are valid. Parse the two tokens as `float` for each vertex; reject malformed values with `InvalidMeshFormat()`. After the vertex/face loop, attach a `v:texcoord` `glm::vec2` vertex property when the parsed texcoord count matches the position count.
  - [x] In `ParseBinaryPLY`, mirror the same alias detection for `Float32` properties only (consistent with the existing `nx/ny/nz` Float32 gate). Track separate `texUIndex` / `texVIndex` int32s defaulting to `-1`. After per-vertex reads, attach the same `v:texcoord` `glm::vec2` vertex property when both indices were found.
  - [x] Do not introduce a new public helper; keep the alias list as local `static constexpr` arrays inside the parser scope.
- [x] `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] Add `GeometryIO_MeshIO.LoadsASCIIPLYTriangleWithVertexTexcoords` covering ASCII PLY with `property float s` / `property float t` between positions and the face element; expect `v:texcoord` populated with the written values.
  - [x] Add `GeometryIO_MeshIO.LoadsASCIIPLYTriangleWithTextureUVAliases` covering the `texture_u` / `texture_v` alias pair on ASCII PLY.
  - [x] Add `GeometryIO_MeshIO.LoadsBinaryPLYTriangleWithVertexTexcoords` covering binary little-endian PLY with `s` / `t` Float32 properties immediately after `x y z`; build the binary body inline (matching the existing binary PLY load test fixture style) and assert `v:texcoord` round-trips with bit-exact float values.
  - [x] Add a negative test `GeometryIO_MeshIO.LoadASCIIPLYIgnoresPartialTexcoordAxis` that declares only `property float s` (no `t` partner) and confirms `v:texcoord` is **not** attached and that positions/faces still load.

## Tests
- [x] New unit tests in `tests/unit/geometry/Test.GeometryIO.cpp` listed above.
- [x] Existing PLY mesh tests (`LoadsASCIIPLYTriangle`, `LoadsASCIIPLYTriangleWithVertexNormalsAndColors`, `WritesPLYBinaryTriangle*`) must continue to pass unchanged; the import addition is gated on both U and V alias presence, so PLY files without texcoord properties produce byte-identical `v:texcoord` absence.

## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` to record mesh PLY vertex texcoord import parity under `GEOIO-002Z` in the mesh import row.
- [x] No `docs/api/generated/module_inventory.md` refresh required because no public module declarations or exported names change.

## Acceptance criteria
- [x] `Geometry::MeshIO::LoadPLY` attaches a `v:texcoord` `glm::vec2` vertex property when the vertex element carries any of the supported U/V alias pairs and the parsed count matches the position count; otherwise `v:texcoord` is absent.
- [x] Partial texcoord declarations (only U or only V) are silently ignored and do not regress `v:color` / `v:normal` behavior or fail the load.
- [x] Binary PLY import accepts `Float32` texcoord properties only; non-Float32 texcoord declarations leave `v:texcoord` absent rather than erroring out (matching the existing normal-property gate).
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
- Adding per-corner PLY attribute expansion (`texcoord_indices` face lists), texture/material import, or asset routing.
- Changing point-cloud or graph PLY behavior.
- Mixing mechanical legacy deletion with semantic IO implementation.

## Completion
- Completed: 2026-05-11.
- Status: done.
- Implementation commit: `681f378` (`GEOIO-002Z: import mesh PLY vertex texcoords`).
- Verified in this session:
  - `cmake --preset ci` — failed before configure because this host does not have the preset-required `clang-20` / `clang++-20` on `PATH`. Therefore `cmake --build --preset ci --target IntrinsicTests` and the `build/ci` CTest gate could not run locally in this session and remain pending CI verification on a clang-20+ host.
  - `python3 tools/repo/check_layering.py --root src --strict` — 718 files scanned; no layering violations found.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — 0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — re-run after this completion section was added; expected to pass.
  - `python3 tools/docs/check_doc_links.py --root .` — 255 relative links checked; no broken relative links found.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` — 421 modules; inventory unchanged (no public module surface changes).
- Verification still required on a CI host with the repository default C++23 toolchain (clang-20+):
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- Notes:
  - Texcoord detection is gated on both U and V alias indices being present in the vertex element; PLY files that declare only one half (or neither) load with the same vertex-property surface as before this slice (no `v:texcoord` attached), so existing `LoadsASCIIPLYTriangle`, `LoadsASCIIPLYTriangleWithVertexNormalsAndColors`, and binary PLY tests continue to exercise the unchanged path.
  - Binary PLY texcoord import requires `Float32` for the matched property pair; non-Float32 texcoord declarations are intentionally left absent rather than failing the load, mirroring the existing normal/color binary property gates.
  - No mesh PLY exporter changes; round-trip through `WritePLY` / `WritePLYBinary` drops `v:texcoord`. A follow-up slice will own the exporter side.
