# GEOIO-002C — Geometry-owned ASCII STL mesh exporter

## Goal
- Add a geometry-owned ASCII STL exporter API to `Geometry.MeshIO` that
  serializes a `MeshIOResult` (positions, triangle face lists) without
  introducing assets/runtime/graphics dependencies, completing the OBJ /
  PLY / STL exporter trio called for by the parent `GEOIO-002` parity
  task and matching the diagnostics scaffolding established by
  `GEOIO-002A` (OBJ) and `GEOIO-002B` (PLY).

## Non-goals
- No binary STL output. ASCII only, mirroring the existing `LoadSTL`
  importer (which only consumes ASCII `vertex` lines).
- No options struct, no per-solid naming overrides, no attribute byte
  count emission beyond the canonical ASCII `solid` framing.
- No n-gon triangulation: faces with arity != 3 are rejected with
  `InvalidFace`, matching the legacy `STLExporter::Export` behavior of
  rejecting non-triangle topology with `AssetError::InvalidData`.
- No legacy `Graphics::STLExporter`/`IAssetExporter` registry deletion
  or rewiring; that retirement remains tracked under `GRAPHICS-019`
  follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or GPU
  upload work.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-jNiN5`.
- Parent task:
  `tasks/done/GEOIO-002-geometry-io-parity-hardening.md`.
- Predecessor slices:
  - `tasks/done/GEOIO-002A-mesh-obj-exporter.md` introduced
    `MeshIOWriteStatus` and `WriteOBJ`.
  - `tasks/done/GEOIO-002B-mesh-ply-exporter.md` added `WritePLY`
    reusing the same enum and validation pattern.
- `MeshIOResult` stores positions in the `v:point` `glm::vec3` vertex
  property and per-face polygon vertex index lists in the `f:vertices`
  face property (matches the importer side). STL has no concept of
  per-vertex normals — only per-facet normals — so this slice does not
  consume the optional `v:normal` property; the per-facet normal is
  always computed from triangle geometry.
- The existing `LoadSTL` parser consumes any `vertex x y z` line in the
  text, accumulates them in groups of three, and ignores `solid` /
  `facet` / `outer loop` / `endloop` / `endfacet` / `endsolid` framing,
  so emitting the canonical ASCII solid framing remains backward
  compatible with the importer.
- Container build environment is missing `clang-20` and `libxrandr` dev
  headers, so the standard `cmake --preset ci` configure currently fails
  at GLFW dependency discovery (see `tasks/backlog/bugs/index.md` and
  the prior `GEOIO-002A`/`GEOIO-002B` retirement notes). Report build
  evidence honestly; the focused gate may not run in the agent
  container.

## Required changes
- [x] Extend `src/geometry/Geometry.HalfedgeMesh.IO.cppm`:
  - [x] Add a function declaration
    `MeshIOWriteStatus WriteSTL(std::string_view absolute_path,
                                 const MeshIOResult& mesh);`
  - [x] Reuse the existing `MeshIOWriteStatus` enum unchanged.
  - [x] Keep all existing `Load*` / `WriteOBJ` / `WritePLY` declarations
    bit-for-bit.
- [x] Implement `WriteSTL` in `src/geometry/Geometry.HalfedgeMesh.IO.cpp`:
  - [x] Add `#include <cmath>` and `#include <glm/geometric.hpp>` to the
    module preamble (needed for `std::isfinite`, `glm::cross`,
    `glm::normalize`).
  - [x] Reject empty paths with `InvalidPath`.
  - [x] Reject empty meshes (no `v:point` property, zero vertices, no
    `f:vertices` property, or zero faces) with `EmptyMesh`.
  - [x] Reject any face with `size() != 3` (STL is strictly triangles) or
    any out-of-range index with `InvalidFace`. No mutation of input.
  - [x] Open the output stream with `std::ios::binary | std::ios::trunc`;
    on `!stream` return `InvalidPath`; on post-write `!stream.good()`
    return `FileWriteError`.
  - [x] Write the canonical ASCII STL framing:
    ```
    solid IntrinsicEngine
      facet normal <nx> <ny> <nz>
        outer loop
          vertex <x0> <y0> <z0>
          vertex <x1> <y1> <z1>
          vertex <x2> <y2> <z2>
        endloop
      endfacet
      ...
    endsolid IntrinsicEngine
    ```
  - [x] For each triangle face, compute
    `normal = glm::normalize(glm::cross(v1 - v0, v2 - v0))` and substitute
    `glm::vec3(0.0f)` when `!std::isfinite(normal.x)` (degenerate
    triangle), matching the legacy `STLExporter::Export` ASCII branch.
  - [x] Use `%.6e` formatting for both the per-facet normal and each vertex
    line to match the legacy ASCII STL output style.
- [x] Do not change importer behavior, do not introduce new module imports,
  and do not touch any file outside `src/geometry/`,
  `tests/unit/geometry/`, `tasks/`, and the generated module inventory.

