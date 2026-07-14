# GEOIO-002K — Geometry-owned binary PLY point-cloud exporter

## Goal
- Add a geometry-owned little-endian binary PLY exporter API to
  `Geometry.PointCloud.IO` that serializes a `PointCloudIOResult`
  (mandatory positions; optional per-point normals, RGB colors, and radii)
  without introducing assets/runtime/graphics dependencies, so the broader
  `GEOIO-002` parity work can finish point-cloud round-trip coverage with
  the binary variant the existing reader already accepts.

## Non-goals
- No big-endian binary PLY writer in this slice; PLY 1.0 deployments use
  little-endian almost universally and the reader covers both.
- No PCD or XYZ exporter in this slice (separate `GEOIO-002` follow-ups).
- No write API for halfedge meshes or graphs (separate slices).
- No legacy `Graphics::PLYExporter` or `IAssetExporter` registry deletion
  or rewiring; that retirement remains tracked under `GRAPHICS-019`
  follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or GPU
  upload work.
- No new format-detection metadata helpers.
- No GPU/Vulkan requirement in the default CPU gate.
- No reader-side support for `radius` properties; the existing PLY
  point-cloud reader ignores them and that contract stays unchanged.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-hTttQ`.
- Parent task: `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002J` shipped the symmetric ASCII PLY point-cloud writer
  (`Geometry::PointCloudIO::WritePLY`). This slice adds the binary
  little-endian variant under the same module so round-trip parity with
  the existing binary reader (`ParseBinaryPLYPointCloud` in
  `src/geometry/Geometry.PointCloud.IO.cpp`) is achievable from CPU-only
  geometry callers.
- Public mesh PLY ASCII writer in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp::WritePLY` and the existing
  point-cloud ASCII writer remain the behavioral references for header
  emission. The binary reader's accepted property set
  (`x/y/z` as `float32`, `nx/ny/nz` as `float32`,
  `red/green/blue` as `uint8`) defines the binary writer's emitted
  property set; `radius` is emitted as `float32` to keep parity with the
  ASCII writer, even though the reader currently skips it.

## Required changes
- [x] Extend `src/geometry/Geometry.PointCloud.IO.cppm` with a
  `WritePLYBinary(std::string_view absolute_path, const PointCloudIOResult& cloud)`
  declaration in the `Geometry::PointCloudIO` namespace, returning the
  existing `PointCloudIOWriteStatus` enum.
- [x] Implement `WritePLYBinary` in
  `src/geometry/Geometry.PointCloud.IO.cpp`:
  - [x] Reject empty `absolute_path` with `InvalidPath`.
  - [x] Reject empty clouds (`Cloud.IsEmpty()`) with `EmptyCloud`.
  - [x] Open the file with `std::ofstream(... std::ios::binary | std::ios::trunc)`;
    return `InvalidPath` if the stream cannot be opened.
  - [x] Emit ASCII PLY 1.0 header lines (`ply`, `format binary_little_endian 1.0`,
    `comment Exported by IntrinsicEngine`, `element vertex N`,
    `property float x/y/z`, optional `nx/ny/nz`, optional
    `uchar red/green/blue` (clamp 0..1 channels to `0..255`),
    optional `property float radius`, `end_header`) terminated by `\n`.
  - [x] Write packed little-endian binary vertex records in declared
    property order. Float properties are stored as IEEE 754 32-bit
    little-endian; color channels as `uint8`.
  - [x] Detect host endianness with a 32-bit value and byte-swap floats
    on big-endian hosts so the on-disk encoding stays
    `binary_little_endian` regardless of platform.
  - [x] Flush and report `FileWriteError` if `stream.good()` is false at
    end.
- [x] No additional public exports beyond `WritePLYBinary`; helper logic stays
  inside the existing translation-unit anonymous namespace.

