# GEOIO-002J — Geometry-owned ASCII PLY point-cloud exporter

## Goal
- Add a geometry-owned ASCII PLY exporter API to `Geometry.PointCloud.IO`
  that serializes a `PointCloudIOResult` (mandatory positions; optional
  per-point normals, RGB colors, and radii) without introducing
  assets/runtime/graphics dependencies, so the broader `GEOIO-002` parity
  work can finish point-cloud round-trip coverage symmetric to the existing
  mesh writers.

## Non-goals
- No binary PLY point-cloud exporter in this slice (defer to a follow-up
  slice under `GEOIO-002`).
- No PCD or XYZ exporter in this slice.
- No write API for halfedge meshes, graphs, or any non-point-cloud data.
- No legacy `Graphics::PLYExporter` or `IAssetExporter` registry deletion
  or rewiring; that retirement remains tracked under `GRAPHICS-019`
  follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or GPU
  upload work.
- No new format-detection metadata helpers.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-aHpQ0` (implementation),
  `claude/setup-agentic-workflow-58MLx` (retirement).
- Parent task: `tasks/done/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-001`/`GEOIO-002F`/`GEOIO-002G`/`GEOIO-002H` cover ASCII+binary
  PLY/PCD/XYZ point-cloud importers in
  `src/geometry/Geometry.PointCloud.IO.{cppm,cpp}`. There is currently no
  symmetric write path for any point-cloud format. The mesh-side exporter
  pattern is established by `GEOIO-002A/B/C` (`MeshIOWriteStatus` enum and
  `WriteOBJ`/`WritePLY`/`WriteSTL`), so this slice mirrors that pattern.
- Public mesh PLY writer in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp::WritePLY` is the behavioral
  reference. Cloud accessors used here come from
  `src/geometry/Geometry.PointCloud.cppm` (`Positions()`, `HasNormals()`,
  `Normals()`, `HasColors()`, `Colors()`, `HasRadii()`, `Radii()`).

## Required changes
- [x] Extend `Geometry.PointCloud.IO.cppm` with a `PointCloudIOWriteStatus`
  enum (`Success`, `EmptyCloud`, `InvalidPath`, `FileWriteError`) and a
  `WritePLY(std::string_view absolute_path, const PointCloudIOResult& cloud)`
  declaration in the `Geometry::PointCloudIO` namespace.
- [x] Implement `WritePLY` in `Geometry.PointCloud.IO.cpp`:
  - [x] Reject empty `absolute_path` with `InvalidPath`.
  - [x] Reject empty clouds (`Cloud.IsEmpty()`) with `EmptyCloud`.
  - [x] Open the file with `std::ofstream(... std::ios::binary | std::ios::trunc)`;
    return `InvalidPath` if the stream cannot be opened.
  - [x] Emit ASCII PLY 1.0 header with `format ascii 1.0`, a comment line,
    `element vertex N`, `property float x/y/z`, optional
    `property float nx/ny/nz` when `HasNormals()`, optional
    `property uchar red/green/blue` when `HasColors()` (clamp 0..1 channels
    to `0..255` integers), optional `property float radius` when
    `HasRadii()`, then `end_header`.
  - [x] Write one point per line in declared property order using
    `snprintf` for deterministic float formatting; map normal/color/radius
    spans by index.
  - [x] Flush and report `FileWriteError` if `stream.good()` is false at end.
- [x] No additional public exports from `Geometry.PointCloud.IO.cppm`; all helper
  logic stays inside the existing translation-unit anonymous namespace.

## Tests
- [x] Add `unit;geometry` cases to `tests/unit/geometry/Test.GeometryIO.cpp` under
  `GeometryIO_PointCloudIO`:
  - [x] `WritesPLYPointCloud` — three-point cloud, round-trip via `LoadPLY` and
    verify positions match.
  - [x] `WritesPLYPointCloudWithNormalsAndColorsAndRadii` — round-trip preserves
    normals, color channels (255-mapped), and radius scalar.
  - [x] `WritePLYPointCloudRejectsEmptyCloud` — empty cloud yields
    `PointCloudIOWriteStatus::EmptyCloud`.
  - [x] `WritePLYPointCloudRejectsBadPath` — empty `absolute_path` yields
    `PointCloudIOWriteStatus::InvalidPath`; non-creatable directory path
    yields `InvalidPath`.
- [x] Use `std::filesystem::temp_directory_path()` plus a unique file name
  pattern (mirroring existing mesh writer tests) for round-trip targets;
  clean up via `std::filesystem::remove`.

## Docs
- [x] Update the `geometry` row of `docs/migration/nonlegacy-parity-matrix.md` to
  note that point-cloud PLY ASCII export is now geometry-owned.
- [x] No module surface inventory regeneration is required because no new
  module is introduced.

## Acceptance criteria
- [x] `Geometry::PointCloudIO::WritePLY` compiles and is exported from
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
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding assets/runtime/graphics/RHI imports to `src/geometry/*`.
- Editing or deleting legacy `Graphics::PLYExporter`.
- Mixing mechanical legacy deletion with semantic IO implementation.
- Adding binary PLY point-cloud emission in this slice (separate follow-up).
- Adding PCD or XYZ exporters in this slice.

## Completion
- Completed: 2026-05-10.
- Status: done.
- Implementation commit: `df157ae`
  (`GEOIO-002J: add geometry-owned ASCII PLY point-cloud exporter`).
- Retired in a follow-up commit on
  `claude/setup-agentic-workflow-58MLx`. The implementation commit on
  `claude/setup-agentic-workflow-aHpQ0` (PR #790) merged without moving
  the task file out of `tasks/backlog/geometry/`; this retirement closes
  that gap with no source/test changes.
- Verified in this session:
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings after this retirement (139 task files validated).
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken links.
- Build/CTest gate not re-run in this retirement: no source/test files
  change. The implementation commit `df157ae` already recorded a clean
  focused gate (`ctest -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine'`
  passing 70/70) on the originating session host.
- Notes:
  - `Geometry::PointCloudIO::WritePLY` plus
    `PointCloudIOWriteStatus { Success, EmptyCloud, InvalidPath, FileWriteError }`
    are exported from `src/geometry/Geometry.PointCloud.IO.cppm` and
    implemented in `src/geometry/Geometry.PointCloud.IO.cpp`.
  - Coverage in `tests/unit/geometry/Test.GeometryIO.cpp` (`GeometryIO_PointCloudIO`)
    adds `WritesPLYPointCloud`,
    `WritesPLYPointCloudWithNormalsAndColorsAndRadii`,
    `WritePLYPointCloudRejectsEmptyCloud`, and
    `WritePLYPointCloudRejectsBadPath`.
  - `docs/migration/nonlegacy-parity-matrix.md` (`GRAPHICS-019` row,
    OBJ/PLY/STL exporters) records the new geometry-owned ASCII PLY
    point-cloud writer.
  - Remaining `GEOIO-002` scope (binary PLY point-cloud writer; PCD and
    XYZ point-cloud writers; TGF graph writer; granular reader-side
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics; OBJ ASCII
    parity hardening; packed-`rgb`/`rgba` PCD plus `binary_compressed`
    LZF decompression) stays tracked under the parent backlog task
    `tasks/done/GEOIO-002-geometry-io-parity-hardening.md`.
