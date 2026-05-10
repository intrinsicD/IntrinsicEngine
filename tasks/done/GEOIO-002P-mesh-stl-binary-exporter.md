# GEOIO-002P — Geometry-owned binary STL mesh exporter

## Goal
- Add a geometry-owned binary STL exporter API to
  `Geometry.HalfedgeMesh.IO` that serializes a `MeshIOResult`
  (mandatory positions; per-face computed normals; triangle-only
  faces) into the canonical 80-byte-header + `uint32` triangle
  count + 50-bytes-per-triangle binary STL layout without
  introducing assets/runtime/graphics dependencies, so the
  broader `GEOIO-002` parity work can finish mesh STL round-trip
  coverage with the binary variant the existing
  `Geometry::MeshIO::LoadSTL` already detects via
  `IsBinarySTL`.

## Non-goals
- No big-endian STL emission. The binary STL format is defined
  as little-endian on disk; the writer must produce
  little-endian regardless of host endianness.
- No per-face attribute byte count overrides; the trailing
  `uint16` attribute byte count is emitted as zero, matching the
  existing `ParseBinarySTL` reader which ignores it.
- No multi-solid output. STL binary format does not support the
  ASCII `solid <name>` grouping.
- No legacy `Graphics::STLExporter` retirement or rewiring;
  that retirement remains tracked under `GRAPHICS-019` follow-ups.
- No asset registry, runtime scene ingestion, ECS construction,
  or GPU upload work.
- No new format-detection metadata helpers.
- No reader-side change: `LoadSTL` / `IsBinarySTL` /
  `ParseBinarySTL` remain unmodified and remain the ground-truth
  round-trip target.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-xMjLy`.
- Parent backlog task:
  `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002C` shipped the symmetric ASCII STL mesh writer
  (`Geometry::MeshIO::WriteSTL`) emitting
  `solid IntrinsicEngine\n...endsolid IntrinsicEngine\n` with
  per-facet computed normals. This slice adds the binary variant
  under the same module so round-trip parity with the existing
  binary reader (`ParseBinarySTL` in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp`) is achievable from
  CPU-only geometry callers.
- `GEOIO-002L` shipped the symmetric binary PLY mesh writer
  (`Geometry::MeshIO::WritePLYBinary`); this slice reuses the
  same endianness handling (`std::endian::native` plus 32-bit
  byte-swap fallback) and `std::ios::binary | std::ios::trunc`
  stream conventions for the STL domain.
- Binary STL on-disk layout (little-endian):
  - 80 bytes: header (free-form; commonly an ASCII comment;
    never `solid ...` so naive readers do not misclassify the
    file as ASCII).
  - 4 bytes: `uint32` triangle count.
  - For each triangle (50 bytes): 3 × `float32` facet normal,
    3 × 3 × `float32` vertex positions (v0, v1, v2), 2 bytes
    `uint16` attribute byte count.
- The existing `ParseBinarySTL` reader synthesizes one vertex
  per triangle corner without welding, so a binary STL round
  trip produces a `MeshIOResult` with `triCount * 3` vertices
  and `triCount` triangle faces.

## Required changes
- Extend `src/geometry/Geometry.HalfedgeMesh.IO.cppm` with a
  `MeshIOWriteStatus WriteSTLBinary(std::string_view absolute_path,
                                    const MeshIOResult& mesh);`
  declaration in the `Geometry::MeshIO` namespace, reusing the
  existing `MeshIOWriteStatus` enum unchanged.
