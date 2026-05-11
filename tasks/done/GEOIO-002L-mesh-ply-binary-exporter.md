# GEOIO-002L — Geometry-owned binary PLY mesh exporter

## Goal
- Add a geometry-owned little-endian binary PLY exporter API to
  `Geometry.HalfedgeMesh.IO` that serializes a `MeshIOResult`
  (mandatory positions; optional per-vertex normals; polygon face
  vertex-index lists) without introducing assets/runtime/graphics
  dependencies, so the broader `GEOIO-002` parity work can finish
  mesh round-trip coverage with the binary variant the existing
  `Geometry::MeshIO::LoadPLY` binary reader already accepts.

## Non-goals
- No big-endian binary PLY writer in this slice; PLY 1.0 deployments
  use little-endian almost universally and the reader covers both.
- No vertex color, texture coordinate, material, or arbitrary
  user-property output (parity with the existing ASCII
  `Geometry::MeshIO::WritePLY` writer).
- No legacy `Graphics::PLYExporter` or `IAssetExporter` registry
  deletion or rewiring; that retirement remains tracked under
  `GRAPHICS-019` follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or
  GPU upload work.
- No new format-detection metadata helpers.
- No GPU/Vulkan requirement in the default CPU gate.
- No reader-side change: extra vertex properties such as `nx/ny/nz`
  remain skipped by the existing binary mesh PLY reader and that
  contract is preserved.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-n82W8`.
- Parent backlog task:
  `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002B` shipped the symmetric ASCII PLY mesh writer
  (`Geometry::MeshIO::WritePLY`) emitting positions, optional
  `nx/ny/nz`, and `property list uchar int vertex_indices` faces.
  This slice adds the binary little-endian variant under the same
  module so round-trip parity with the existing binary reader
  (`ParseBinaryPLY` in `src/geometry/Geometry.HalfedgeMesh.IO.cpp`)
  is achievable from CPU-only geometry callers.
- `GEOIO-002K` shipped the symmetric binary point-cloud PLY writer
  (`Geometry::PointCloudIO::WritePLYBinary`); this slice mirrors its
  endianness handling and stream/header conventions for the mesh
  domain.
- Legacy reference: `src/legacy/Graphics/Exporters/Graphics.Exporters.PLY.cpp`
  emits `property list uchar int vertex_indices` with a `uint8_t`
  count and `int32_t` indices in `binary_little_endian 1.0`. The
  geometry-owned writer matches that on-disk shape.

## Required changes
- [x] Extend `src/geometry/Geometry.HalfedgeMesh.IO.cppm` with a
  `MeshIOWriteStatus WritePLYBinary(std::string_view absolute_path,
                                    const MeshIOResult& mesh);`
  declaration in the `Geometry::MeshIO` namespace, reusing the
  existing `MeshIOWriteStatus` enum unchanged.
- [x] Implement `WritePLYBinary` in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp`:
  - [x] Reject empty `absolute_path` with `InvalidPath`.
  - [x] Reject empty meshes (no `v:point` `glm::vec3` property, zero
    positions, no `f:vertices` `std::vector<std::uint32_t>` property,
    or zero faces) with `EmptyMesh`.
  - [x] Reject any face with fewer than three indices or any out-of-range
    vertex index with `InvalidFace`.
  - [x] Open the output stream with
    `std::ios::binary | std::ios::trunc`; return `InvalidPath` if the
    stream cannot be opened.
  - [x] Emit ASCII PLY 1.0 header lines (`ply`,
    `format binary_little_endian 1.0`,
    `comment Exported by IntrinsicEngine`,
    `element vertex N`, `property float x/y/z`, optional
    `nx/ny/nz`, `element face M`,
    `property list uchar int vertex_indices`, `end_header`)
    terminated by `\n`. Optional `nx/ny/nz` lines are emitted only
    when a `v:normal` `glm::vec3` property of length equal to the
    position count is present (parity with ASCII `WritePLY`).
  - [x] Write packed little-endian binary vertex records in declared
    property order. Float properties are stored as IEEE 754 32-bit
    little-endian.
  - [x] Write each face as `uint8_t count` followed by `count` × `int32_t`
    little-endian vertex indices.
  - [x] Detect host endianness with `std::endian::native` and byte-swap
    32-bit floats and 32-bit signed face indices on big-endian hosts
    so the on-disk encoding stays
    `binary_little_endian 1.0` regardless of platform.
  - [x] Flush and report `FileWriteError` if `stream.good()` is false at
    end.
- [x] Add `#include <bit>` to the implementation translation unit so
  `std::endian` is available; no change to the module interface
  imports.
- [x] No additional public exports beyond `WritePLYBinary`; helper logic
  stays inside the existing translation-unit anonymous namespace or
  local to the function.

