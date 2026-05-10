# GEOIO-002O — Geometry-owned ASCII PCD point-cloud exporter

## Goal
- Add a geometry-owned ASCII PCD point-cloud exporter API to
  `Geometry.PointCloud.IO` that serializes a
  `PointCloudIOResult` (mandatory `x/y/z`; optional
  `normal_x/normal_y/normal_z`; optional `r/g/b`) without
  introducing assets/runtime/graphics dependencies, so the
  broader `GEOIO-002` parity work can grow point-cloud
  round-trip coverage symmetric to the existing PCD importer
  (`Geometry::PointCloudIO::LoadPCD`).

## Non-goals
- No binary PCD writer in this slice. The binary path requires
  additional layout choices (packed `rgb`/`rgba`, viewpoint
  metadata, `binary_compressed`) tracked in the parent task.
- No `binary_compressed` LZF support.
- No new format-detection metadata helpers.
- No assets/runtime ownership of point-cloud file IO; geometry
  owns format codecs only.
- No legacy graphics PCD exporter retirement (none exists today;
  this slice introduces the geometry-owned writer as new
  authority).
- No reader-side change: `LoadPCD` is unmodified and remains the
  ground-truth round-trip target.
- No new exporter for radii or arbitrary user-defined per-point
  scalars beyond `x/y/z`, optional `normal_x/normal_y/normal_z`,
  and optional `r/g/b`. `Geometry::PointCloud::Cloud` does not
  store viewpoint metadata, so `VIEWPOINT` is emitted as the
  identity sensor pose.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-rHAVf`.
- Parent backlog task:
  `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002J..M` shipped point-cloud and mesh writers
  symmetric to existing readers in `Geometry.PointCloud.IO` /
  `Geometry.HalfedgeMesh.IO`. `GEOIO-002N` added the
  geometry-owned text TGF graph writer. The point-cloud IO
  module currently has `WritePLY`, `WritePLYBinary`, and
  `WriteXYZ` but no PCD writer, leaving point-cloud round-trip
  parity unproven for the PCD format on the CPU-only path.
- The reader is in
  `src/geometry/Geometry.PointCloud.IO.cpp::LoadPCD`. The
  ASCII path requires `FIELDS`, `SIZE`, `TYPE`, `COUNT`,
  `WIDTH`, `HEIGHT`, `POINTS`, and `DATA ascii` header lines,
  then one whitespace-separated row per point. `x/y/z` are
  mandatory; the optional triples `normal_x/normal_y/normal_z`
  and `r/g/b` enable normals and colors. Colors are parsed as
  floats and run through `NormalizeColorChannel`, which leaves
  values in `[0, 1]` untouched (clamped) and divides values
  `> 1.0` by `255.0`.
- The writer therefore emits each row as
  `x y z [nx ny nz] [r g b]` with `%.6f` format. Colors are
  written as floats in `[0, 1]` so the reader's normalizer
  round-trips them unchanged. Radii are not part of the PCD
  field set the reader recognizes; the writer must not emit
  them.

## Required changes
- Extend `src/geometry/Geometry.PointCloud.IO.cppm`:
  - Add a
    `PointCloudIOWriteStatus WritePCD(std::string_view absolute_path,
                                       const PointCloudIOResult& cloud);`
    declaration in the `Geometry::PointCloudIO` namespace
    after the existing `WriteXYZ` declaration. Reuse the
    existing `PointCloudIOWriteStatus` enum.
- Implement `WritePCD` in
  `src/geometry/Geometry.PointCloud.IO.cpp` after `WriteXYZ`:
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
    `DATA ascii`.
  - For each point, emit one whitespace-separated line with
    `%.6f`-formatted values: `x y z`, then optionally
    `nx ny nz`, then optionally `r g b`, terminated by `\n`.
    Color channels are clamped to `[0, 1]` before writing so
    the round-trip stays inside `NormalizeColorChannel`'s
    no-op branch.
  - Flush and report `FileWriteError` if `stream.good()` is
    false after writing the body.
- No additional public exports beyond `WritePCD`; helper logic
  stays inside the existing translation-unit anonymous
  namespace or local to the function.
- No new module imports in
  `src/geometry/Geometry.PointCloud.IO.cppm`.

## Tests
- Add `unit;geometry` cases to
  `tests/unit/geometry/Test.GeometryIO.cpp` under
  `GeometryIO_PointCloudIO` after the existing `WritesXYZ*`
  tests:
  - `WritesPCDPositionsOnly` — three positions only; expect
    success, header includes `FIELDS x y z`, `WIDTH 3`,
    `POINTS 3`, `DATA ascii`; re-import via `LoadPCD` returns
    three points without normals/colors.
  - `WritesPCDWithNormalsAndColorsRoundTrips` — two points
    with normals and colors enabled; expect header includes
    `FIELDS x y z normal_x normal_y normal_z r g b`; re-import
    returns matching positions, normals, and colors.
  - `WritesPCDIgnoresRadii` — one point with radii enabled and
    no normals/colors; expect header has only `x y z` fields
    and re-import has no radii/normals/colors.
  - `WritePCDRejectsEmptyCloud` — default-constructed cloud
    yields `EmptyCloud`.
  - `WritePCDRejectsBadPath` — empty `absolute_path` yields
    `InvalidPath`; a path under a non-existent directory
    yields `InvalidPath`.
- Use the existing `TempFile` helper and `ReadFileContents`
  in the test file; do not introduce new test-only headers.

## Docs
- Update the `OBJ/PLY/STL exporters` row in
  `docs/migration/nonlegacy-parity-matrix.md` so it also
  records the new geometry-owned ASCII PCD writer added under
  `GEOIO-002O` (rename the row to span point-cloud exporters
  consistently with the existing additions; or add a new
  dedicated row if the existing row becomes unwieldy).
- Regenerate `docs/api/generated/module_inventory.md` only if
  the generator picks up changes to the existing
  `Geometry.PointCloud.IO` module surface. If the regenerator
  changes only the date stamp, leave it untouched.

## Acceptance criteria
- `Geometry::PointCloudIO::WritePCD` compiles and is exported
  from `Geometry.PointCloud.IO`.
- New tests pass under `IntrinsicTests` and the CPU gate.
- No assets/runtime/graphics imports leak into
  `src/geometry/*`.
- Parity matrix records the new geometry-owned ASCII PCD
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
- Adding a binary PCD writer, `binary_compressed` LZF support,
  or radii/intensity field writers in this slice.
- Changing the existing `LoadPCD` reader signature or
  behavior.
- Mixing mechanical legacy deletion with semantic IO
  implementation.
- Promoting arbitrary user-defined per-point property
  serialization in this slice.

## Completion
- Completed: 2026-05-10.
- Status: done.
- Branch: `claude/setup-agentic-workflow-rHAVf`.
- Implementation commit: to be set after commit.
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` —
    no layering violations; `geometry` imports remain
    `geometry -> core` only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (144 task files validated).
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken
    relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — 420 modules;
    no diff (the new exported function lives inside the existing
    `Geometry.PointCloud.IO` module surface and the inventory
    tracks modules, not individual functions).
- CPU build/test gate not exercised: this container lacks
  `clang-20` and `clang-scan-deps` 20+, which the `ci` preset
  hard-codes. `cmake --preset ci` therefore fails at the compiler
  detection step, mirroring the constraint already recorded by
  `GEOIO-002L`/`GEOIO-002M`/`GEOIO-002N` and earlier slices.
  Build verification needs to be re-run on a CI host with the
  correct toolchain prior to merge.
- Notes:
  - `Geometry::PointCloudIO::WritePCD` is exported from
    `src/geometry/Geometry.PointCloud.IO.cppm` and implemented in
    `src/geometry/Geometry.PointCloud.IO.cpp`. It reuses the
    existing `PointCloudIOWriteStatus` enum.
  - The on-disk encoding is ASCII text. The header emits
    `# .PCD v0.7`, `VERSION 0.7`, `FIELDS x y z` (extending with
    `normal_x normal_y normal_z` when the cloud has matching-size
    normals, and `r g b` when it has matching-size colors), the
    matching `SIZE`/`TYPE`/`COUNT` lines using `4`/`F`/`1`,
    `WIDTH <N>`, `HEIGHT 1`, `VIEWPOINT 0 0 0 1 0 0 0`,
    `POINTS <N>`, and `DATA ascii`. Each point line is
    `x y z[ nx ny nz][ r g b]` formatted with `%.6f` and
    terminated by `\n`.
  - Color channels are clamped to `[0, 1]` before serialization,
    which keeps the round-trip inside `NormalizeColorChannel`'s
    no-op branch in `LoadPCD` (values `> 1.0` get divided by
    `255.0`; values in `[0, 1]` are clamped only).
  - `Cloud::HasRadii()` is intentionally ignored: the writer
    emits only the `x/y/z`, optional normals, and optional RGB
    fields the reader recognizes, so radii are not encoded.
  - Empty clouds (`Cloud::IsEmpty()`) yield `EmptyCloud`. Empty
    `absolute_path` and paths under non-existent directories
    yield `InvalidPath` (the `std::ofstream` open fails).
  - Tests in `tests/unit/geometry/Test.GeometryIO.cpp`
    (`GeometryIO_PointCloudIO`) add `WritesPCDPositionsOnly`,
    `WritesPCDWithNormalsAndColorsRoundTrips`,
    `WritesPCDIgnoresRadii`, `WritePCDRejectsEmptyCloud`, and
    `WritePCDRejectsBadPath`.
  - `docs/migration/nonlegacy-parity-matrix.md` updates the
    existing `OBJ/PLY/STL exporters` row to record the new
    geometry-owned ASCII PCD writer.
  - Remaining `GEOIO-002` scope (binary PCD writer; granular
    reader-side `MeshIOReadStatus`/`PointCloudIOReadStatus`
    diagnostics; OBJ ASCII parity hardening; packed-`rgb`/`rgba`
    PCD plus `binary_compressed` LZF decompression; binary STL
    mesh writer) stays tracked under the parent backlog task
    `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
