# GEOIO-002R — Geometry-owned binary PCD point-cloud exporter

## Goal
- Add a geometry-owned binary little-endian PCD point-cloud
  exporter API (`WritePCDBinary`) to `Geometry.PointCloud.IO`
  that serializes a `PointCloudIOResult` (mandatory `x/y/z`;
  optional `normal_x/normal_y/normal_z`; optional `r/g/b`)
  using `DATA binary` framing, so the broader `GEOIO-002`
  parity work gains point-cloud writer parity symmetric to the
  existing PCD binary importer (`Geometry::PointCloudIO::LoadPCD`
  on its `DATA binary` branch).

## Non-goals
- No `binary_compressed` LZF support. The reader already
  explicitly rejects `binary_compressed` and that scope stays
  tracked under the parent backlog task.
- No packed `rgb` (`UINT32`-aliased float) or `rgba` field
  emission. The writer reuses the float `r g b` triple already
  produced by the ASCII PCD writer (`GEOIO-002O`) so colors
  round-trip through `LoadPCD`'s float-typed `r/g/b` path.
- No `uchar` (`SIZE 1 TYPE U`) color emission. The existing
  binary PCD reader test fixtures use that layout, but the
  reader is content with float `SIZE 4 TYPE F` colors too;
  matching the ASCII PCD writer keeps `WritePCD` and
  `WritePCDBinary` byte-identical apart from the `DATA` line
  and the encoded body, which simplifies the round-trip
  contract.
- No new format-detection metadata helpers.
- No assets/runtime ownership of point-cloud file IO; geometry
  owns format codecs only.
- No legacy graphics PCD exporter retirement (none exists
  today; this slice introduces the geometry-owned writer as new
  authority).
- No reader-side change: `LoadPCD` is unmodified and remains
  the ground-truth round-trip target.
- No new exporter for radii, intensity, or arbitrary
  user-defined per-point scalars beyond `x/y/z`, optional
  `normal_x/normal_y/normal_z`, and optional `r/g/b`.
  `Geometry::PointCloud::Cloud` does not store viewpoint
  metadata, so `VIEWPOINT` is emitted as the identity sensor
  pose.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: backlog.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-EP5PN`.
- Parent backlog task:
  `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002J..N` shipped point-cloud and graph writers
  symmetric to existing readers in `Geometry.PointCloud.IO` /
  `Geometry.Graph.IO`. `GEOIO-002O` added the geometry-owned
  ASCII PCD point-cloud writer (`WritePCD`). `GEOIO-002P`
  added the geometry-owned binary STL mesh writer.
  `GEOIO-002Q` added the geometry-owned text edge-list graph
  writer. The point-cloud IO module still lacks a binary PCD
  writer, leaving binary `DATA binary` PCD round-trip parity
  unproven on the CPU-only path.
- The reader is in
  `src/geometry/Geometry.PointCloud.IO.cpp::LoadPCD`. The
  `DATA binary` branch consumes a body whose stride equals
  `sum(SIZE[i] * COUNT[i])` and decodes each field via
  `ReadPCDBinaryScalar`. It accepts float `SIZE 4 TYPE F`
  fields for positions, normals, and `r/g/b` colors, and runs
  color channels through `NormalizeColorChannel`, which leaves
  values in `[0, 1]` clamped and unchanged.
- The writer therefore emits a header identical to `WritePCD`
  with `DATA binary` substituted for `DATA ascii`, then writes
  little-endian 4-byte floats for `x, y, z` followed by
  optional `nx, ny, nz` and optional `r, g, b`. Colors are
  clamped to `[0, 1]` before serialization so the reader's
  normalizer round-trips them unchanged. The host's float
  endianness is swapped into little-endian on big-endian hosts
  using the same pattern as `WritePLYBinary`.

## Required changes
- Extend `src/geometry/Geometry.PointCloud.IO.cppm`:
  - Add a
    `PointCloudIOWriteStatus WritePCDBinary(std::string_view absolute_path,
                                              const PointCloudIOResult& cloud);`
    declaration in the `Geometry::PointCloudIO` namespace
    after the existing `WritePCD` declaration. Reuse the
    existing `PointCloudIOWriteStatus` enum.
- Implement `WritePCDBinary` in
  `src/geometry/Geometry.PointCloud.IO.cpp` after `WritePCD`:
  - Reject empty `absolute_path` with `InvalidPath`.
  - Reject empty clouds (`source.IsEmpty()`) with `EmptyCloud`.
  - Open the output stream with
    `std::ios::binary | std::ios::trunc`; return `InvalidPath`
    if the stream cannot be opened.
  - Emit the header (in order):
    `# .PCD v0.7`,
    `VERSION 0.7`,
    `FIELDS x y z` (plus `normal_x normal_y normal_z` when
    `HasNormals()` and `Normals().size() == pointCount`,
    plus `r g b` when `HasColors()` and
    `Colors().size() == pointCount`),
    `SIZE 4 4 4` (and `4 4 4` for normals, `4 4 4` for colors),
    `TYPE F F F` (and `F F F` for normals, `F F F` for colors),
    `COUNT 1 1 1` (and `1 1 1` for normals, `1 1 1` for colors),
    `WIDTH <N>`,
    `HEIGHT 1`,
    `VIEWPOINT 0 0 0 1 0 0 0`,
    `POINTS <N>`,
    `DATA binary`.
  - For each point, emit the packed scalar block in field
    order: little-endian 4-byte float `x`, `y`, `z`, then
    optionally `nx`, `ny`, `nz`, then optionally `r`, `g`, `b`.
    Color channels are clamped to `[0, 1]` before writing so
    the round-trip stays inside `NormalizeColorChannel`'s
    no-op branch. Float endianness uses the same swap pattern
    as `WritePLYBinary` (`std::memcpy` into a 4-byte buffer
    and byte-swap when `std::endian::native == std::endian::big`).
  - Flush and report `FileWriteError` if `stream.good()` is
    false after writing the body.
