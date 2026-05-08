# GEOIO-002B — Geometry-owned ASCII PLY mesh exporter

## Goal
- Add a geometry-owned ASCII PLY exporter API to `Geometry.MeshIO` that
  serializes a `MeshIOResult` (positions, optional vertex normals, polygon
  face lists) without introducing assets/runtime/graphics dependencies, so
  the broader `GEOIO-002` parity work can layer the STL exporter and
  asset/runtime routing on the same scaffolding established by
  `GEOIO-002A`.

## Non-goals
- No STL exporter in this slice (separate slice under `GEOIO-002`).
- No binary PLY (`format binary_little_endian` / `binary_big_endian`)
  output. ASCII only, mirroring the existing `LoadPLY` importer.
- No vertex color, texture coordinate, material, or arbitrary
  user-property output.
- No legacy `Graphics::PLYExporter`/`IAssetExporter` registry deletion or
  rewiring; that retirement remains tracked under `GRAPHICS-019` follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or GPU
  upload work.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-rX6Um`.
- Parent backlog task: `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- Predecessor slice: `tasks/done/GEOIO-002A-mesh-obj-exporter.md` introduced
  `MeshIOWriteStatus` and `WriteOBJ` with `EmptyMesh`/`InvalidFace`/
  `InvalidPath`/`FileWriteError` diagnostics; this slice reuses the same
  enum and validation pattern for PLY.
- `MeshIOResult` stores positions in the `v:point` `glm::vec3` vertex
  property, optional normals in `v:normal`, and per-face polygon vertex
  index lists in the `f:vertices` face property (matches the importer
  side).
- The existing `LoadPLY` only consumes the first three whitespace-separated
  numeric tokens per vertex line as `x y z` and ignores any trailing
  tokens, so emitting `nx ny nz` after the position on the same line
  remains backward-compatible for round-tripping positions and faces
  through `LoadPLY`.
- Container build environment is missing `clang-20` and `libxrandr` dev
  headers, so the standard `cmake --preset ci` configure currently fails
  at GLFW dependency discovery (see `tasks/backlog/bugs/index.md`). Report
  build evidence honestly; the focused gate may not run in the agent
  container.

## Required changes
- Extend `src/geometry/Geometry.HalfedgeMesh.IO.cppm`:
  - Add a function declaration
    `MeshIOWriteStatus WritePLY(std::string_view absolute_path,
                                 const MeshIOResult& mesh);`
  - Reuse the existing `MeshIOWriteStatus` enum unchanged.
  - Keep all existing `Load*` and `WriteOBJ` declarations bit-for-bit.
- Implement `WritePLY` in `src/geometry/Geometry.HalfedgeMesh.IO.cpp`:
  - Reject empty paths with `InvalidPath`.
  - Reject empty meshes (no `v:point` property, zero vertices, no
    `f:vertices` property, or zero faces) with `EmptyMesh`.
  - Reject any face with fewer than three indices or any out-of-range
    index with `InvalidFace`. No mutation of input.
  - Open the output stream with `std::ios::binary | std::ios::trunc`; on
    `!stream` return `InvalidPath`; on post-write `!stream.good()` return
    `FileWriteError`.
  - Write the canonical ASCII PLY header:
    ```
    ply
    format ascii 1.0
    comment Exported by IntrinsicEngine
    element vertex <N>
    property float x
    property float y
    property float z
    [property float nx
     property float ny
     property float nz]
    element face <M>
    property list uchar int vertex_indices
    end_header
    ```
    The three `nx/ny/nz` properties are emitted only when a `v:normal`
    `glm::vec3` property of equal length to positions is present.
  - Write each vertex on a single line: `%.6f %.6f %.6f` for `x y z`,
    optionally followed by ` %.6f %.6f %.6f` for `nx ny nz` when normals
    are present. Terminate each line with `\n`.
  - Write each face as `<count> i0 i1 ... ik\n` with zero-based indices.
- Do not change importer behavior, do not introduce new module imports,
  and do not touch any file outside `src/geometry/`,
  `tests/unit/geometry/`, `tasks/`, and the generated module inventory.

