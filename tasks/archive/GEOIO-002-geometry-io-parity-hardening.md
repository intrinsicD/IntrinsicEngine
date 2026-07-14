# GEOIO-002 — Geometry IO parity hardening and exporters
## Goal
- Harden promoted geometry-owned IO parity for legacy graphics import/export coverage and add geometry-owned OBJ/PLY/STL exporter contracts without introducing assets/runtime/graphics dependencies into `src/geometry`.
## Status
- Status: done.
- Completed: 2026-05-21.
- Commit: TBD (filled in when the landing commit is created).
- PR: TBD (filled in when the landing PR is opened).
- Owner/agent: active session on `main` working tree.
- Follow-up owner: `ASSETIO-001` owns asset/runtime byte transport, import hints, typed payload registration, and ambiguous-extension policy; no further geometry-owned `GEOIO-002` follow-up is required before asset/runtime routing.
## Non-goals
- No asset registry, runtime scene ingestion, ECS construction, or GPU upload work.
- No GLTF/GLB scene/model ownership except reusable geometry primitive extraction helpers explicitly needed by `ASSETIO-001`.
- No file IO ownership in `src/graphics/*`.
## Context
- Owner: `geometry -> core` only.
- `GEOIO-001` added minimal `Geometry.HalfedgeMesh.IO`, `Geometry.PointCloud.IO`, and `Geometry.Graph.IO` loaders for OBJ/OFF/PLY/STL, XYZ/PCD/PLY, and TGF/edge-list.
- `GRAPHICS-019` assigns legacy `Graphics.Importers.*` and `Graphics.Exporters.*` retirement to promoted geometry/assets owners.
- Legacy exporters exist for OBJ, PLY, and STL under `src/legacy/Graphics/Exporters` and remain behavioral references only.
## Required changes
- [x] Add or harden geometry-owned importer parity for OBJ, OFF, STL, mesh PLY, point-cloud PLY, PCD, XYZ/PTS/XYZRGB-style text point clouds, and TGF.
- [x] Record or implement domain-selection metadata needed by asset/runtime import routing without making geometry depend on assets/runtime.
- [x] Add geometry-owned OBJ, PLY, and STL exporter APIs for supported mesh/point-cloud data shapes.
- [x] Add deterministic diagnostics for malformed headers, unsupported properties, invalid topology, out-of-range indices, unsupported binary/ASCII variants, and partial/empty data.
- [x] Preserve public geometry API boundaries and update generated module inventory if module surfaces change.
## Tests
- [x] Add `unit;geometry` importer/exporter round-trip and negative-path tests under `tests/unit/geometry`.
- [x] Cover OBJ/OFF/STL/PLY mesh import, PLY/PCD/XYZ point-cloud import, TGF graph import, and OBJ/PLY/STL export behavior.
- [x] Add regression cases for extension aliases or domain ambiguity only as CPU-only metadata tests.
## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` and `docs/api/generated/module_inventory.md` when public module surfaces change.
- [x] Cross-link `GRAPHICS-019` and `ASSETIO-001` from completion notes.
## Slice plan
- **GEOIO-002AI — mesh exporter non-finite rejection.** Harden geometry-owned OBJ/PLY/STL mesh exporters so serialized positions and supported vertex attributes reject `NaN`/`Inf` with deterministic `FileWriteError` results. Tests: `GeometryIO_MeshIO.MeshExportersRejectNonFinitePositions`, `GeometryIO_MeshIO.MeshExportersRejectNonFiniteVertexColors`, `GeometryIO_MeshIO.MeshExportersRejectNonFiniteVertexNormals`, and `GeometryIO_MeshIO.MeshExportersRejectNonFiniteVertexTexcoords`. Defers remaining importer parity and asset/runtime routing to later `GEOIO-002`/`ASSETIO-001` slices.
- **GEOIO-002AJ — point-cloud exporter non-finite rejection.** Harden geometry-owned PLY/XYZ/PCD point-cloud exporters so serialized positions and attributes reject `NaN`/`Inf` with deterministic `FileWriteError` results; radii validation is PLY-only because XYZ/PCD do not serialize radii. Tests: `GeometryIO_PointCloudIO.PointCloudExportersRejectNonFinitePositions`, `GeometryIO_PointCloudIO.PointCloudExportersRejectNonFiniteColors`, `GeometryIO_PointCloudIO.PointCloudExportersRejectNonFiniteNormalsWhenSerialized`, and `GeometryIO_PointCloudIO.PointCloudPLYExportersRejectNonFiniteRadii`. Defers non-serialized property validation and asset/runtime routing to later slices.
- **GEOIO-002AK — graph exporter non-finite rejection.** Harden TGF and edge-list graph exporters so serialized vertex positions and edge weights reject `NaN`/`Inf` with deterministic `FileWriteError` results. Tests: `GeometryIO_GraphIO.WriteTGFRejectsNonFiniteVertexPosition` and `GeometryIO_GraphIO.GraphExportersRejectNonFiniteEdgeWeight`. Defers graph importer hardening to `GEOIO-002AL`.
- **GEOIO-002AL — graph importer non-finite rejection.** Harden TGF and edge-list graph importers so parsed vertex positions and edge weights reject `NaN`/`Inf` with deterministic `InvalidFormat` results. Tests: `GeometryIO_GraphIO.LoadTGFRejectsNonFiniteVertexPosition` and `GeometryIO_GraphIO.GraphImportersRejectNonFiniteEdgeWeight`. Defers broader geometry IO parity closure to later `GEOIO-002` slices.
- **GEOIO-002AM — mesh importer non-finite rejection.** Harden OBJ/OFF/STL/mesh-PLY importers so parsed positions and supported vertex attributes reject `NaN`/`Inf` with deterministic `InvalidFormat` results. Tests: `GeometryIO_MeshIO.MeshImportersRejectNonFinitePositions`, `GeometryIO_MeshIO.MeshImportersRejectNonFiniteVertexColors`, `GeometryIO_MeshIO.MeshImportersRejectNonFiniteVertexNormals`, and `GeometryIO_MeshIO.MeshImportersRejectNonFiniteVertexTexcoords`. Defers point-cloud importer hardening to `GEOIO-002AN`.
- **GEOIO-002AN — point-cloud importer finite-value diagnostics.** Reject non-finite XYZ/PTS/XYZRGB/PCD/point-cloud-PLY positions and serialized attributes with deterministic `InvalidFormat` diagnostics. Tests: `GeometryIO_PointCloudIO.PointCloudImportersRejectNonFinitePositions`, `GeometryIO_PointCloudIO.PointCloudImportersRejectNonFiniteColors`, and `GeometryIO_PointCloudIO.PointCloudImportersRejectNonFiniteNormals`. Preserve existing soft-skip behavior only where the format contract already treats a row as non-point data. Defers malformed-header and unsupported-variant audit to `GEOIO-002AO`.
- **GEOIO-002AO — malformed-header and unsupported-variant audit.** Audit and close remaining malformed header, unsupported scalar/list property, unsupported ASCII/binary variant, and truncated payload cases for OBJ/OFF/STL/PLY/XYZ/PCD/TGF. This slice hardened mesh/point-cloud PLY `format` headers to require version `1.0` and hardened PCD headers to reject unsupported scalar size/type layouts. Tests: `GeometryIO_MeshIO.LoadPLYRejectsUnsupportedFormatVersion`, `GeometryIO_PointCloudIO.LoadPLYPointCloudRejectsUnsupportedFormatVersion`, and `GeometryIO_PointCloudIO.LoadPCDRejectsUnsupportedScalarFieldLayouts`. Defers invalid topology/index parity audit to `GEOIO-002AP`.
- **GEOIO-002AP — invalid topology/index parity audit.** Audit and close invalid topology/index behavior for OBJ/OFF/PLY/STL faces, graph endpoints, empty/partial topology, and out-of-range references. This slice hardened OBJ/OFF/mesh-PLY importers to reject faces with duplicate vertex indices and STL importers to reject triangles with duplicate vertex positions. Test: `GeometryIO_MeshIO.MeshImportersRejectDuplicateVertexFaces`. Defers asset/runtime routing readiness closure to `GEOIO-002AQ`.
- **GEOIO-002AQ — asset/runtime routing readiness closure.** Confirm geometry IO parity is sufficient for `ASSETIO-001` and asset/runtime routing, update final notes/docs, and decide whether `GEOIO-002` can retire or needs a follow-up task. This slice added `GeometryIO_Metadata.ReportsAssetRoutingReadinessForAllPromotedFormats`, confirmed geometry-owned codecs and domain metadata are sufficient for downstream routing, and records `ASSETIO-001` as the remaining owner for asset/runtime orchestration.
## Progress notes
- 2026-05-19 (`GEOIO-002AH` slice): added geometry-owned ambiguous import/export domain metadata helpers for extension routing (PLY requires an explicit caller-side domain hint); remaining importer/exporter parity items stay open.
- 2026-05-21 (`GEOIO-002AI` slice): hardened mesh OBJ/PLY/STL exporters to reject non-finite positions and supported vertex attributes with deterministic `FileWriteError` diagnostics instead of serializing `nan`/`inf` payloads; broader parity items stay open.
- 2026-05-21 (`GEOIO-002AJ` slice): hardened point-cloud PLY/XYZ/PCD exporters to reject non-finite positions and serialized attributes with deterministic `FileWriteError` diagnostics; PLY-only radii validation follows the formats that serialize radii.
- 2026-05-21 (`GEOIO-002AK` slice): hardened graph TGF/edge-list exporters to reject non-finite serialized vertex positions and edge weights with deterministic `FileWriteError` diagnostics.
- 2026-05-21 (`GEOIO-002AL` slice): hardened graph TGF/edge-list importers to reject non-finite parsed vertex positions and edge weights with deterministic `InvalidFormat` diagnostics.
- 2026-05-21 (`GEOIO-002AM` slice): hardened OBJ/OFF/STL/mesh-PLY importers to reject non-finite parsed positions and supported vertex attributes with deterministic `InvalidFormat` diagnostics; point-cloud importer finite diagnostics stay open for `GEOIO-002AN`.
- 2026-05-21 (`GEOIO-002AN` slice): hardened XYZ/PTS/XYZRGB, PCD, and point-cloud PLY importers to reject non-finite parsed positions and supported serialized attributes with deterministic `InvalidFormat` diagnostics; malformed-header and unsupported-variant audit stays open for `GEOIO-002AO`.
- 2026-05-21 (`GEOIO-002AO` slice): hardened mesh/point-cloud PLY headers to reject missing or unsupported `format` versions and hardened PCD headers to reject unsupported scalar size/type layouts with deterministic `InvalidFormat` diagnostics; invalid topology/index parity audit stays open for `GEOIO-002AP`.
- 2026-05-21 (`GEOIO-002AP` slice): hardened OBJ/OFF/mesh-PLY importers to reject duplicate vertex indices in faces and STL importers to reject duplicate vertex positions in triangles with deterministic `InvalidFormat` diagnostics; asset/runtime routing readiness closure stays open for `GEOIO-002AQ`.
- 2026-05-21 (`GEOIO-002AQ` slice): added full promoted-format domain/binary/ambiguity metadata coverage for asset routing readiness and confirmed no further geometry-owned follow-up is required before `ASSETIO-001`; asset/runtime still own byte transport, import hints, payload registration, and ambiguous-extension policy.
## Acceptance criteria
- [x] Geometry IO parity is sufficient for asset/runtime import routing to stop depending on legacy graphics importers/exporters for supported geometry formats.
- [x] `src/geometry/*` imports only allowed lower-layer dependencies and remains independent of assets/runtime/graphics.
- [x] Exporter APIs and tests cover OBJ, PLY, and STL or explicitly document unsupported data shapes and failure diagnostics.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```
## Forbidden changes
- Adding assets/runtime/graphics/RHI imports to `src/geometry/*`.
- Copying legacy graphics registry ownership into geometry.
- Mixing mechanical legacy deletion with semantic IO implementation.
