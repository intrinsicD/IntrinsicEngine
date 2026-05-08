# GEOIO-002A — Geometry-owned OBJ mesh exporter

## Goal
- Add a geometry-owned ASCII OBJ exporter API to `Geometry.MeshIO` that
  serializes a `MeshIOResult` (positions, optional vertex normals, polygon
  face lists) without introducing assets/runtime/graphics dependencies, so
  the broader `GEOIO-002` parity work can layer PLY/STL exporters and
  asset/runtime routing on the same scaffolding.

## Non-goals
- No PLY or STL exporter in this slice (separate slices under `GEOIO-002`).
- No texture coordinate, material, group, or smoothing-group output.
- No legacy `Graphics::OBJExporter`/`IAssetExporter` registry deletion or
  rewiring; that retirement remains tracked under `GRAPHICS-019` follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or GPU
  upload work.
- No new format-detection metadata helpers.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: in-progress.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/agentic-workflow-session-eaW9t`.
- Parent backlog task: `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-001` landed minimal `LoadOBJ`/`LoadOFF`/`LoadPLY`/`LoadSTL`
  importers in `src/geometry/Geometry.MeshIO.{cppm,cpp}`. The exporter side
  is currently empty; legacy reference implementations live under
  `src/legacy/Graphics/Exporters/Graphics.Exporters.OBJ.{cppm,cpp}` and may
  be consulted as behavioural references only (do not import).
- `MeshIOResult` stores positions in the `v:point` `glm::vec3` vertex
  property, optional normals in `v:normal`, and per-face polygon vertex
  index lists in the `f:vertices` face property (matches the importer
  side).
- Container build environment is missing `clang-20` and `libxrandr` dev
  headers, so the standard `cmake --preset ci` configure currently fails
  at GLFW dependency discovery (see `tasks/backlog/bugs/index.md`). The
  geometry CPU library does not depend on either, but full configure may
  not complete in the agent container; report build evidence honestly.

## Required changes
- Extend `src/geometry/Geometry.MeshIO.cppm`:
  - Add a result enum `MeshIOWriteStatus` with at least:
    `Success`, `EmptyMesh`, `InvalidFace`, `FileWriteError`,
    `InvalidPath`.
  - Add a function declaration
    `MeshIOWriteStatus WriteOBJ(std::string_view absolute_path,
                                 const MeshIOResult& mesh);`
  - Keep all existing `Load*` declarations bit-for-bit.
- Implement `WriteOBJ` in `src/geometry/Geometry.MeshIO.cpp`:
  - Reject empty meshes (no vertex `v:point` property or zero vertices, or
    no `f:vertices` face property, or zero faces) with `EmptyMesh`.
  - Reject any face with fewer than three indices or any index that is out
    of range with `InvalidFace`. No mutation of input.
  - Write `# Exported by IntrinsicEngine` header comment.
  - Write each vertex as `v <x> <y> <z>` using `%.6f`.
  - If a `v:normal` `glm::vec3` property exists with the same vertex
    count, write `vn <x> <y> <z>` lines and emit faces using `f a//na b//nb
    c//nc` form (1-based indices, normal index equal to vertex index).
  - Otherwise emit faces using `f a b c ...` form (1-based indices).
  - Stream output through `std::ofstream` opened in binary mode; on
    `!stream` (open failure) return `InvalidPath`; on post-write
    `!stream.good()` return `FileWriteError`.
- Do not change importer behavior, do not introduce new module imports,
  and do not touch any file outside `src/geometry/`, `tests/unit/geometry/`,
  `tasks/`, and the generated module inventory.

## Tests
- Add focused `unit;geometry` coverage to
  `tests/unit/geometry/Test.GeometryIO.cpp`:
  - `WritesOBJTriangle`: write a synthetic triangle `MeshIOResult`,
    re-import via `LoadOBJ`, verify topology and vertex equivalence.
  - `WritesOBJTriangleWithNormals`: same as above but with vertex normals
    populated; verify the re-imported mesh keeps the triangle topology
    (the OBJ importer currently records only `v:point`/`f:vertices`, so the
    test asserts on positions and face indices, plus presence of `vn`
    lines via filesystem read).
  - `WritesOBJQuad`: a single quad face, verify round-trip face arity.
  - `WriteOBJRejectsEmptyMesh`: empty `MeshIOResult` returns `EmptyMesh`.
  - `WriteOBJRejectsOutOfRangeIndex`: a face referencing an out-of-range
    vertex returns `InvalidFace`.
  - `WriteOBJRejectsBadPath`: a path under a non-existent directory
    returns `InvalidPath`.

## Docs
- Update `docs/api/generated/module_inventory.md` only if module surfaces
  changed in a way the generator picks up
  (`python3 tools/repo/generate_module_inventory.py --root src --out
  docs/api/generated/module_inventory.md`).
- No additional architecture/migration doc edits required for this slice;
  parity-matrix updates remain part of the parent `GEOIO-002` task once
  PLY/STL/exporter parity lands.

## Acceptance criteria
- `Geometry::MeshIO::WriteOBJ` exists, is callable from `unit;geometry`
  tests, and round-trips a triangle and a quad through `LoadOBJ` without
  topology loss for the supported attributes.
- Exporter rejects empty meshes, out-of-range face indices, and
  non-writable paths with the documented `MeshIOWriteStatus` values.
- `src/geometry/*` imports remain layered (`geometry -> core` only); no
  new asset/runtime/graphics imports introduced.
- Existing `LoadOBJ`/`LoadOFF`/`LoadPLY`/`LoadSTL` and `PointCloudIO` /
  `GraphIO` tests continue to pass.

## Verification
```bash
# Focused gate (full configure may fail in containers missing libxrandr / clang-20):
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# Layering and task-policy gates (do not require build):
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Adding `assets`/`runtime`/`graphics`/`RHI` imports to `src/geometry/*`.
- Mixing this exporter with mechanical legacy exporter deletion.
- Adding PLY/STL exporters in this slice.
- Touching `src/legacy/Graphics/Exporters/*` other than reading them as
  behavioral reference.
- Auto-acknowledging or mutating any runtime/render extraction state
  (unrelated to this slice).
- Introducing GPU/Vulkan-only verification requirements.