## Tests
- Add focused `unit;geometry` coverage to
  `tests/unit/geometry/Test.GeometryIO.cpp`:
  - `WritesPLYTriangle`: write a synthetic triangle `MeshIOResult`,
    re-import via `LoadPLY`, verify topology and vertex equivalence.
  - `WritesPLYTriangleWithNormals`: same as above but with vertex
    normals populated; verify the file contains the `property float nx`
    header line and a `0.000000 0.000000 0.000000 0.000000 0.000000 1.000000`
    style vertex line, and that re-import via `LoadPLY` keeps positions
    and face indices.
  - `WritesPLYQuadRoundTripsFaceArity`: a single quad face survives the
    `WritePLY` -> `LoadPLY` round-trip with `f:vertices` arity 4.
  - `WritePLYRejectsEmptyMesh`: empty `MeshIOResult` returns `EmptyMesh`.
  - `WritePLYRejectsOutOfRangeIndex`: a face referencing an out-of-range
    vertex returns `InvalidFace`.
  - `WritePLYRejectsBadPath`: a path under a non-existent directory
    returns `InvalidPath`.

## Docs
- Update `docs/api/generated/module_inventory.md` only if module surfaces
  changed in a way the generator picks up
  (`python3 tools/repo/generate_module_inventory.py --root src --out
  docs/api/generated/module_inventory.md`). Adding a single new exported
  function to the existing `Geometry.MeshIO` module is expected to update
  the inventory; if the regenerator only changes the date, leave it
  untouched.
- No additional architecture/migration doc edits required for this slice;
  parity-matrix updates remain part of the parent `GEOIO-002` task once
  STL exporter parity also lands.

## Acceptance criteria
- `Geometry::MeshIO::WritePLY` exists, is callable from `unit;geometry`
  tests, and round-trips a triangle and a quad through `LoadPLY` without
  topology loss for the supported attributes.
- Exporter rejects empty paths, empty meshes, out-of-range face indices,
  and non-writable paths with the documented `MeshIOWriteStatus` values.
- `src/geometry/*` imports remain layered (`geometry -> core` only); no
  new asset/runtime/graphics imports introduced.
- Existing `LoadOBJ`/`LoadOFF`/`LoadPLY`/`LoadSTL` and `PointCloudIO` /
  `GraphIO` tests continue to pass, and the `WriteOBJ*` cases added by
  `GEOIO-002A` continue to pass.

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
- Adding STL or binary-PLY exporters in this slice.
- Adding vertex color, texture coordinate, or material output.
- Touching `src/legacy/Graphics/Exporters/*` other than reading them as
  behavioral reference.
- Auto-acknowledging or mutating any runtime/render extraction state
  (unrelated to this slice).
- Introducing GPU/Vulkan-only verification requirements.

## Completion
- Completed: 2026-05-08.
- Implementation commit: `15b6dce`
  (`GEOIO-002B: add geometry-owned PLY mesh exporter`).
- Retired in a follow-up commit on
  `claude/setup-agentic-workflow-rX6Um`.
- Verified in this session:
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (84 task files validated).
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/repo/generate_module_inventory.py --root src --out
    docs/api/generated/module_inventory.md` — only the regeneration
    date and pre-existing renames from commit `d070e17` differ; this
    slice adds a function to the existing exported `Geometry.MeshIO`
    module without changing the module name set, so the inventory was
    left untouched to avoid mixing unrelated drift into this slice.
- Build/CTest gate not run in this container: `cmake --preset ci`
  configure fails because `clang-20`/`clang++-20` are not installed in
  this agent environment, matching the limitation called out under
  `Context` and the prior `GEOIO-002A` retirement. The default CPU
  correctness gate
  (`ctest --test-dir build/ci -R 'GeometryIO' -LE
  'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) should be re-run
  on a host with the documented C++23 toolchain when available.
- Notes:
  - `Geometry::MeshIO::WritePLY` ships in
    `src/geometry/Geometry.HalfedgeMesh.IO.{cppm,cpp}` and reuses the
    existing `MeshIOWriteStatus` enum from `GEOIO-002A`.
  - Round-trip and negative-path coverage lives in
    `tests/unit/geometry/Test.GeometryIO.cpp` (six new
    `WritesPLY*`/`WritePLYRejects*` cases).
  - The remaining `GEOIO-002` exporter slice (STL) is still tracked
    under the parent backlog task
    `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