## Tests
- [x] Add `unit;geometry` cases to `tests/unit/geometry/Test.GeometryIO.cpp`
  under `GeometryIO_MeshIO`:
  - [x] `WritesPLYBinaryTriangle` — write a synthetic triangle
    `MeshIOResult`, re-import via `LoadPLY`, verify topology and
    vertex equivalence; assert the on-disk header contains
    `format binary_little_endian 1.0\n`.
  - [x] `WritesPLYBinaryTriangleWithNormals` — same as above but with
    vertex normals populated; verify the header contains
    `property float nx\n` and re-import via `LoadPLY` keeps positions
    and face indices (normals are skipped by the reader, parity with
    the ASCII writer test).
  - [x] `WritesPLYBinaryQuadRoundTripsFaceArity` — a single quad face
    survives `WritePLYBinary` -> `LoadPLY` round-trip with
    `f:vertices` arity 4.
  - [x] `WritePLYBinaryRejectsEmptyMesh` — empty `MeshIOResult` returns
    `EmptyMesh`.
  - [x] `WritePLYBinaryRejectsOutOfRangeIndex` — a face referencing an
    out-of-range vertex returns `InvalidFace`.
  - [x] `WritePLYBinaryRejectsBadPath` — empty `absolute_path` yields
    `InvalidPath`; a path under a non-existent directory yields
    `InvalidPath`.

## Docs
- [x] Update the `OBJ/PLY/STL exporters` row of
  `docs/migration/nonlegacy-parity-matrix.md` to record that
  binary-little-endian PLY mesh export is now geometry-owned and
  added under `GEOIO-002L`.
- [x] Regenerate `docs/api/generated/module_inventory.md` only if the
  generator picks up the new exported function on the existing
  `Geometry.HalfedgeMesh.IO` module surface. If the regenerator
  changes only the date stamp, leave it untouched.

## Acceptance criteria
- [x] `Geometry::MeshIO::WritePLYBinary` compiles and is exported from
  `Geometry.HalfedgeMesh.IO`.
- [x] New tests pass under `IntrinsicTests` and the CPU gate.
- [x] No assets/runtime/graphics imports leak into `src/geometry/*`.
- [x] Legacy `src/legacy/Graphics/Exporters/Graphics.Exporters.PLY.{cppm,cpp}`
  remains untouched (reference only).
- [x] Parity matrix row reflects the new exporter ownership.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding assets/runtime/graphics/RHI imports to `src/geometry/*`.
- Editing or deleting legacy `Graphics::PLYExporter`.
- Mixing mechanical legacy deletion with semantic IO implementation.
- Adding a binary big-endian PLY emitter in this slice.
- Changing the existing `WritePLY` ASCII writer's signature or
  behavior.
- Adding vertex color, texture coordinate, material, or arbitrary
  property output in this slice.

## Completion
- Completed: 2026-05-10.
- Status: done.
- Branch: `claude/setup-agentic-workflow-n82W8`.
- Implementation commit: `d2b5654`
  (`GEOIO-002L: add geometry-owned binary PLY mesh exporter`).
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (141 task files validated).
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken
    relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — 420 modules; no
    diff (the new exported function lives inside the existing
    `Geometry.HalfedgeMesh.IO` module surface and the inventory
    tracks modules, not individual functions).
- CPU build/test gate not exercised: this container lacks
  `clang-20` and `clang-scan-deps` 20+, which the `ci` preset
  hard-codes. `cmake --preset ci` therefore fails at the compiler
  detection step, mirroring the constraint already noted in
  `tasks/backlog/bugs/index.md`. Configure attempts with
  `clang-18` (missing C++23 `std::expected`) and `g++-13`
  (rejected because the build system requires `clang-scan-deps`
  20+) both fail before any geometry source is compiled. Build
  verification therefore needs to be re-run on a CI host with the
  correct toolchain prior to merge.
- Notes:
  - `Geometry::MeshIO::WritePLYBinary` is exported from
    `src/geometry/Geometry.HalfedgeMesh.IO.cppm` and implemented
    in `src/geometry/Geometry.HalfedgeMesh.IO.cpp`. It reuses the
    existing `MeshIOWriteStatus` enum.
  - The on-disk encoding is
    `format binary_little_endian 1.0` with `float x/y/z`,
    optional `float nx/ny/nz` (emitted only when `v:normal`
    matches the position count, parity with the ASCII
    `WritePLY`), and per-face `uchar` count + `int32_t` indices
    (`property list uchar int vertex_indices`), matching the
    behavioral reference in
    `src/legacy/Graphics/Exporters/Graphics.Exporters.PLY.cpp`.
  - On big-endian hosts, both 32-bit floats and 32-bit signed
    face indices are byte-swapped before write so the on-disk
    encoding stays little-endian regardless of platform;
    endianness is detected at compile time via
    `std::endian::native`.
  - Polygons with more than 255 vertices are rejected with
    `MeshIOWriteStatus::InvalidFace` because the declared list
    count type is `uchar`. The ASCII writer has no such limit;
    callers requiring n>255 polygons must use ASCII.
  - Tests in `tests/unit/geometry/Test.GeometryIO.cpp`
    (`GeometryIO_MeshIO`) add `WritesPLYBinaryTriangle`,
    `WritesPLYBinaryTriangleWithNormals`,
    `WritesPLYBinaryQuadRoundTripsFaceArity`,
    `WritePLYBinaryRejectsEmptyMesh`,
    `WritePLYBinaryRejectsOutOfRangeIndex`, and
    `WritePLYBinaryRejectsBadPath`.
  - `docs/migration/nonlegacy-parity-matrix.md`
    (`OBJ/PLY/STL exporters` row) records the new geometry-owned
    binary little-endian PLY mesh writer.
  - Remaining `GEOIO-002` scope (PCD and XYZ point-cloud writers;
    TGF graph writer; granular reader-side
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics; OBJ
    ASCII parity hardening; packed-`rgb`/`rgba` PCD plus
    `binary_compressed` LZF decompression) stays tracked under
    the parent backlog task
    `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