- No additional public exports beyond `WritePCDBinary`; helper
  logic stays inside the existing translation-unit anonymous
  namespace or local to the function.
- No new module imports in
  `src/geometry/Geometry.PointCloud.IO.cppm`.

## Tests
- Add `unit;geometry` cases to
  `tests/unit/geometry/Test.GeometryIO.cpp` under
  `GeometryIO_PointCloudIO` after the existing `WritesPCD*`
  tests:
  - `WritesPCDBinaryPositionsOnly` — three positions only;
    expect success, header includes `FIELDS x y z`,
    `WIDTH 3`, `POINTS 3`, `DATA binary`; re-import via
    `LoadPCD` returns three points without normals/colors and
    with positions matching the originals.
  - `WritesPCDBinaryWithNormalsAndColorsRoundTrips` — two
    points with normals and colors enabled; expect header
    includes `FIELDS x y z normal_x normal_y normal_z r g b`
    and `DATA binary`; re-import returns matching positions,
    normals, and colors (within float exactness, since values
    are stored as exact 4-byte floats).
  - `WritesPCDBinaryIgnoresRadii` — one point with radii
    enabled and no normals/colors; expect header has only
    `x y z` fields and re-import has no radii/normals/colors.
  - `WritePCDBinaryRejectsEmptyCloud` — default-constructed
    cloud yields `EmptyCloud`.
  - `WritePCDBinaryRejectsBadPath` — empty `absolute_path`
    yields `InvalidPath`; a path under a non-existent
    directory yields `InvalidPath`.
- Use the existing `TempFile` helper and `ReadFileContents`
  in the test file; do not introduce new test-only headers.

## Docs
- Update the `OBJ/PLY/STL exporters` row in
  `docs/migration/nonlegacy-parity-matrix.md` so it also
  records the new geometry-owned binary PCD writer added
  under `GEOIO-002R`.
- Regenerate `docs/api/generated/module_inventory.md` only if
  the generator picks up changes to the existing
  `Geometry.PointCloud.IO` module surface. If the regenerator
  changes only the date stamp, leave it untouched.

## Acceptance criteria
- `Geometry::PointCloudIO::WritePCDBinary` compiles and is
  exported from `Geometry.PointCloud.IO`.
- New tests pass under `IntrinsicTests` and the CPU gate.
- No assets/runtime/graphics imports leak into
  `src/geometry/*`.
- Parity matrix records the new geometry-owned binary PCD
  writer.

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
- Adding assets/runtime/graphics/RHI imports to
  `src/geometry/*`.
- Adding `binary_compressed` LZF support, packed `rgb`/`rgba`
  field emission, or radii/intensity field writers in this
  slice.
- Changing the existing `LoadPCD` reader signature or
  behavior.
- Changing the existing `WritePCD` (ASCII) writer signature
  or behavior.
- Mixing mechanical legacy deletion with semantic IO
  implementation.
- Promoting arbitrary user-defined per-point property
  serialization in this slice.
