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
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-aHpQ0`.
- Parent backlog task: `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
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
- Extend `Geometry.PointCloud.IO.cppm` with a `PointCloudIOWriteStatus`
  enum (`Success`, `EmptyCloud`, `InvalidPath`, `FileWriteError`) and a
  `WritePLY(std::string_view absolute_path, const PointCloudIOResult& cloud)`
  declaration in the `Geometry::PointCloudIO` namespace.
- Implement `WritePLY` in `Geometry.PointCloud.IO.cpp`:
  - Reject empty `absolute_path` with `InvalidPath`.
  - Reject empty clouds (`Cloud.IsEmpty()`) with `EmptyCloud`.
  - Open the file with `std::ofstream(... std::ios::binary | std::ios::trunc)`;
    return `InvalidPath` if the stream cannot be opened.
  - Emit ASCII PLY 1.0 header with `format ascii 1.0`, a comment line,
    `element vertex N`, `property float x/y/z`, optional
    `property float nx/ny/nz` when `HasNormals()`, optional
    `property uchar red/green/blue` when `HasColors()` (clamp 0..1 channels
    to `0..255` integers), optional `property float radius` when
    `HasRadii()`, then `end_header`.
  - Write one point per line in declared property order using
    `snprintf` for deterministic float formatting; map normal/color/radius
    spans by index.
  - Flush and report `FileWriteError` if `stream.good()` is false at end.
- No additional public exports from `Geometry.PointCloud.IO.cppm`; all helper
  logic stays inside the existing translation-unit anonymous namespace.

## Tests
- Add `unit;geometry` cases to `tests/unit/geometry/Test.GeometryIO.cpp` under
  `GeometryIO_PointCloudIO`:
  - `WritesPLYPointCloud` — three-point cloud, round-trip via `LoadPLY` and
    verify positions match.
  - `WritesPLYPointCloudWithNormalsAndColorsAndRadii` — round-trip preserves
    normals, color channels (255-mapped), and radius scalar.
  - `WritePLYPointCloudRejectsEmptyCloud` — empty cloud yields
    `PointCloudIOWriteStatus::EmptyCloud`.
  - `WritePLYPointCloudRejectsBadPath` — empty `absolute_path` yields
    `PointCloudIOWriteStatus::InvalidPath`; non-creatable directory path
    yields `InvalidPath`.
- Use `std::filesystem::temp_directory_path()` plus a unique file name
  pattern (mirroring existing mesh writer tests) for round-trip targets;
  clean up via `std::filesystem::remove`.

## Docs
- Update the `geometry` row of `docs/migration/nonlegacy-parity-matrix.md` to
  note that point-cloud PLY ASCII export is now geometry-owned.
- No module surface inventory regeneration is required because no new
  module is introduced.

## Acceptance criteria
- `Geometry::PointCloudIO::WritePLY` compiles and is exported from
  `Geometry.PointCloud.IO`.
- New tests pass under `IntrinsicTests` and the CPU gate.
- No assets/runtime/graphics imports leak into `src/geometry/*`.
- Legacy `src/legacy/Graphics/Exporters/Graphics.Exporters.PLY.{cppm,cpp}`
  remains untouched (reference only).
- Parity matrix row reflects the new exporter ownership.

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