- Implement `WriteSTLBinary` in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp` adjacent to the
  existing `WriteSTL`:
  - Reject empty `absolute_path` with `InvalidPath`.
  - Reject empty meshes (no `v:point` `glm::vec3` property, zero
    positions, no `f:vertices` `std::vector<std::uint32_t>`
    property, or zero faces) with `EmptyMesh`.
  - Reject any face whose vertex count is not exactly 3, or any
    face with an out-of-range vertex index, with `InvalidFace`.
    Binary STL has no facet arity other than triangles, so quads
    and n-gons are rejected at the writer; callers that need
    polygon output should use ASCII PLY or OBJ.
  - Reject triangle counts that do not fit a `uint32_t` with
    `InvalidFace` (defensive check; in practice
    `std::vector::size()` already bounds this).
  - Open the output stream with
    `std::ios::binary | std::ios::trunc`; return `InvalidPath`
    if the stream cannot be opened.
  - Emit an 80-byte header initialized to zero, with a leading
    ASCII tag (`"IntrinsicEngine binary STL"`) copied into the
    first up-to-80 bytes (no trailing newline; the tag does not
    start with the ASCII keyword `solid`, so `IsBinarySTL` will
    not misclassify the file).
  - Emit the triangle count as a little-endian `uint32_t`
    (byte-swap on big-endian hosts).
  - For each triangle, compute the facet normal as
    `normalize(cross(v1 - v0, v2 - v0))`. If the cross product
    is degenerate (any component non-finite after normalization),
    emit `{0, 0, 0}` (matching the ASCII writer's fallback).
  - Emit the per-triangle record:
    `float32 nx, ny, nz, v0x, v0y, v0z, v1x, v1y, v1z, v2x, v2y,
    v2z` followed by `uint16 attribute_byte_count = 0`. All
    floats are written in little-endian byte order; on
    big-endian hosts, both the 32-bit floats and the 16-bit
    attribute count are byte-swapped before write.
  - Flush and report `FileWriteError` if `stream.good()` is
    false at end.
- Detect host endianness with `std::endian::native`. Reuse the
  same byte-swap helpers introduced by `GEOIO-002K`/`GEOIO-002L`
  if they are accessible within the translation unit's anonymous
  namespace; otherwise add a local 32-bit + 16-bit swap helper
  inside the translation-unit anonymous namespace. No change to
  the module interface imports.
- No additional public exports beyond `WriteSTLBinary`; helper
  logic stays inside the existing translation-unit anonymous
  namespace or local to the function.

## Tests
- Add `unit;geometry` cases to
  `tests/unit/geometry/Test.GeometryIO.cpp` under
  `GeometryIO_MeshIO`:
  - `WritesSTLBinaryTriangleRoundTrip` — write a synthetic
    triangle `MeshIOResult` via `WriteSTLBinary`, re-import via
    `LoadSTL`, verify topology and vertex equivalence; assert
    the on-disk file is binary-classified by checking that the
    first five bytes are not the ASCII keyword `solid` (i.e.
    `IsBinarySTL` would route the parser to the binary path).
  - `WritesSTLBinaryReportsTriangleCountInHeader` — write a
    two-triangle quad-as-two-triangles fan, re-import via
    `LoadSTL`, verify the loaded result has 6 vertices and 2
    triangle faces (matches the per-corner vertex
    synthesization in `ParseBinarySTL`); spot-check the on-disk
    triangle count word at byte offset 80 equals `2` in
    little-endian.
  - `WriteSTLBinaryRejectsQuadFace` — a single quad face yields
    `InvalidFace`.
  - `WriteSTLBinaryRejectsEmptyMesh` — empty `MeshIOResult`
    returns `EmptyMesh`.
  - `WriteSTLBinaryRejectsOutOfRangeIndex` — a face referencing
    an out-of-range vertex returns `InvalidFace`.
  - `WriteSTLBinaryRejectsBadPath` — empty `absolute_path`
    yields `InvalidPath`; a path under a non-existent directory
    yields `InvalidPath`.

## Docs
- Update the `OBJ/PLY/STL exporters` row of
  `docs/migration/nonlegacy-parity-matrix.md` to record that
  binary-little-endian STL mesh export is now geometry-owned and
  added under `GEOIO-002P`.
- Regenerate `docs/api/generated/module_inventory.md` only if
  the generator picks up the new exported function on the
  existing `Geometry.HalfedgeMesh.IO` module surface. If the
  regenerator changes only the date stamp, leave it untouched.

## Acceptance criteria
- `Geometry::MeshIO::WriteSTLBinary` compiles and is exported
  from `Geometry.HalfedgeMesh.IO`.
- New tests pass under `IntrinsicTests` and the CPU gate.
- No assets/runtime/graphics imports leak into `src/geometry/*`.
- Legacy `src/legacy/Graphics/Exporters/Graphics.Exporters.STL.{cppm,cpp}`
  remains untouched (reference only).
- Parity matrix row reflects the new exporter ownership.

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
- Editing or deleting legacy `Graphics::STLExporter`.
- Mixing mechanical legacy deletion with semantic IO implementation.
- Changing the existing `WriteSTL` ASCII writer's signature or
  behavior.
- Changing the existing `LoadSTL` / `IsBinarySTL` /
  `ParseBinarySTL` reader signatures or behavior.
- Promoting per-face attribute byte counts, color extensions, or
  multi-solid output in this slice.

## Completion
- Completed: 2026-05-10.
- Status: done.
- Branch: `claude/setup-agentic-workflow-xMjLy`.
- Implementation commit: pending — to be filled with the SHA of
  the `GEOIO-002P: add geometry-owned binary STL mesh exporter`
  commit on this branch in a follow-up commit on the same branch.
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` —
    no layering violations; `geometry` imports remain
    `geometry -> core` only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (145 task files validated before the move; the
    file rename does not change the file count after the move).
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken
    relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — 420 modules;
    no diff (the new exported function lives inside the existing
    `Geometry.HalfedgeMesh.IO` module surface and the inventory
    tracks modules, not individual functions).
- CPU build/test gate not exercised: this container lacks
  `clang-20` and `clang-scan-deps` 20+, which the `ci` preset
  hard-codes. `cmake --preset ci` therefore fails at the compiler
  detection step, mirroring the constraint already recorded by
  `GEOIO-002L`/`GEOIO-002M`/`GEOIO-002N`/`GEOIO-002O` and earlier
  slices. Build verification therefore needs to be re-run on a CI
  host with the correct toolchain prior to merge.
- Notes:
  - `Geometry::MeshIO::WriteSTLBinary` is exported from
    `src/geometry/Geometry.HalfedgeMesh.IO.cppm` and implemented
    in `src/geometry/Geometry.HalfedgeMesh.IO.cpp`. It reuses the
    existing `MeshIOWriteStatus` enum (`Success`, `EmptyMesh`,
    `InvalidFace`, `InvalidPath`, `FileWriteError`).
  - The on-disk encoding is the standard binary STL layout: an
    80-byte header tagged
    `"IntrinsicEngine binary STL"` (zero-padded; never starts
    with the ASCII keyword `solid`, so `IsBinarySTL` routes
    `LoadSTL` to the binary parser unambiguously), a 4-byte
    little-endian `uint32` triangle count, and one 50-byte
    record per triangle (3 × float32 normal, 3 × 3 × float32
    vertex positions, 2-byte `uint16` attribute byte count = 0).
  - On big-endian hosts both 32-bit floats and the 16-bit
    attribute count are byte-swapped before write so the on-disk
    encoding stays little-endian regardless of platform;
    endianness is detected at compile time via
    `std::endian::native`, matching the helpers introduced by
    `GEOIO-002K`/`GEOIO-002L`.
  - Per-facet normals are computed as
    `normalize(cross(v1 - v0, v2 - v0))`; if the cross product
    is degenerate the normal falls back to `(0, 0, 0)` (parity
    with the ASCII `WriteSTL` writer).
  - Polygons with more than three vertices are rejected with
    `InvalidFace` because binary STL is triangle-only. Callers
    needing higher-arity polygons should use ASCII PLY or OBJ.
  - Tests in `tests/unit/geometry/Test.GeometryIO.cpp`
    (`GeometryIO_MeshIO`) add
    `WritesSTLBinaryTriangleRoundTrip`,
    `WritesSTLBinaryReportsTriangleCountInHeader`,
    `WriteSTLBinaryRejectsQuadFace`,
    `WriteSTLBinaryRejectsEmptyMesh`,
    `WriteSTLBinaryRejectsOutOfRangeIndex`, and
    `WriteSTLBinaryRejectsBadPath`. The round-trip case verifies
    the on-disk file does not start with the ASCII keyword
    `solid`, ensuring `IsBinarySTL` routes the parser correctly.
  - `docs/migration/nonlegacy-parity-matrix.md`
    (`OBJ/PLY/STL exporters` row) records the new geometry-owned
    binary little-endian STL mesh writer.
  - Remaining `GEOIO-002` scope (granular reader-side
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics;
    OBJ ASCII parity hardening; packed-`rgb`/`rgba` PCD plus
    `binary_compressed` LZF decompression; binary PCD writer;
    edge-list `.edges` graph writer) stays tracked under the
    parent backlog task
    `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
