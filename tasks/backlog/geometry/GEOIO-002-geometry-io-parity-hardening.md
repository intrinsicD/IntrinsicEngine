# GEOIO-002 — Geometry IO parity hardening and exporters
## Goal
- Harden promoted geometry-owned IO parity for legacy graphics import/export coverage and add geometry-owned OBJ/PLY/STL exporter contracts without introducing assets/runtime/graphics dependencies into `src/geometry`.
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
- [ ] Add or harden geometry-owned importer parity for OBJ, OFF, STL, mesh PLY, point-cloud PLY, PCD, XYZ/PTS/XYZRGB-style text point clouds, and TGF.
- [ ] Record or implement domain-selection metadata needed by asset/runtime import routing without making geometry depend on assets/runtime.
- [ ] Add geometry-owned OBJ, PLY, and STL exporter APIs for supported mesh/point-cloud data shapes.
- [ ] Add deterministic diagnostics for malformed headers, unsupported properties, invalid topology, out-of-range indices, unsupported binary/ASCII variants, and partial/empty data.
- [ ] Preserve public geometry API boundaries and update generated module inventory if module surfaces change.
## Tests
- [ ] Add `unit;geometry` importer/exporter round-trip and negative-path tests under `tests/unit/geometry`.
- [ ] Cover OBJ/OFF/STL/PLY mesh import, PLY/PCD/XYZ point-cloud import, TGF graph import, and OBJ/PLY/STL export behavior.
- [ ] Add regression cases for extension aliases or domain ambiguity only as CPU-only metadata tests.
## Docs
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` and `docs/api/generated/module_inventory.md` when public module surfaces change.
- [ ] Cross-link `GRAPHICS-019` and `ASSETIO-001` from completion notes.
## Progress notes
- 2026-05-19 (`GEOIO-002AH` slice): added geometry-owned ambiguous import/export domain metadata helpers for extension routing (PLY requires an explicit caller-side domain hint); remaining importer/exporter parity items stay open.
## Acceptance criteria
- [ ] Geometry IO parity is sufficient for asset/runtime import routing to stop depending on legacy graphics importers/exporters for supported geometry formats.
- [ ] `src/geometry/*` imports only allowed lower-layer dependencies and remains independent of assets/runtime/graphics.
- [ ] Exporter APIs and tests cover OBJ, PLY, and STL or explicitly document unsupported data shapes and failure diagnostics.
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
