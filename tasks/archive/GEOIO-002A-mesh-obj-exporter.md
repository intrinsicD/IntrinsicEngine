# GEOIO-002A — Geometry-owned OBJ mesh exporter

## Goal
- Add a geometry-owned ASCII OBJ exporter API to `Geometry.HalfedgeMesh.IO` that
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
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/agentic-workflow-session-eaW9t` (implementation),
  retired on `claude/setup-agentic-workflow-PjgiX`.
- Parent task: `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-001` landed minimal `LoadOBJ`/`LoadOFF`/`LoadPLY`/`LoadSTL`
  importers in `src/geometry/Geometry.HalfedgeMesh.IO.{cppm,cpp}`. The exporter side
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
- [x] Extend `src/geometry/Geometry.HalfedgeMesh.IO.cppm`:
  - [x] Add a result enum `MeshIOWriteStatus` with at least:
    `Success`, `EmptyMesh`, `InvalidFace`, `FileWriteError`,
    `InvalidPath`.
  - [x] Add a function declaration
    `MeshIOWriteStatus WriteOBJ(std::string_view absolute_path,
                                 const MeshIOResult& mesh);`
  - [x] Keep all existing `Load*` declarations bit-for-bit.
- [x] Implement `WriteOBJ` in `src/geometry/Geometry.HalfedgeMesh.IO.cpp`:
  - [x] Reject empty meshes (no vertex `v:point` property or zero vertices, or
    no `f:vertices` face property, or zero faces) with `EmptyMesh`.
  - [x] Reject any face with fewer than three indices or any index that is out
    of range with `InvalidFace`. No mutation of input.
  - [x] Write `# Exported by IntrinsicEngine` header comment.
  - [x] Write each vertex as `v <x> <y> <z>` using `%.6f`.
  - [x] If a `v:normal` `glm::vec3` property exists with the same vertex
    count, write `vn <x> <y> <z>` lines and emit faces using `f a//na b//nb
    c//nc` form (1-based indices, normal index equal to vertex index).
  - [x] Otherwise emit faces using `f a b c ...` form (1-based indices).
  - [x] Stream output through `std::ofstream` opened in binary mode; on
    `!stream` (open failure) return `InvalidPath`; on post-write
    `!stream.good()` return `FileWriteError`.
- [x] Do not change importer behavior, do not introduce new module imports,
  and do not touch any file outside `src/geometry/`, `tests/unit/geometry/`,
  `tasks/`, and the generated module inventory.

## Tests
- [x] Add focused `unit;geometry` coverage to
  `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] `WritesOBJTriangle`: write a synthetic triangle `MeshIOResult`,
    re-import via `LoadOBJ`, verify topology and vertex equivalence.
  - [x] `WritesOBJTriangleWithNormals`: same as above but with vertex normals
    populated; verify the re-imported mesh keeps the triangle topology
    (the OBJ importer currently records only `v:point`/`f:vertices`, so the
    test asserts on positions and face indices, plus presence of `vn`
    lines via filesystem read).
  - [x] `WritesOBJQuad`: a single quad face, verify round-trip face arity.
  - [x] `WriteOBJRejectsEmptyMesh`: empty `MeshIOResult` returns `EmptyMesh`.
  - [x] `WriteOBJRejectsOutOfRangeIndex`: a face referencing an out-of-range
    vertex returns `InvalidFace`.
  - [x] `WriteOBJRejectsBadPath`: a path under a non-existent directory
    returns `InvalidPath`.

## Docs
- [x] Update `docs/api/generated/module_inventory.md` only if module surfaces
  changed in a way the generator picks up
  (`python3 tools/repo/generate_module_inventory.py --root src --out
  docs/api/generated/module_inventory.md`).
- [x] No additional architecture/migration doc edits required for this slice;
  parity-matrix updates remain part of the parent `GEOIO-002` task once
  PLY/STL/exporter parity lands.

## Acceptance criteria
- [x] `Geometry::MeshIO::WriteOBJ` exists, is callable from `unit;geometry`
  tests, and round-trips a triangle and a quad through `LoadOBJ` without
  topology loss for the supported attributes.
- [x] Exporter rejects empty meshes, out-of-range face indices, and
  non-writable paths with the documented `MeshIOWriteStatus` values.
- [x] `src/geometry/*` imports remain layered (`geometry -> core` only); no
  new asset/runtime/graphics imports introduced.
- [x] Existing `LoadOBJ`/`LoadOFF`/`LoadPLY`/`LoadSTL` and `PointCloudIO` /
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

## Completion
- Completed: 2026-05-08.
- Implementation commit: `b9cbb8e` (`GEOIO-002A: add geometry-owned OBJ
  mesh exporter`).
- Retired in a follow-up commit on
  `claude/setup-agentic-workflow-PjgiX`.
- Verified in this session:
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings.
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/repo/generate_module_inventory.py --root src --out
    docs/api/generated/module_inventory.md` — no module-surface change
    (only the regeneration date differed, so the inventory was left
    untouched to avoid date churn).
- Build/CTest gate not run in this container: `cmake --preset ci`
  configure fails because `clang-20`/`clang++-20` are not installed in
  this agent environment, matching the limitation called out under
  `Context`. Build evidence for the implementation slice was produced
  on the originating session and committed in `b9cbb8e`. The default
  CPU correctness gate
  (`ctest --test-dir build/ci -R 'GeometryIO' -LE
  'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) should be re-run
  on a host with the documented C++23 toolchain when available.
- Notes:
  - `Geometry::MeshIO::WriteOBJ` and `MeshIOWriteStatus` ship in
    `src/geometry/Geometry.HalfedgeMesh.IO.{cppm,cpp}`; round-trip and
    negative-path coverage lives in
    `tests/unit/geometry/Test.GeometryIO.cpp` (six new
    `WritesOBJ*`/`WriteOBJRejects*` cases).
  - Follow-up PLY/STL exporter slices remain tracked under the parent
    parent task `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