## Tests
- [x] Add `unit;geometry` cases to `tests/unit/geometry/Test.GeometryIO.cpp`
  under `GeometryIO_PointCloudIO`:
  - [x] `WritesPLYBinaryPointCloud` — three-point cloud, round-trip via
    `LoadPLY` and verify positions match exactly.
  - [x] `WritesPLYBinaryPointCloudWithNormalsAndColors` — round-trip
    preserves normals and color channels (255-mapped). Header text is
    asserted to declare `format binary_little_endian 1.0` and the
    expected property names.
  - [x] `WritesPLYBinaryPointCloudWithRadiiEmitsRadiusProperty` — header
    declares `property float radius`; reader still ignores it (parity
    with ASCII writer test).
  - [x] `WritePLYBinaryPointCloudRejectsEmptyCloud` — empty cloud yields
    `PointCloudIOWriteStatus::EmptyCloud`.
  - [x] `WritePLYBinaryPointCloudRejectsBadPath` — empty `absolute_path`
    yields `PointCloudIOWriteStatus::InvalidPath`; non-creatable
    directory path yields `InvalidPath`.

## Docs
- [x] Update the `OBJ/PLY/STL exporters` row of
  `docs/migration/nonlegacy-parity-matrix.md` to record that
  binary-little-endian PLY point-cloud export is now geometry-owned and
  added under `GEOIO-002K`.
- [x] No module surface inventory regeneration is required because no new
  module is introduced. Public surface of `Geometry.PointCloud.IO` gains
  a single new exported function in the existing namespace.

## Acceptance criteria
- [x] `Geometry::PointCloudIO::WritePLYBinary` compiles and is exported from
  `Geometry.PointCloud.IO`.
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
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding assets/runtime/graphics/RHI imports to `src/geometry/*`.
- Editing or deleting legacy `Graphics::PLYExporter`.
- Mixing mechanical legacy deletion with semantic IO implementation.
- Adding a binary big-endian PLY emitter in this slice.
- Adding PCD or XYZ exporters in this slice.
- Changing the existing `WritePLY` ASCII writer's signature or
  behavior.
- Extending the PLY reader to consume `radius` properties.

## Completion
- Completed: 2026-05-10.
- Status: done.
- Branch: `claude/setup-agentic-workflow-hTttQ`.
- Implementation commit: `1f61788`
  (`GEOIO-002K: add geometry-owned binary PLY point-cloud exporter`).
- Verified in this session:
  - `cmake --preset ci` — configure succeeded.
  - `cmake --build --preset ci --target IntrinsicGeometryTests` —
    427/427 build steps succeeded.
  - `ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` —
    75/75 GeometryIO tests passing, including the 5 new
    `WritesPLYBinaryPointCloud*` and `WritePLYBinaryPointCloud*` cases.
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (140 task files validated).
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken
    relative links.
- Notes:
  - `Geometry::PointCloudIO::WritePLYBinary` is exported from
    `src/geometry/Geometry.PointCloud.IO.cppm` and implemented in
    `src/geometry/Geometry.PointCloud.IO.cpp`. It reuses the existing
    `PointCloudIOWriteStatus` enum.
  - On big-endian hosts the writer byte-swaps each emitted 32-bit
    float so the on-disk encoding stays
    `binary_little_endian 1.0`; uint8 color channels are stored
    directly. Endianness is detected at compile time via
    `std::endian::native`.
  - Coverage in `tests/unit/geometry/Test.GeometryIO.cpp`
    (`GeometryIO_PointCloudIO`) adds `WritesPLYBinaryPointCloud`,
    `WritesPLYBinaryPointCloudWithNormalsAndColors`,
    `WritesPLYBinaryPointCloudWithRadiiEmitsRadiusProperty`,
    `WritePLYBinaryPointCloudRejectsEmptyCloud`, and
    `WritePLYBinaryPointCloudRejectsBadPath`.
  - `docs/migration/nonlegacy-parity-matrix.md` (`GRAPHICS-019` row,
    OBJ/PLY/STL exporters) records the new geometry-owned binary
    little-endian PLY point-cloud writer.
  - Remaining `GEOIO-002` scope (PCD and XYZ point-cloud writers; TGF
    graph writer; granular reader-side
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics; OBJ ASCII
    parity hardening; packed-`rgb`/`rgba` PCD plus `binary_compressed`
    LZF decompression; matching binary mesh PLY writer) stays tracked
    under the parent backlog task
    `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
