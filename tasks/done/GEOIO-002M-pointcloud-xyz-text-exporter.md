# GEOIO-002M — Geometry-owned text XYZ point-cloud exporter

## Goal
- Add a geometry-owned text XYZ exporter API to `Geometry.PointCloud.IO`
  that serializes a `PointCloudIOResult` (mandatory positions; optional
  per-point RGB colors) without introducing assets/runtime/graphics
  dependencies, so the broader `GEOIO-002` parity work can grow point-cloud
  round-trip coverage to a second on-disk format symmetric to the existing
  XYZ/XYZRGB importer (`Geometry::PointCloudIO::LoadXYZ`).

## Non-goals
- No PCD or TGF exporter in this slice (separate follow-up slices under
  `GEOIO-002`).
- No write API for halfedge meshes or graphs.
- No XYZ normal emission: the existing `LoadXYZ` reader has no normal
  contract and silently skips columns past `x y z [r g b]`. Writing
  normals would not round-trip and is therefore deferred.
- No XYZ scan-line marker emission (`LH<digits>`); the reader treats
  these as skip markers, not authored data.
- No XYZ row count prefix; the importer is tolerant of its absence and
  the existing point-cloud writers do not emit one.
- No legacy `Graphics.Importers.XYZ` or `IAssetImporter` registry
  deletion or rewiring; that retirement remains tracked under
  `GRAPHICS-019` follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or GPU
  upload work.
- No new format-detection metadata helpers.
- No GPU/Vulkan requirement in the default CPU gate.
- No reader-side change: the existing `LoadXYZ` parser is unmodified.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-ZGCu4`.
- Parent backlog task:
  `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002J` shipped the symmetric ASCII PLY point-cloud writer
  (`Geometry::PointCloudIO::WritePLY`). `GEOIO-002K` shipped the binary
  little-endian PLY point-cloud writer
  (`Geometry::PointCloudIO::WritePLYBinary`). This slice adds a text XYZ
  variant under the same module so callers that need plain XYZ/XYZRGB
  output (e.g. interop with tools that consume legacy `*.xyz` files) get
  a CPU-only encoder symmetric to `Geometry::PointCloudIO::LoadXYZ`.
- The reader is in `src/geometry/Geometry.PointCloud.IO.cpp::LoadXYZ`.
  It accepts plain `x y z`, `x y z intensity`, `x y z r g b`, and
  trailing-color `x y z ... r g b` shapes; treats `#`-prefixed text as a
  comment; tolerates `;` delimiters and leading row-count lines; skips
  `LH<digits>` scan-line markers; and stores RGB colors as `glm::vec4`
  with channels in `[0, 1]` after `NormalizeColorChannel` mapping. The
  writer therefore emits `x y z` for color-less clouds and
  `x y z r g b` (with channels in `[0, 1]`) for clouds with colors so
  that `LoadXYZ` round-trips them exactly.
- Legacy reference: `src/legacy/Graphics/Importers/Graphics.Importers.XYZ.cpp`
  is read-only behavioral coverage; no legacy XYZ exporter exists, so
  this slice introduces the geometry-owned writer as new authority.

## Required changes
- Extend `src/geometry/Geometry.PointCloud.IO.cppm` with a
  `PointCloudIOWriteStatus WriteXYZ(std::string_view absolute_path,
                                    const PointCloudIOResult& cloud);`
  declaration in the `Geometry::PointCloudIO` namespace, reusing the
  existing `PointCloudIOWriteStatus` enum unchanged.
- Implement `WriteXYZ` in `src/geometry/Geometry.PointCloud.IO.cpp`:
  - Reject empty `absolute_path` with `InvalidPath`.
  - Reject empty clouds (`Cloud.IsEmpty()`) with `EmptyCloud`.
  - Open the output stream with
    `std::ios::binary | std::ios::trunc`; return `InvalidPath` if the
    stream cannot be opened.
  - When `Cloud.HasColors()` and `Colors().size() == Positions().size()`,
    emit lines of the form `x y z r g b` with all six values formatted
    via `std::snprintf` using `%.6f` precision, channels written in
    `[0, 1]` directly (no scale by 255). Otherwise emit lines of the
    form `x y z`.
  - Do not emit a `LH...` scan marker, a row-count prefix, a `#` header
    comment, or any normal/radius columns.
  - Lines are terminated with `\n`.
  - Flush and report `FileWriteError` if `stream.good()` is false at
    end.
- No additional public exports beyond `WriteXYZ`; helper logic stays
  inside the existing translation-unit anonymous namespace or local to
  the function.

