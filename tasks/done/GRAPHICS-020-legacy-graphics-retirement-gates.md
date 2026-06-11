# GRAPHICS-020 — Legacy graphics retirement gates
## Goal
- Define objective gates for retiring legacy graphics, RHI, and runtime rendering modules after promoted graphics/runtime/assets/geometry parity exists.
## Non-goals
- No deletion of legacy modules in this task.
- No implementation of missing rendering features.
- No untracked compatibility exceptions.
## Context
- Status: completed planning task.
- Owner: graphics migration planning with runtime/assets/geometry cross-links.
- `src/legacy` may contain transitional exceptions only when tracked, time-bounded, and paired with removal tasks.
- The inventory source is `docs/api/generated/module_inventory.md` plus `src/legacy/Graphics`, `src/legacy/RHI`, and render-adjacent `src/legacy/Runtime` module files as of 2026-05-07.
- The final deletion/isolation slice must run only after every row below reaches `ready`; until then retained legacy modules are compatibility/reference code only and must not be imported by promoted final layers.
## Required changes
- [x] Map each retained legacy graphics-related module to a promoted owner, parity task, test gate, or explicit retirement decision.
- [x] Track compatibility shims and removal follow-ups.
- [x] Identify final blockers for deleting or isolating legacy render paths.
## Retirement gate matrix
| Legacy module family | Modules covered | Promoted owner / retirement decision | Gate evidence required | Current status | Removal follow-up |
| --- | --- | --- | --- | --- | --- |
| Graphics component/data-authority bundle | `Graphics.Components`, `Graphics.Components.Core`, `DataAuthority`, `DirtyTag`, `Mesh`, `Graph`, `PointCloud`, `Surface`, `Line`, `Point`, `MeshVertexView`, `MeshEdgeView`, `MeshCollider`, `PointKDTree`, `PrimitiveBVH` | Split to `ecs` CPU-only render/geometry markers, `geometry` CPU data, `runtime` extraction sidecars, and `graphics/renderer` packet/component value types. No GPU handles in canonical ECS components. | `GRAPHICS-016` runtime extraction handoff, completed `GRAPHICS-028` renderable residency bridge contract, geometry/asset follow-ups from `GRAPHICS-019`, and contract tests for layer boundaries and lifecycle. | blocked: `ASSETIO-001`, `GEOIO-002`, and downstream runtime residency implementation parity are not complete. | `GRAPHICS-020` final deletion slice after blockers close. |
| Graphics lifecycle/GPU-scene sync systems | `Graphics.Systems.GPUSceneSync`, `GraphLifecycle`, `MeshRendererLifecycle`, `MeshViewLifecycle`, `PointCloudLifecycle`, `PrimitiveBVHBuild`, `PropertySetDirtySync`, `Graphics.LifecycleUtils` | Runtime owns live ECS/asset/geometry queries and sidecar caches; graphics consumes snapshots and owns GPU resources. Geometry owns CPU acceleration data. | `GRAPHICS-016`, `GRAPHICS-028`, `GRAPHICS-004`, `GRAPHICS-005`, `GRAPHICS-007`, `GRAPHICS-014Q`, plus runtime/graphics contract tests for dirty-domain drain, lifecycle deletion, and asset-to-residency handoff. | blocked: full mesh/graph/point-cloud lifecycle parity and dirty-property routing remain downstream. | `GRAPHICS-020` final deletion slice after runtime residency tests exist. |
| GPU scene/world, global resources, and retained draw state | `Graphics.GPUScene`, `Graphics.GlobalResources`, `Graphics.GpuColor`, relevant parts of `Graphics.Geometry` | `graphics/renderer` owns `GpuWorld`, scene-table buffers, culling buckets, material/light/selection state; `graphics/rhi` owns GPU resource handles. | `GRAPHICS-004`, `GRAPHICS-005`, `GRAPHICS-007`, `GRAPHICS-012`, `GRAPHICS-015`, `GRAPHICS-016`, completed `GRAPHICS-028` residency contract, and default CPU/null renderer extraction tests; GPU/Vulkan parity remains opt-in. | partial: promoted allocation/lifetime exists, but full geometry/residency implementation parity remains downstream. | `GRAPHICS-020` final deletion slice after runtime residency implementation parity lands. |
| Render graph, frame recipe, render driver/pipeline/path | `Graphics.RenderGraph`, `Graphics.RenderPipeline`, `Graphics.RenderPath`, `Graphics.RenderDriver`, `Graphics.FeatureCatalog` | `graphics/framegraph` owns graph compile/execute/resource lifetime; `graphics/renderer` owns frame recipe and renderer orchestration; feature-catalog behavior is either retired or replaced by explicit task/registry docs. | `GRAPHICS-002`, `GRAPHICS-003`, `GRAPHICS-022`, renderer/framegraph contract tests, and no promoted imports of legacy graph/pipeline modules. | mostly gated by done tasks; deletion still waits for final import scan and all pass rows below. | `GRAPHICS-020` final deletion slice with import scan. |
| Pass modules and render buckets | `Graphics.Passes.Surface`, `Line`, `Point`, `Shadow`, `Picking`, `SelectionOutline`, `SelectionOutlineSettings`, `DebugView`, `HtexPatchPreview`, `Composition`, `ImGui`, `PostProcess`, `PostProcess.Bloom`, `FXAA`, `Histogram`, `SMAA`, `ToneMap`, `PostProcessSettings` | `graphics/renderer/Passes` and renderer systems own backend-agnostic pass contracts; runtime/editor/app own producers for UI/debug/visualization/camera data. | `GRAPHICS-008`, `GRAPHICS-009`, `GRAPHICS-010`, `GRAPHICS-012`, `GRAPHICS-013A/B/C`, `GRAPHICS-014`, `GRAPHICS-024`, plus clarification tasks `GRAPHICS-008Q` through `GRAPHICS-014Q`. | partial: core pass contracts are promoted; backend completeness and overlay/presentation adjacency are tracked by existing done tasks and final import scan. | `GRAPHICS-020` final deletion slice after no legacy pass imports remain. |
| Materials, shader/pipeline registries, and hot reload | `Graphics.Material`, `Graphics.MaterialRegistry`, `Graphics.PipelineLibrary`, `Graphics.Pipelines`, `Graphics.ShaderRegistry`, `Graphics.ShaderHotReload` | `graphics/renderer` owns material registry and renderer-facing shader/pipeline policy; `graphics/rhi` owns backend-neutral pipeline objects; `graphics/vulkan` owns Vulkan shader/pipeline implementation details. | `GRAPHICS-006`, `GRAPHICS-006Q`, `GRAPHICS-018`, `GRAPHICS-018Q`, and open `GRAPHICS-023` for shader/material/texture hot reload. | blocked: hot-reload retirement waits on `GRAPHICS-023`. | `GRAPHICS-020` final deletion slice after `GRAPHICS-023` and import scan. |
| Graphics asset, model, texture, import/export IO | `Graphics.IORegistry`, `Graphics.Model`, `Graphics.ModelLoader`, `Graphics.TextureLoader`, `Graphics.Importers.GLTF`, `OBJ`, `OFF`, `PCD`, `PLY`, `STL`, `TGF`, `XYZ`, `Graphics.Exporters.OBJ`, `PLY`, `STL` | `assets` owns CPU ingest/export orchestration and texture/model payload identity; `geometry` owns geometry codecs; `runtime` wires decoded assets to ECS/graphics; `graphics/assets` owns GPU residency only. | `GRAPHICS-019` owner inventory, `ASSETIO-001`, `GEOIO-002`, completed `GRAPHICS-028` residency contract, and asset/runtime/geometry tests. | blocked: follow-up implementation tasks are open. | `GRAPHICS-020` final deletion slice after `ASSETIO-001`, `GEOIO-002`, and runtime residency implementation parity. |
| Visualization/property/debug helpers | `Graphics.Colormap`, `ColorMapper`, `PropertyEnumerator`, `IsolineExtractor`, `VectorFieldManager`, `DebugDraw`, `BVHDebugDraw`, `BoundingDebugDraw`, `ConvexHullDebugDraw`, `KDTreeDebugDraw`, `OctreeDebugDraw`, `SubElementHighlightSettings`, `VisualizationConfig` | `graphics/renderer` owns colormap/materialized visualization packet validation and debug draw consumption; runtime/geometry own property enumeration, isoline/vector-field generation, spatial-debug adapters, and editor/debug producers. | `GRAPHICS-010`, `GRAPHICS-010Q`, `GRAPHICS-011`, `GRAPHICS-011Q`, `GRAPHICS-014`, `GRAPHICS-014Q`, `GRAPHICS-024`, geometry method/helper parity where needed. | partial: promoted packet contracts exist, but producer parity and some geometry helper replacement remain unproven. | `GRAPHICS-020` final deletion slice after producer parity/import scan. |
| Camera, interaction, gizmo, overlay, presentation/editor adjacency | `Graphics.Camera`, `Graphics.Interaction`, `Graphics.TransformGizmo`, `Graphics.OverlayEntityFactory`, `Graphics.Presentation`, presentation/overlay uses of `Graphics.Passes.Composition` and `Graphics.Passes.ImGui` | `runtime`, `platform`, and `app/editor` own input, camera/gizmo mutation, overlay entity lifetime, window/swapchain host state; graphics consumes data-only packets and owns finalizer passes. | `GRAPHICS-017`, `GRAPHICS-017Q`, `GRAPHICS-024`, `GRAPHICS-013CQ`, platform backend tests, and overlay/presentation rows in `docs/migration/nonlegacy-parity-matrix.md`. | blocked/partial: `GRAPHICS-024` records owner decisions; implementation parity still depends on runtime/editor/app follow-ups. | `GRAPHICS-020` final deletion slice after overlay/presentation import scan and runtime/app follow-ups. |
| Legacy RHI backend/resource surface | `RHI`, `RHI.Bindless`, `Buffer`, `CommandContext`, `CommandUtils`, `ComputePipeline`, `Context`, `Descriptors`, `Device`, `Image`, `PersistentDescriptors`, `Pipeline`, `Profiler`, `QueueDomain`, `Renderer`, `SceneInstances`, `Shader`, `StagingBelt`, `Swapchain`, `Texture`, `TextureFwd`, `TextureHandle`, `TextureManager`, `Transfer`, `TransientAllocator`, `Types` | `graphics/rhi` owns backend-neutral handles/managers/commands/transfers; `graphics/vulkan` owns Vulkan implementation; platform owns surface creation. `RHI.SceneInstances` convenience behavior is retired in favor of `GpuWorld`/runtime extraction. | `GRAPHICS-006`, `GRAPHICS-015`, `GRAPHICS-018`, `GRAPHICS-018Q`, `GRAPHICS-018R`, `GRAPHICS-018T`, `GRAPHICS-026`, RHI contract tests, and opt-in `gpu;vulkan` smoke for backend-specific parity. | partial: CPU/null contracts pass; Vulkan parity remains opt-in and some backend operational prerequisites remain open. | `GRAPHICS-020` final deletion slice after RHI/Vulkan import scan and opt-in evidence. |
| Legacy CUDA RHI side path | `RHI.CudaDevice`, `RHI.CudaError` | No promoted default graphics path owns CUDA. `GRAPHICS-086` retires legacy CUDA from the promoted default path; future CUDA requires a new method/backend task with a concrete workload. | `GRAPHICS-086` plus the promoted-source CUDA-import contract. | retired for current promoted scope; not a default CPU/null blocker. | `LEGACY-009` handles mechanical deletion once consumer-grep/subtree gates are clean. |
| Legacy runtime render orchestration | `Runtime.GraphicsBackend`, `Runtime.RenderExtraction`, `Runtime.RenderOrchestrator`, `Runtime.ResourceMaintenance`, render-facing portions of `Runtime.Engine` / `Runtime.FrameLoop` | `runtime` owns composition, backend selection, render extraction, frame sequencing, and maintenance; graphics owns renderer APIs and GPU resources. | `GRAPHICS-016`, `GRAPHICS-018R`, `GRAPHICS-026`, `RORG-031-runtime-composition`, runtime integration tests, and default CPU/null frame-loop tests. | partial: promoted engine/extraction exists, but full runtime composition/asset ingest/scene parity remains tracked outside graphics. | `GRAPHICS-020` final deletion slice after runtime import scan and runtime follow-ups. |
## Mechanical deletion readiness checklist
- `python3 tools/repo/check_layering.py --root src --strict` reports no promoted-layer dependency on `src/legacy` outside documented allowlist entries.
- `docs/api/generated/module_inventory.md` still lists legacy modules only as `legacy`; no promoted module path or task claims the legacy module is the owner.
- All blocker tasks named in the matrix are done or explicitly retired as non-goals.
- Focused contract/integration tests named by each blocker task pass in the current C++23 `ci` build tree.
- The final deletion slice is purely mechanical or is split from semantic replacements; no mechanical delete is mixed with new behavior.
## Tests
- [x] Run task policy, docs link, module inventory, layering, and docs-sync checks when gates are updated.
- [x] Future deletion/isolation slices must add or run import-scan checks proving promoted code does not import deleted legacy modules.
## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the retirement-gate overview.
- [x] Update generated module inventory only if module surfaces change; this planning slice should use `--check` only.
- [x] Update rendering backlog links after promoting/retiring this task.
## Acceptance criteria
- [x] Every retained legacy rendering module has a documented owner/task/gate.
- [x] No legacy dependency remains untracked in promoted final layers.
- [x] Deletion readiness can be evaluated mechanically from documented gates.
## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```
## Completion
- Completed: 2026-05-07.
- Commit reference: local commit for GRAPHICS-020 gate-map slice.
- Notes:
  - Retirement gates are recorded by legacy module family and cover generated legacy graphics/RHI/runtime-rendering module inventory.
  - No legacy source was deleted; final deletion remains a future mechanical slice after all blocker tasks and import scans pass.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Removing legacy source before parity gates and follow-up tasks are satisfied.
## Cross-links
- `GRAPHICS-024` (overlays/presentation/editor handoff planning) is the
  authoritative source for per-module owner decisions covering
  `Graphics.OverlayEntityFactory`, `Graphics.Presentation`,
  `Graphics.Passes.Composition`, and `Graphics.VisualizationConfig`.
  Retirement gating in this task must mechanically resolve each legacy
  overlay/presentation module against the overlay / presentation / editor
  handoff inventory in
  `../../docs/migration/nonlegacy-parity-matrix.md` before any deletion or
  isolation decision. Open follow-ups identified by `GRAPHICS-024` are tracked
  through existing `GRAPHICS-010Q`, `GRAPHICS-011Q`, `GRAPHICS-013CQ`,
  `GRAPHICS-014Q`, and `GRAPHICS-017Q` clarification tasks; no new follow-up
  task is required for this row.