## Tests
- [x] Add focused `unit;geometry` coverage to
  `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] `WritesSTLTriangle`: write a synthetic triangle `MeshIOResult`,
    re-import via `LoadSTL`, verify topology and vertex equivalence via
    `ExpectTriangleMeshProperties`.
  - [x] `WritesSTLTriangleEmitsFacetNormal`: write a triangle whose normal
    is `+Z`; verify the file contents contain `solid IntrinsicEngine`,
    `endsolid IntrinsicEngine`, an `outer loop` block, and a
    `facet normal 0.000000e+00 0.000000e+00 1.000000e+00` line.
  - [x] `WriteSTLRejectsQuadFace`: a single quad face returns `InvalidFace`
    (this is the slice's distinguishing behavior — STL is triangles
    only).
  - [x] `WriteSTLRejectsEmptyMesh`: empty `MeshIOResult` returns `EmptyMesh`.
  - [x] `WriteSTLRejectsOutOfRangeIndex`: a face referencing an out-of-range
    vertex returns `InvalidFace`.
  - [x] `WriteSTLRejectsBadPath`: a path under a non-existent directory
    returns `InvalidPath`.

## Docs
- [x] Update `docs/api/generated/module_inventory.md` only if module surfaces
  changed in a way the generator picks up
  (`python3 tools/repo/generate_module_inventory.py --root src --out
  docs/api/generated/module_inventory.md`). Adding a single new exported
  function to the existing `Geometry.MeshIO` module is expected to update
  the inventory; if the regenerator only changes the date, leave it
  untouched (matches `GEOIO-002B` precedent).
- [x] No additional architecture/migration doc edits required for this
  slice; parity-matrix updates remain part of the parent `GEOIO-002`
  task once asset/runtime routing actually drops the legacy graphics
  exporters.

## Acceptance criteria
- [x] `Geometry::MeshIO::WriteSTL` exists, is callable from `unit;geometry`
  tests, and round-trips a triangle through `LoadSTL` without topology
  loss.
- [x] Exporter rejects empty paths, empty meshes, non-triangle face arity,
  out-of-range face indices, and non-writable paths with the documented
  `MeshIOWriteStatus` values.
- [x] `src/geometry/*` imports remain layered (`geometry -> core` only); no
  new asset/runtime/graphics imports introduced.
- [x] Existing `LoadOBJ`/`LoadOFF`/`LoadPLY`/`LoadSTL`,
  `PointCloudIO`/`GraphIO`, `WriteOBJ*`, and `WritePLY*` tests continue
  to pass.

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
- Adding binary STL, options struct, or n-gon triangulation in this
  slice.
- Adding per-vertex normal, color, or material output (STL has no such
  concept in ASCII form, and no extension is in scope here).
- Touching `src/legacy/Graphics/Exporters/*` other than reading them as
  behavioral reference.
- Auto-acknowledging or mutating any runtime/render extraction state
  (unrelated to this slice).
- Introducing GPU/Vulkan-only verification requirements.

## Completion
- Completed: 2026-05-08.
- Implementation commit: `f112cfe`
  (`GEOIO-002C: add geometry-owned ASCII STL mesh exporter`).
- Retired in a follow-up commit on
  `claude/setup-agentic-workflow-jNiN5`.
- Verified in this session:
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (85 task files validated).
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/repo/generate_module_inventory.py --root src --out
    docs/api/generated/module_inventory.md` — only the regeneration
    date and pre-existing renames from prior commits differ; this
    slice adds a function to the existing exported `Geometry.MeshIO`
    module without changing the module name set, so the inventory was
    left untouched to avoid mixing unrelated drift into this slice
    (matches `GEOIO-002B` precedent).
- Build/CTest gate not run in this container: `cmake --preset ci`
  configure fails because `clang-20`/`clang++-20` are not installed in
  this agent environment, matching the limitation called out under
  `Context` and the prior `GEOIO-002A`/`GEOIO-002B` retirement notes.
  The default CPU correctness gate
  (`ctest --test-dir build/ci -R 'GeometryIO' -LE
  'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) should be re-run
  on a host with the documented C++23 toolchain when available.
- Notes:
  - `Geometry::MeshIO::WriteSTL` ships in
    `src/geometry/Geometry.HalfedgeMesh.IO.{cppm,cpp}` and reuses the
    existing `MeshIOWriteStatus` enum from `GEOIO-002A`.
  - Round-trip and negative-path coverage lives in
    `tests/unit/geometry/Test.GeometryIO.cpp` (six new
    `WritesSTL*`/`WriteSTLRejects*` cases).
  - With this slice, the `GEOIO-002` parent task's OBJ/PLY/STL
    exporter trio is complete; remaining `GEOIO-002` scope (importer
    parity hardening, asset/runtime routing) is still tracked under
    the parent backlog task
    `tasks/done/GEOIO-002-geometry-io-parity-hardening.md`.