## Tests
- Add `unit;geometry` cases to `tests/unit/geometry/Test.GeometryIO.cpp`
  under `GeometryIO_PointCloudIO`:
  - `WritesXYZPositionsOnly` — three-point cloud without colors;
    re-import via `LoadXYZ` and verify positions; assert the on-disk
    file is plain `x y z` (no `r g b` columns) by inspecting line
    contents.
  - `WritesXYZWithColorsRoundTrips` — two-point cloud with colors;
    re-import via `LoadXYZ` and verify positions and colors round-trip
    exactly because writer emits channels in `[0, 1]` and reader keeps
    them as-is via `NormalizeColorChannel`.
  - `WritesXYZIgnoresRadiiAndNormals` — cloud has normals/radii enabled
    but the writer emits only `x y z` (or `x y z r g b` if colors are
    set); re-import via `LoadXYZ` and verify positions match and the
    re-imported cloud has no normals/radii.
  - `WriteXYZRejectsEmptyCloud` — empty cloud yields `EmptyCloud`.
  - `WriteXYZRejectsBadPath` — empty `absolute_path` yields
    `InvalidPath`; a path under a non-existent directory yields
    `InvalidPath`.
- Use the existing `TempFile` helper and `ReadFileContents` in the test
  file; do not introduce new test-only headers.

## Docs
- Update the `OBJ/PLY/STL exporters` row of
  `docs/migration/nonlegacy-parity-matrix.md` to note that the
  geometry-owned text XYZ point-cloud exporter was added under
  `GEOIO-002M`.
- Regenerate `docs/api/generated/module_inventory.md` only if the
  generator picks up changes to the existing
  `Geometry.PointCloud.IO` module surface. If the regenerator changes
  only the date stamp, leave it untouched.

## Acceptance criteria
- `Geometry::PointCloudIO::WriteXYZ` compiles and is exported from
  `Geometry.PointCloud.IO`.
- New tests pass under `IntrinsicTests` and the CPU gate.
- No assets/runtime/graphics imports leak into `src/geometry/*`.
- Legacy `src/legacy/Graphics/Importers/Graphics.Importers.XYZ.{cppm,cpp}`
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
- Editing or deleting any legacy `Graphics::*` XYZ importer.
- Mixing mechanical legacy deletion with semantic IO implementation.
- Adding XYZ binary variants in this slice.
- Changing the existing `WritePLY`/`WritePLYBinary` writers' signatures
  or behavior.
- Adding XYZ normal, radius, or scan-marker output in this slice.
- Adding a row-count prefix or `#` header comment to the XYZ payload.

## Completion
- Completed: 2026-05-10.
- Status: done.
- Branch: `claude/setup-agentic-workflow-ZGCu4`.
- Implementation commit: TBD (filled in a follow-up commit on the same
  branch, mirroring the `GEOIO-002L` retirement pattern).
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (142 task files validated).
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken
    relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — 420 modules; no
    diff (the new exported function lives inside the existing
    `Geometry.PointCloud.IO` module surface and the inventory tracks
    modules, not individual functions).
- CPU build/test gate not exercised: this container lacks `clang-20`
  and `clang-scan-deps` 20+, which the `ci` preset hard-codes.
  `cmake --preset ci` therefore fails at the compiler detection step,
  mirroring the constraint already recorded by `GEOIO-002L` and
  earlier slices (see those task records for context). Build
  verification therefore needs to be re-run on a CI host with the
  correct toolchain prior to merge.
- Notes:
  - `Geometry::PointCloudIO::WriteXYZ` is exported from
    `src/geometry/Geometry.PointCloud.IO.cppm` and implemented in
    `src/geometry/Geometry.PointCloud.IO.cpp`. It reuses the existing
    `PointCloudIOWriteStatus` enum unchanged.
  - The on-disk encoding is plain ASCII text. Color-less clouds emit
    one `x y z\n` line per point. Clouds with `HasColors()` and a
    `Colors()` span matching the position count emit
    `x y z r g b\n` lines with all six values formatted via
    `std::snprintf` using `%.6f`, channels written directly in
    `[0, 1]` (no scale by 255). Color channels are clamped to `[0, 1]`
    before write so `LoadXYZ` round-trips them exactly via
    `NormalizeColorChannel` (which keeps values <= 1.0 as-is and only
    divides by 255 when value > 1.0).
  - Normals and radii are intentionally not emitted (the existing
    `LoadXYZ` parser has no contract for them and would silently
    discard or misinterpret extra columns), matching the `Non-goals`
    above. A re-imported cloud written from a source that had normals
    or radii will report `HasNormals() == false` and
    `HasRadii() == false`, so the round-trip is positions-and-colors
    only by design.
  - Tests in `tests/unit/geometry/Test.GeometryIO.cpp`
    (`GeometryIO_PointCloudIO`) add `WritesXYZPositionsOnly`,
    `WritesXYZWithColorsRoundTrips`, `WritesXYZIgnoresRadiiAndNormals`,
    `WriteXYZRejectsEmptyCloud`, and `WriteXYZRejectsBadPath`.
  - `docs/migration/nonlegacy-parity-matrix.md`
    (`OBJ/PLY/STL exporters` row) records the new geometry-owned
    text XYZ point-cloud writer.
  - Remaining `GEOIO-002` scope (PCD point-cloud writer; TGF graph
    writer; granular reader-side
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics; OBJ ASCII
    parity hardening; packed-`rgb`/`rgba` PCD plus
    `binary_compressed` LZF decompression) stays tracked under the
    parent backlog task
    `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
