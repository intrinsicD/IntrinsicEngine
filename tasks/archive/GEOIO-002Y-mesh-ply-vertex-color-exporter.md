# GEOIO-002Y — Geometry-owned mesh PLY vertex color export parity

## Goal
- Extend `Geometry::MeshIO::WritePLY` and `Geometry::MeshIO::WritePLYBinary` to emit `uchar red/green/blue` vertex properties when a `v:color` `glm::vec4` property matches the position count, so mesh PLY writers round-trip color data through the geometry-owned PLY importer added under `GEOIO-002W`.

## Non-goals
- No new public module surface or exported API names.
- No mesh PLY importer changes; existing ASCII and binary PLY readers already preserve `v:color`.
- No alpha-channel emission. Existing point-cloud PLY writers also drop alpha, so the alpha component of `v:color` is intentionally not serialized in this slice.
- No point-cloud, OBJ, OFF, STL, graph, assets/runtime/graphics, or GPU behavior changes.
- No mechanical legacy deletion.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-Qvotd`.
- Parent task: `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002W` added mesh PLY vertex normal/color **import**: ASCII and binary readers populate `v:color` (`glm::vec4`) from `red green blue [alpha]` properties.
- The existing mesh PLY writers emit positions and optional `nx ny nz` normals but ignore `v:color`, so color data round-trips one-way only.
- The point-cloud PLY writer (`Geometry::PointCloudIO::WritePLY` / `WritePLYBinary`) emits `uchar red/green/blue` (no alpha) when colors exist. This slice mirrors that convention for the mesh writers.

## Required changes
- [x] `src/geometry/Geometry.HalfedgeMesh.IO.cpp`:
  - [x] In `WritePLY` (ASCII), detect a `v:color` `glm::vec4` property whose size matches the position count. When present, declare `property uchar red`, `property uchar green`, and `property uchar blue` in the vertex element header (between any normal declarations and the face element). Emit `r g b` integer triples after each vertex's `x y z` (and optional `nx ny nz`) tokens, encoding each channel by clamping to `[0, 1]`, scaling by 255, and rounding to nearest with saturation to 255.
  - [x] In `WritePLYBinary`, declare the same three `uchar` properties in the header and write three single-byte values per vertex after the position bytes (and optional `float32` normals) using the same `[0, 1]`-clamp-scale-round-saturate encoding.
  - [x] Share the encoding helper with the point-cloud writers' convention (local lambda is acceptable; do not export a public helper).
- [x] `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] Add `GeometryIO_MeshIO.WritesPLYTriangleWithVertexColors` covering the ASCII writer: emits `property uchar red/green/blue`, writes `0 255 0`-style tokens, and round-trips `v:color` rgb values through `LoadPLY` (alpha defaults to 1.0 on import).
  - [x] Add `GeometryIO_MeshIO.WritesPLYBinaryTriangleWithVertexColors` covering the binary writer: emits the same header tokens and round-trips `v:color` rgb values via `LoadPLY`.
  - [x] Add coverage for the combined-normals-and-colors case in at least one of the two new tests so the header order (`nx ny nz` before `red green blue`) is locked.

## Tests
- [x] New `GeometryIO_MeshIO.WritesPLYTriangleWithVertexColors` and `GeometryIO_MeshIO.WritesPLYBinaryTriangleWithVertexColors` unit tests under `tests/unit/geometry/Test.GeometryIO.cpp` exercising the ASCII and binary writer color paths plus a normals+colors round-trip.

## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` to record mesh PLY vertex color export parity under `GEOIO-002Y` in the exporter row.
- [x] No `docs/api/generated/module_inventory.md` refresh required because no public module declarations, module files, or exported names change.

## Acceptance criteria
- [x] `WritePLY` and `WritePLYBinary` emit `uchar red/green/blue` vertex properties exactly when `v:color` is present and matches the position count; otherwise output remains byte-identical to the prior writer.
- [x] The new round-trip tests pass: `v:color` rgb values written by the mesh PLY writers reload through `LoadPLY` within standard 8-bit-quantization tolerance (`0.0`, `1.0`, primary colors).
- [x] A mesh with both `v:normal` and `v:color` emits the normal properties before the color properties (matching point-cloud convention).
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
- Adding per-corner PLY attribute expansion, alpha or float color emission, texture/material export, or asset routing.
- Changing point-cloud or graph PLY behavior.
- Mixing mechanical legacy deletion with semantic IO implementation.

## Completion
- Completed: 2026-05-11.
- Status: done.
- Implementation commit: `fbced54` (`GEOIO-002Y: export mesh PLY vertex colors`).
- Verified in this session:
  - `cmake --preset ci` — failed before configure because this host does not have the preset-required `clang-20` / `clang++-20` on `PATH`. A fallback configure with `g++-13` was attempted but also failed because the repository CMake guard requires `clang-scan-deps-20+`. Therefore `cmake --build --preset ci --target IntrinsicTests` and the `build/ci` CTest gate could not run locally in this session and remain pending CI verification on a clang-20+ host.
  - `python3 tools/repo/check_layering.py --root src --strict` — 718 files scanned; no layering violations found.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — 0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — 154 task files validated; 0 findings.
  - `python3 tools/docs/check_doc_links.py --root .` — 255 relative links checked; no broken relative links found.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` — 421 modules; inventory unchanged (no public module surface changes).
- Verification still required on a CI host with the repository default C++23 toolchain (clang-20+):
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- Notes:
  - The new color emission is gated on `v:color` size matching the position count; meshes without `v:color` produce byte-identical PLY output to the prior writers, so existing writer tests (`WritesPLYTriangleWithNormals`, `WritesPLYBinaryTriangleWithNormals`, quad arity, empty-mesh, out-of-range, bad-path) remain unaffected.
  - Alpha is intentionally not serialized; the point-cloud PLY writer convention is preserved. Asset/runtime routing remains responsible for choosing whether to round-trip alpha through alternative formats.
