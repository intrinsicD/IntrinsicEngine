# GRAPHICS-019 — Legacy graphics IO ownership boundaries
## Goal
- Split legacy graphics import/export/model-loader feature coverage into architecture-compliant assets, geometry, runtime, and graphics-asset ownership tasks instead of reintroducing file IO into `src/graphics`.
## Non-goals
- No importer/exporter implementation in this task.
- No renderer pass work.
- No migration of graphics-layer behavior into the wrong owner.
- No deletion or mechanical movement of legacy source.
## Context
- Status: completed planning task.
- Owner: planning across `assets`, `geometry`, `runtime`, `graphics/assets`, and `graphics/renderer` boundaries.
- Legacy `src/legacy/Graphics/Importers`, `Exporters`, `ModelLoader`, `Model`, `TextureLoader`, and `Graphics.IORegistry` modules are adjacent to rendering but do not belong as live file IO inside final graphics layers.
- `GEOIO-001` already promoted minimal geometry-owned loaders for mesh (`OBJ`, `OFF`, `PLY`, `STL`), point cloud (`XYZ`, `PCD`, `PLY`), and graph (`TGF`, edge list) under `src/geometry` without depending on graphics/assets/runtime.
- Promoted `assets` modules own CPU asset identity, payload storage, path lookup, load-state transitions, and events. Runtime owns composition/wiring from assets and geometry payloads into ECS and graphics residency. `src/graphics/assets` owns GPU-side asset cache state only.
## Required changes
- Inventory OBJ, PLY, STL, OFF, PCD, TGF, XYZ, GLTF, texture, and model-loader behavior currently under legacy graphics.
- Create or update follow-up geometry/assets tasks for importer/exporter/model ownership as needed.
- Define the graphics-facing asset/geometry GPU-view handoff required by rendering tasks.
- Update migration parity docs to state that legacy graphics IO is retired through assets/geometry/runtime ownership, not promoted graphics imports.
## Ownership inventory
| Legacy feature/source | Promoted owner decision | Existing promoted coverage | Follow-up gate |
| --- | --- | --- | --- |
| `Graphics.IORegistry` extension dispatch and `Core::IO::IIOBackend` byte transport | `assets` owns asset import/export orchestration and typed payload registration; `core` keeps backend byte transport; geometry owns format decoders/encoders. | `Core.IOBackend` exists; `Asset.LoadPipeline`, `Asset.Service`, `Asset.Registry`, and `Asset.PayloadStore` exist; legacy registry remains only as compatibility/reference behavior. | `ASSETIO-001` defines the promoted asset ingest/export registry contract and adapter seams. |
| OBJ mesh import (`Graphics.Importers.OBJ`) | `geometry` mesh IO. | `Geometry.HalfedgeMesh.IO::LoadOBJ` exists and is unit-tested by `GeometryIO_MeshIO`. | `GEOIO-002` hardens parity, diagnostics, and byte/input adapter coverage. |
| OFF mesh import (`Graphics.Importers.OFF`) | `geometry` mesh IO. | `Geometry.HalfedgeMesh.IO::LoadOFF` exists and is unit-tested by `GeometryIO_MeshIO`. | `GEOIO-002`. |
| STL mesh import (`Graphics.Importers.STL`) | `geometry` mesh IO. | `Geometry.HalfedgeMesh.IO::LoadSTL` exists and is unit-tested by `GeometryIO_MeshIO`. | `GEOIO-002`. |
| PLY mesh and point-cloud import (`Graphics.Importers.PLY`) | `geometry` owns domain-specific mesh/point-cloud PLY readers; `assets`/runtime choose the domain from metadata, import hint, or caller intent. | `Geometry.HalfedgeMesh.IO::LoadPLY` and `Geometry.PointCloud.IO::LoadPLY` exist and are unit-tested. | `GEOIO-002` records domain-selection parity requirements; `ASSETIO-001` records asset-level hint/metadata policy. |
| PCD point-cloud import (`Graphics.Importers.PCD`) | `geometry` point-cloud IO. | `Geometry.PointCloud.IO::LoadPCD` exists and is unit-tested. | `GEOIO-002`. |
| XYZ/PTS/XYZRGB text point-cloud import (`Graphics.Importers.XYZ`) | `geometry` point-cloud IO, with extension-alias policy owned by assets/runtime import routing. | `Geometry.PointCloud.IO::LoadXYZ` exists and is unit-tested for XYZ-style data. | `GEOIO-002` for alias/attribute parity; `ASSETIO-001` for extension routing. |
| TGF graph import (`Graphics.Importers.TGF`) | `geometry` graph IO. | `Geometry.Graph.IO::LoadTGF` exists and is unit-tested. | `GEOIO-002`. |
| GLTF/GLB mesh/material/image import (`Graphics.Importers.GLTF`) | `assets` owns model/scene asset ingest, external-resource resolution, embedded image/material payloads, and typed CPU payload registration; `geometry` owns extracted mesh/primitive CPU geometry; `runtime` owns ECS scene/entity instantiation; `graphics/assets` owns later GPU residency from asset IDs. | No promoted non-legacy GLTF/GLB asset/model ingest contract is proven. Legacy importer decodes meshes, embedded images, and material texture refs through `tinygltf` and `Core::IOBackend`. | `ASSETIO-001` owns GLTF/GLB CPU ingest and runtime handoff scope; `GEOIO-002` covers any reusable geometry primitive extraction helpers needed by that ingest. |
| Texture file decode and GPU upload (`Graphics.TextureLoader`) | `assets` owns CPU texture decode payloads and metadata; `runtime` wires asset events to `graphics/assets`; `graphics/assets::GpuAssetCache` owns GPU upload/residency; `graphics/renderer` and `graphics/vulkan` must not own file decode policy. | `Graphics.GpuAssetCache` exists for GPU-side buffer/texture residency; CPU texture decode ownership is not yet proven outside legacy graphics. | `ASSETIO-001` records CPU texture ingest and runtime-to-`GpuAssetCache` handoff. |
| `Graphics.ModelLoader` and `Graphics.Model` | Retired as a split responsibility: `assets` stores model/scene payload identity, `geometry` stores CPU geometry/collision/bounds-friendly data, `runtime` creates ECS entities/components and requests graphics residency, `graphics/renderer` consumes snapshots/asset IDs only. | `Runtime.RenderExtraction` and `GpuWorld` cover transform/light/visualization sidecars and GPU instance/geometry state, but full mesh/graph/point-cloud asset-to-renderable parity is tracked by downstream rendering residency tasks. | `ASSETIO-001` for model payload/scene ingest; `GRAPHICS-028` for ECS renderable-to-`GpuWorld` residency; `GRAPHICS-020` for final legacy deletion gates. |
| OBJ/PLY/STL exporters (`Graphics.Exporters.*`) | `geometry` owns geometry serialization; asset export orchestration may call geometry encoders but final graphics layers never export model files. | No promoted OBJ/PLY/STL exporter parity is proven in `src/geometry`. | `GEOIO-002` adds promoted exporter contracts/tests. |
## Graphics-facing handoff contract
- Rendering tasks must consume asset IDs, runtime-submitted snapshots, geometry GPU views, or `src/graphics/assets` residency APIs only.
- `src/graphics/renderer` must not parse model/texture files, resolve filesystem paths, own import/export registries, or invoke live `AssetService` traffic.
- `src/graphics/assets` may cache/upload GPU resources keyed by `Assets::AssetId`; runtime is responsible for translating asset events and CPU payload availability into cache requests.
- Runtime is the only layer that may import both asset/geometry CPU authority and graphics residency surfaces for composition.
## Tests
- Run task policy and docs-link checks after adding ownership tasks.
- Run layering checks because this task changes ownership-boundary documentation.
- Future implementation tasks must add parser/exporter unit tests under the owning subsystem.
- `ASSETIO-001` must add asset/runtime integration coverage for import routing, CPU payload registration, texture decode metadata, and runtime-to-GPU-residency handoff without graphics owning file IO.
- `GEOIO-002` must add geometry-owned importer/exporter parity tests without assets/runtime/graphics imports.
## Docs
- Update migration parity docs to state that legacy graphics IO is retired through assets/geometry/runtime ownership, not promoted graphics imports.
- Update rendering backlog links after promoting this task to active/done.
- Add backlog category docs for new assets follow-up tasks.
## Acceptance criteria
- Every legacy graphics IO feature has an owning subsystem decision.
- Rendering tasks depend only on asset IDs, geometry GPU views, runtime-submitted snapshots, or promoted graphics asset residency APIs.
- No final graphics task requires direct model/texture file parsing or exporter ownership.
- Follow-up tasks exist for unproven geometry exporter/import parity and asset model/texture ingest/runtime handoff.
## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```
## Completion
- Completed: 2026-05-07.
- Commit reference: local commit for GRAPHICS-019 planning slice.
- Follow-ups created:
  - `ASSETIO-001` — asset model, texture, and import/export ingest ownership.
  - `GEOIO-002` — geometry IO parity hardening and exporters.
- Notes:
  - Legacy graphics IO/model/texture ownership is recorded as split across `assets`, `geometry`, `runtime`, and `graphics/assets` handoff seams.
  - Final `src/graphics/*` layers remain file-IO-free and parser/exporter-free by policy.
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding direct importer/exporter ownership to `src/graphics`.
- Removing legacy source before `GRAPHICS-020` retirement gates are satisfied.
