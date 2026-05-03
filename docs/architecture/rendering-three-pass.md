# Rendering Three-Pass Architecture (Canonical)

This is the canonical rendering architecture specification for the runtime path currently in use.

## Scope

The renderer uses three primitive-owned passes:

1. `SurfacePass` (filled triangles)
2. `LinePass` (edges/wire/debug lines)
3. `PointPass` (points/nodes/point clouds/debug points)

These primitive-owned passes feed later composition/overlay stages. There are no parallel legacy collector passes.

## Core Invariants

- Primitive ownership is explicit: surfaces/lines/points are rendered only by their owning pass.
- ECS toggle model is presence/absence of typed components (`ECS::Surface::Component`, `ECS::Line::Component`, `ECS::Point::Component`).
- CPU geometry authority is PropertySet-backed (`PointCloud::Cloud`, `Graph`, `Halfedge::Mesh`).
- GPU rendering is BDA-driven from shared `GeometryGpuData` buffers.
- GPU-driven rendering uses one canonical instance-slot space shared by renderable records, transform records, bounds/culling records, material references, picking IDs, and draw buckets.
- Runtime owns CPU scene composition and extraction-side mappings; graphics owns GPU scene buffers and must not store graphics GPU handles in canonical ECS components.
- Mesh/graph/point-cloud paths are equal peers in lifecycle, upload, and scheduling.
- Frame construction is recipe-driven: resource allocation/import happens once in `FrameSetup`, then later passes only consume canonical blackboard handles.

## Canonical Frame Resources

The render graph blackboard exposes a fixed canonical resource vocabulary:

| Resource | Format | Lifetime | Producer / Use |
|----------|--------|----------|----------------|
| `SceneDepth` | Swapchain/device depth format | Imported | Depth-tested scene + picking |
| `EntityId` | `R32_UINT` | Frame transient | `PickingPass`, selection/debug sampling |
| `PrimitiveId` | `R32_UINT` | Frame transient | `PickingPass` primitive-domain hint (`4` high bits = `SelectionPrimitiveDomain`, `28` low bits = authoritative face/edge/point ID when available, otherwise primitive index) for sub-element selection/debug |
| `SceneNormal` | `R16G16B16A16_SFLOAT` | Frame transient | Deferred-capable surface normal target; also sampleable for debug |
| `Albedo` | `R8G8B8A8_UNORM` | Frame transient | Deferred-capable surface albedo target |
| `Material0` | `R16G16B16A16_SFLOAT` | Frame transient | Deferred-capable surface material/shading parameters |
| `SceneColorHDR` | `R16G16B16A16_SFLOAT` | Frame transient | Geometry/lighting output |
| `ShadowAtlas` | `D32_SFLOAT` | Frame transient | Cascade shadow depth atlas (horizontal strip, one cascade per column block). Produced by `ShadowPass`, sampled by `SurfacePass` (forward) and `CompositionPass` (deferred) |
| `SceneColorLDR` | Swapchain format | Frame transient | Post-process + overlay composition target |
| `PostProcess.BloomScratch` | `R16G16B16A16_SFLOAT` | Frame transient | Optional bloom intermediate owned by `PostProcessPass` |
| `PostProcess.Histogram` | storage buffer | Frame transient | Optional luminance histogram diagnostics owned by `PostProcessPass` |
| `PostProcess.AATemp` | Swapchain format | Frame transient | Optional anti-aliasing intermediate owned by `PostProcessPass` |
| `SelectionMask` | `R8_UNORM` | Frame transient | Reserved for future outline mask split |
| `SelectionOutline` | Swapchain format | Frame transient | Reserved for future standalone outline target |
| `DebugViewRGBA` | `R8G8B8A8_UNORM` | Frame transient | Debug-view preview output for sampled resource inspection |
| `Backbuffer` | Swapchain format | Imported | Final presentation destination only |

`FrameRecipe` determines which of these are allocated for a frame. Unused optional resources are not created. The promoted implementation lives in `Extrinsic.Graphics.FrameRecipe`; it declares typed feature gates, canonical resource names, pass-order introspection, and the backend-agnostic graph construction helper consumed by the null renderer.

Default feature gates:

- Always active: `CullingPass`, `SurfacePass`, `LinePass`, `PointPass`, `Present`, `SceneDepth`, `SceneColorHDR`, GPU scene buffers, material buffer, and draw-bucket buffers for surface opaque, surface alpha-mask, lines, points, shadow opaque, and selection surface/line/point lanes.
- Default active but explicitly gateable: `DepthPrepass`, deferred/hybrid `CompositionPass`, `PostProcessPass`, and `ImGuiPass`.
- Optional: `PickingPass` (`EntityId`, `PrimitiveId`, `Picking.Readback`), `ShadowPass` (`ShadowAtlas`), `SelectionOutlinePass` (`SelectionOutline`), and `DebugViewPass` (`DebugViewRGBA`).

The imported `Backbuffer` is declared once and finalized only by the `Present` declaration; intermediate passes write transient recipe resources instead of taking backbuffer ownership.

## Picking and sub-element selection contract

`PrimitiveId` is a hint produced by the picking pass, not a replacement for CPU-side geometry authority. Its high two bits encode the primitive domain (`surface triangle`, `line segment`, `point`, or reserved) and the low 30 bits encode the domain-local primitive index. Selection resolution follows these rules:

- Entity ID from `EntityId` remains the ownership key. Runtime selection rejects invalid or non-selectable entities before resolving sub-elements.
- For explicit point and line domains on entities that expose the matching primitive component, the GPU primitive hint is authoritative for the requested vertex/edge index and may be CPU-refined for hit-space data.
- For explicit surface-triangle domains, the surface primitive hint is authoritative for the face anchor. CPU refinement may compute the nearest vertex/edge on that face and hit-space diagnostics, but it must not fall back to a different whole-mesh raycast face while a valid surface hint exists.
- For mesh helper lines on pure-surface entities, CPU face-anchored refinement remains the compatibility fallback because rendered helper line IDs are not necessarily topology edge IDs.
- If no valid primitive hint is available, CPU picking is the compatibility fallback and must return IDs from the authoritative mesh/graph/point-cloud data structures.

## Pass Contract

| Pass | Inputs | Outputs | Initialization / Ownership |
|------|--------|---------|----------------------------|
| `CullingPass` | GPUScene buffers, `CameraUBO` | GPU draw-bucket args/count buffers | Real rendergraph pass that runs before any geometry-consuming pass; resets bucket counters and dispatches GPU culling against the canonical instance/bounds records. Source module: `Pass.Culling` (`CullingPass`). Drives the surface, line, point, alpha-mask, shadow, and selection draw buckets owned by `CullingSystem`. Not a per-geometry-pass helper |
| `PickingPass` | `SceneDepth` | `EntityId` | Clears both; no swapchain ownership. Logical picking/selection ID stage: composed of split source modules (`Pass.Selection.EntityId`, `Pass.Selection.FaceId`, `Pass.Selection.EdgeId`, `Pass.Selection.PointId`) plus the readback/result seam owned by `SelectionSystem`. Each module fills part of the `EntityId` / `PrimitiveId` outputs; the logical name is `PickingPass` |
| `DepthPrepass` | GPUScene buffers | `SceneDepth` | Depth-only early-Z fill (triangle-list geometry only). Clears depth to 1.0. Recipe-driven: active only when `FrameRecipe::DepthPrepass` is true. Internal to `SurfacePass` (shares draw stream and GPU culling infrastructure) |
| `ShadowPass` | GPUScene buffers, `ShadowCascadeData` / shadow params in the global camera-light packet | `ShadowAtlas` | Renders depth-only from each cascade's light-space VP into a horizontal-strip atlas (`CascadeCount × Resolution` wide). Uses the `ShadowOpaque` culling bucket and skips command recording when `ShadowParams::Enabled` is false, the cascade count/resolution disables the atlas, the pipeline is missing, or the bucket is invalid. Uses a dedicated depth pipeline with `VK_CULL_MODE_FRONT_BIT` to reduce self-shadowing. Recipe-gated by `ShadowParams::Enabled`. Texel-snapped cascade frusta computed by runtime/later shadow extraction are packed into `ShadowCascadeData` |
| `SurfacePass` | `SceneDepth`, `ShadowAtlas` (sampled), GPUScene buffers | forward: `SceneColorHDR`; deferred: `SceneNormal` + `Albedo` + `Material0`, `SceneDepth` | Opaque surface lane. Forward path samples `ShadowAtlas` via global set (binding 1, `sampler2DShadow` with `VK_COMPARE_OP_LESS_OR_EQUAL`). When depth prepass is active, loads existing depth and uses `VK_COMPARE_OP_EQUAL` (zero overdraw). Otherwise clears depth with `VK_COMPARE_OP_LESS` |
| `CompositionPass` | deferred: `SceneNormal`, `Albedo`, `Material0`, `SceneDepth`, `ShadowAtlas` (sampled via global set 1) | deferred: `SceneColorHDR` | Fullscreen deferred lighting with PCF shadow sampling from cascade atlas. No-op in forward mode |
| `LinePass` | `SceneColorHDR`, `SceneDepth` | `SceneColorHDR`, `SceneDepth` | Forward-overlay lane for wireframe/graph/debug lines; accumulates via `LOAD` |
| `PointPass` | `SceneColorHDR`, `SceneDepth` | `SceneColorHDR`, `SceneDepth` | Forward-overlay lane for point clouds/debug points; accumulates via `LOAD` |
| `PostProcessPass` | `SceneColorHDR` | `SceneColorLDR`, `PostProcess.BloomScratch`, `PostProcess.Histogram`, `PostProcess.AATemp` | Backend-agnostic HDR-to-LDR chain. `PostProcessSystem` describes deterministic stages in order: optional `Histogram`, optional `Bloom`, required enabled-chain `ToneMap`, then optional `FXAA` or `SMAA`. Invalid numeric settings are sanitized and unsupported AA enum values are diagnosed without Vulkan |
| `SelectionOutlinePass` | `EntityId`, presentation target | presentation target | Alpha-blends via `LOAD`; outlines the **union stencil** of selected/hovered renderable PickIDs across surface, line, and point lanes (including mesh/graph/cloud point modes such as sphere impostors). Source module: `Pass.Selection.Outline` (`SelectionOutlinePass`). Conceptually paired with `PickingPass` but scheduled later in the pipeline so it can read overlay results |
| `DebugViewPass` | selected sampled resource or deterministic fallback | `DebugViewRGBA` | Writes a fullscreen preview image for backend-agnostic render-target inspection. `DebugViewSystem` resolves requested resources from frame-recipe introspection, rejects disabled/missing/buffer/backbuffer selections with structured diagnostics, and falls back to the current presentation source without platform/window ownership |
| `ImGuiPass` | presentation target | presentation target | UI overlay via `LOAD`; consumes renderer-owned `ImGuiOverlayFrame` draw-data summaries translated by runtime/editor, never platform/window state |
| `Present` | `FrameRecipe.PresentSource`, `Backbuffer` | imported `Backbuffer` finalization | Explicit finalization stage and the only pass declaration allowed to finalize the imported backbuffer |

## Data Contract (CPU -> GPU)

All renderable buffers are derived from PropertySet spans (`std::span`) and uploaded through lifecycle/sync systems.

`RenderWorld` is the frame-local immutable snapshot consumed by renderer passes.
Runtime submits extraction packets through `IRenderer::SubmitRuntimeSnapshots()`;
the renderer copies them into renderer-owned frame storage and exposes read-only
spans from `RenderWorld`:

- `RenderableSnapshot`: stable renderable ID, canonical `GpuInstanceHandle`,
  current model matrix, bounds/culling data, render flags, and resolved material
  slot metadata.
- `LightSnapshot`: typed directional/point/spot light values extracted by
  runtime.
- `PickRequestSnapshot`, `SelectionSnapshot`, `ShadowSnapshot`,
  `DebugPrimitiveSnapshot`, and `PostProcessSnapshot`: optional frame-feature
  packets with explicit default states. Runtime/pass tasks populate these as
  GRAPHICS-012, GRAPHICS-009, GRAPHICS-010/011/014, and GRAPHICS-013A/B/C land.
- `DebugPrimitiveSnapshot`: sanitized transient debug line, point, and triangle
  packet spans. Runtime submits packet spans through
  `IRenderer::SubmitRuntimeSnapshots()`; graphics copies and sanitizes them into
  renderer-owned frame storage. Non-finite coordinates/colors are rejected,
  line widths are clamped to `[0.5, 32]`, point radii are clamped to
  `[0.0001, 1]`, and rejected records increment `InvalidSnapshotRecordCount`.
- `InvalidSnapshotRecordCount`: deterministic diagnostics for malformed runtime
  records dropped while building the immutable snapshot.

These spans never reference live ECS storage. Runtime-owned pointer sidecars used
by the current visualization sync seam remain a GRAPHICS-002 follow-up question
before pass contracts depend on them directly.

The promoted GPU scene is organized around these canonical buffers:

- `RenderableInstance` buffer: one record per renderable instance slot with geometry record reference, material slot, stable entity/pick ID, render-domain flags, visibility/layer flags, selection flags, and draw-bucket participation bits.
- `Transform` buffer: current and previous world transforms indexed by the same instance slot as `RenderableInstance`.
- `Bounds/Culling` buffer: local/world-space bounds, culling flags, shadow participation, and visibility metadata indexed by the same instance slot; any compacted visible list is an indirection over instance slots.
- `Material` buffer: graphics-owned `Extrinsic.Graphics.MaterialSystem` slots
  populated from runtime-extracted CPU material descriptions or asset IDs.
  Slot `0` is the default/fallback material, and layout version `1` is a
  128-byte `RHI::GpuMaterialSlot` with four custom `vec4` slots plus four
  bindless texture references (`Albedo`, `Normal`, `MetallicRoughness`,
  `Emissive`). Asset IDs are resolved to material leases/slots by runtime or
  graphics asset sidecars; canonical ECS components do not store graphics-owned
  material-slot indices.
- `Geometry` records: graphics-owned references to uploaded geometry views/buffers, shared by renderable instances through generation-checked handles.
- `Light` buffer / `LightEnvironmentPacket`: runtime-extracted light descriptions, directional/ambient parameters, deterministic fallback directional light state, and upload diagnostics consumed by lighting and shadow passes. Unsupported light enum values are dropped from the GPU light buffer and counted in `LightSyncDiagnostics`; when a frame has no directional light snapshot, `LightSystem` uploads the configured directional fallback so ambient/directional defaults remain deterministic.
- Scene table / descriptor set: the backend binding point that exposes the renderable, transform, bounds/culling, material, geometry, and light buffers to passes.

Runtime owns ECS access, dirty-domain interpretation, deletion events, and sidecar mappings from entities/assets/geometry sources to graphics handles. Graphics owns GPU allocation, generation checks, descriptor binding, and diagnostics for invalid or stale handles.

### Culling and draw-bucket contract

`CullingPass` records the culling command sequence in a fixed order: reset every bucket counter, publish the `GpuCullBucketTable`, barrier that table for shader reads, bind the cull pipeline, push `GpuCullPushConstants`, dispatch over canonical instance slots, then barrier every indirect-args/count buffer for indirect reads. Bucket outputs are indexed by canonical GPU instance slot through each indirect command's `firstInstance`; compacted visible lists are not a second entity identity source.

The current `GpuCullBucketTable` contains eight bucket outputs:

| Bucket | Command type | Producer condition | Consumer lane |
|--------|--------------|--------------------|---------------|
| `SurfaceOpaque` | indexed | `GpuRender_Surface`, not alpha-mask | depth/surface opaque |
| `SurfaceAlphaMask` | indexed | `GpuRender_Surface | GpuRender_AlphaMask` | surface alpha-mask |
| `Lines` | indexed | `GpuRender_Line` | line pass |
| `Points` | non-indexed | `GpuRender_Point` | point pass |
| `ShadowOpaque` | indexed | `GpuRender_Surface | GpuRender_CastShadow`, not transparent | shadow pass |
| `SelectionSurface` | indexed | selectable surface renderable | selection/picking surface lane |
| `SelectionLines` | indexed | selectable line renderable | selection/picking line lane |
| `SelectionPoints` | non-indexed | selectable point renderable | selection/picking point lane |

Selection participation is explicit via `GpuRender_Selectable`; the culling shader mirrors otherwise visible surface/line/point renderables into the matching selection bucket. Invalid geometry slots, out-of-range geometry slots, zero-sized geometry ranges, non-positive world-sphere radii, invisible instances, and frustum-culled instances produce no bucket writes. CPU-side generation checks in `GpuWorld` keep stale freed geometry from remaining in instance records before this shader path runs.

### Depth, surface, and G-buffer command contract

`DepthPrepass`, forward `SurfacePass`, and deferred `GBufferPass` consume the `SurfaceOpaque` bucket produced by `CullingPass`. Each pass binds its configured graphics pipeline, binds the managed index buffer from `GpuWorld`, pushes `GpuScenePushConstants`, and records `DrawIndexedIndirectCount` with the bucket's indexed-args buffer, count buffer, and capacity. `GpuScenePushConstants::SceneTableBDA` is the canonical binding seam for the scene table and the SSBOs reachable through it; these passes must not grow pass-specific push constants to carry renderable-instance, transform, material, geometry, or bounds/culling data.

Forward lighting writes `SceneColorHDR` and owns `SceneDepth` when the depth prepass is disabled. Deferred and hybrid lighting paths write `SceneNormal`, `Albedo`, and `Material0` from `SurfacePass`, then resolve through `CompositionPass`; hybrid remains deferred-backed until `GRAPHICS-025` adds its transparent/special-material forward overlay. The CPU/null contract requires these command sequences to be testable without Vulkan and to skip recording when the owning system, pipeline, or surface bucket handles are not valid.

### Lighting, shadow, and deferred composition contract

`LightSystem` owns only renderer-local lighting defaults and GPU light-buffer packetization. Runtime extraction submits immutable `LightSnapshot` spans; graphics never queries live ECS light components. `LightSystem::ApplyTo()` writes the directional and ambient default fields in `RHI::CameraUBO`, while `SyncGpuBuffer()` converts directional, point, and spot snapshots into `RHI::GpuLight` records. Unsupported snapshot enum values are skipped deterministically and reported through `LightSyncDiagnostics`. A missing directional snapshot is not an error: the configured directional fallback is uploaded and marked in diagnostics.

`ShadowSystem` owns backend-agnostic shadow params (`Enabled`, cascade count, atlas resolution, depth/normal bias, PCF radius, split lambda), `ShadowCascadeData`, `ShadowAtlasDesc`, and `ShadowDiagnostics`. Cascade counts above `RHI::kMaxShadowCascades` are clamped and counted as unsupported. Disabled params, zero cascades, or zero resolution produce a disabled atlas desc; disabled shadows therefore avoid optional recipe resources and avoid `ShadowPass` commands. Enabled atlases are horizontal strips sized as `CascadeCount * AtlasResolution` by `AtlasResolution`.

The global camera-light packet now carries shadow cascade matrices, split/count data, bias/filter params, and atlas size/enabled flags. Forward and deferred shaders share that packet contract; later Vulkan/backend work may bind the atlas sampler, but CPU/null tests validate the packing without requiring a Vulkan device.

`DeferredLightingPass` is the promoted `CompositionPass` command contract. With an initialized `DeferredSystem` and a configured pipeline it binds the fullscreen lighting pipeline, pushes the scene-table BDA, and records a fullscreen triangle draw that resolves canonical G-buffer resources into `SceneColorHDR`. Without the owning system or pipeline it records no commands.

### Line and point command contract

`ForwardLinePass` and `ForwardPointPass` consume draw buckets produced by `CullingPass` and never query live ECS/editor/debug ownership. The frame recipe imports explicit line and point bucket resources: `Cull.Lines.IndexedArgs`, `Cull.Lines.Count`, `Cull.Points.NonIndexedArgs`, and `Cull.Points.Count`. `ForwardLinePass` requires an initialized `ForwardSystem`, a configured pipeline, and a valid indexed `Lines` bucket before it binds the managed index buffer, pushes `GpuScenePushConstants`, and records `DrawIndexedIndirectCount`. `ForwardPointPass` requires an initialized `ForwardSystem`, a configured pipeline, and a valid non-indexed `Points` bucket before it pushes `GpuScenePushConstants` and records `DrawIndirectCount`. Invalid or empty buckets are deterministic no-ops so CPU/null tests can validate the contract without Vulkan.

Transient debug primitive packets are frame-local submissions owned by runtime extraction/debug tooling and copied into `RenderWorld`. They are not persistent editor overlay entities and do not create ECS ownership inside graphics. Concrete GPU expansion of transient packets into line/point/surface draw buckets is a backend/runtime upload concern; the promoted contract guarantees packet shape, sanitization, counts, and line/point overlay resource scheduling.

### Visualization attribute and overlay packet contract

`Graphics.VisualizationPackets` defines the renderer-owned, data-only packet seam for visualization attributes and overlays. Runtime/geometry systems remain responsible for property enumeration, isoline extraction, vector-field generation, Htex patch content, and ECS ownership; graphics consumes immutable packets with explicit domains, element counts, buffer device-address seams, ranges, colormap IDs, and overlay descriptors.

The CPU/null contract validates scalar, color, vector-field, isoline, generic attribute-buffer, and Htex patch-preview atlas descriptors without requiring texture residency. Diagnostics report missing attributes/resources, domain mismatches, invalid ranges or non-finite overlay parameters, unsupported colormap IDs, and Htex descriptors whose texture residency is intentionally deferred to `GRAPHICS-015`. `VisualizationOverlaySummary` aggregates vector glyph counts, isoline layer/value counts, and whether later texture residency is required, so downstream upload/pass work can remain deterministic and testable without importing live geometry or editor overlay ownership.

### Postprocess chain contract

`PostProcessSystem` owns backend-agnostic postprocess settings, deterministic chain description, push-constant data, and diagnostics. The promoted chain is data-only and CPU/null-testable: when enabled it always contains `ToneMap` to transform `SceneColorHDR` into `SceneColorLDR`; `Histogram` and `Bloom` are optional pre-tonemap stages; `FXAA` and `SMAA` are mutually exclusive typed anti-aliasing choices after tonemapping. Disabling the chain produces no stages and no `SceneColorLDR` write in the system description; the frame recipe likewise gates `SceneColorLDR` and postprocess intermediates behind `FrameRecipeFeatures::EnablePostProcess`.

The split source modules (`Pass.PostProcess.Histogram`, `Bloom`, `ToneMap`, `FXAA`, and `SMAA`) are command-contract shims. Each pass records no commands unless `PostProcessSystem` is initialized, its stage is enabled, and a valid pipeline has been configured. Enabled fullscreen stages bind their pipeline, push `PostProcessPushConstants`, and draw a fullscreen triangle; the histogram diagnostics stage binds its pipeline, pushes the same packet, and dispatches a compute-style diagnostics workload. Concrete descriptor binding, shader kernels, downsample pyramids, exposure adaptation history, and Vulkan-specific layout choices are backend follow-ups, not GRAPHICS-013A ownership.

### Debug-view and render-target inspection contract

`DebugViewSystem` owns backend-agnostic debug-view selection and inspection metadata. It builds a deterministic inspection table from `FrameRecipeIntrospection`, classifying resources as texture, depth texture, buffer, backbuffer, alias, or unknown. Only enabled texture/depth resources are previewable, and `DebugViewRGBA` is deliberately excluded as an input preview target to avoid feedback. Buffer resources such as GPU scene tables, draw buckets, histogram buffers, and picking readbacks are inspectable metadata entries but are not sampled by the preview pass.

Selection resolution is deterministic and CPU-testable. Disabled debug view produces no enabled preview selection. Missing, disabled, or unsupported requested resources increment structured diagnostics and fall back first to the requested presentation-source resource (default `SceneColorLDR`), then to `SceneColorHDR`, then to the first enabled previewable texture if needed. `Pass.DebugView` (`DebugViewPass`) records no commands unless `DebugViewSystem` is initialized, a valid pipeline is set, and a resolved selection is enabled; otherwise it binds the pipeline, pushes `DebugViewPushConstants`, and draws a fullscreen triangle into `DebugViewRGBA`. Concrete sampled-image descriptor binding and shader visualization modes remain backend follow-ups.

### ImGui overlay and present/finalization contract

`ImGuiOverlaySystem` owns only backend-agnostic overlay draw-data summaries and diagnostics. Runtime/editor code translates concrete `ImDrawData` into `ImGuiOverlayFrame` records; graphics records accepted draw-list counts, command/vertex/index totals, user-texture presence, invalid display-size diagnostics, and deterministic text diagnostics. It does not import Dear ImGui platform/window backends or mutate platform state.

`Pass.ImGui` records no commands unless the overlay system is initialized, accepted overlay work exists, and a valid pipeline is configured. The command contract binds the overlay pipeline, pushes `ImGuiOverlayPushConstants`, and records an indexed draw over the accepted overlay index count. The frame recipe schedules `ImGuiPass` as a side-effect overlay over `FrameRecipe.PresentSource`; it does not write or finalize `Backbuffer`.

`Present` is the only frame-recipe declaration with `FinalizesBackbuffer=true`. The render graph rejects attempted writes to imported backbuffer resources through normal color/depth/shader/transfer write usages; the imported backbuffer is consumed only through `TextureUsage::Present` by the final present pass. `Pass.Present` is a CPU-testable fullscreen finalization shim that records only when a valid pipeline is configured. Swapchain acquire/present and platform window ownership remain platform/backend responsibilities.

### Legacy feature coverage classification

The canonical GPU scene covers renderable identity and per-instance state. Legacy-inspired features that need different ownership attach through declared auxiliary resources or remain outside graphics ownership.

- Fits directly in canonical instance slots: retained surface, line, and point renderables; current/previous transforms; local/world bounds; visibility, layer, selection, and shadow flags; material slot references; stable entity/pick/primitive IDs; and draw-bucket participation.
- Needs auxiliary GPU resources/buffers: per-vertex, per-edge, and per-face attributes; colors, widths, radii, normals, labels, centroid/Voronoi data, point-render-mode data, transient debug primitive streams, Htex/visualization atlases, texture/bindless residency, post-process intermediates/LUTs/readbacks, and shadow/camera packets.
- Remains CPU/runtime-only: ECS extraction and deletion handling, dirty-domain interpretation, runtime sidecar mappings, selection resolution and CPU hit refinement, camera/input/gizmo mutation, property enumeration/range policy, ImGui draw-data production, and async visualization baking.
- Belongs to assets/geometry: import/export/model loading, PropertySet/Halfedge/Graph/PointCloud authority, geometry algorithms and spatial structures, and isoline/vector-field/Htex patch generation when they produce source geometry/data.

Graphics may reference asset IDs and geometry GPU views only through snapshots/handles supplied by runtime; it must not import live ECS ownership or store graphics GPU handles in canonical ECS components.

- Vertex-domain data -> positions/normals/aux buffers
- Edge-domain data -> edge index + optional per-edge aux
- Face-domain data -> optional per-face aux

Dirty domains drive sync granularity:

- `VertexPositions`
- `VertexAttributes`
- `EdgeTopology`
- `EdgeAttributes`
- `FaceTopology`
- `FaceAttributes`

Position/topology changes may escalate to full re-upload; pure attribute changes use incremental extraction/upload.

Per-frame lighting is carried by `LightEnvironmentPacket` and includes directional + ambient parameters plus typed `ShadowParams` (`Enabled`, `CascadeCount`, cascade splits, depth/normal bias, PCF radius, split lambda). Shadow cascade view-projection matrices and split distances are computed by the runtime/shadow extraction seam with texel-snapped orthographic frusta, packed into `ShadowCascadeData`, and uploaded to the global camera UBO fields (`ShadowCascadeMatrices[4]`, `ShadowCascadeSplitsAndCount`, `ShadowBiasAndFilter`, `ShadowAtlasSizeAndFlags`). The shadow atlas is bound by concrete backends as a `sampler2DShadow` with `VK_COMPARE_OP_LESS_OR_EQUAL` for hardware-accelerated PCF. Both forward and deferred paths share the same packet contract for cascade selection and PCF sampling.

## Pipeline Order

`DefaultPipeline` execution order:

1. `CullingPass` (populates per-bucket indirect args/count buffers for downstream geometry/shadow/selection passes)
2. `PickingPass` (logical stage; source modules `Pass.Selection.EntityId`/`FaceId`/`EdgeId`/`PointId`)
3. `DepthPrepass` (internal to `SurfacePass`, recipe-gated)
4. `ShadowPass` (recipe-gated by `ShadowParams::Enabled`)
5. `SurfacePass`
6. `CompositionPass`
7. `LinePass`
8. `PointPass`
9. `PostProcessPass`
10. `SelectionOutlinePass`
11. `DebugViewPass`
12. `ImGuiPass`
13. `Present`

`Extrinsic.Graphics.FrameRecipe::DescribeDefaultFrameRecipe()` reports this pass order with disabled optional stages retained as declarations for tooling/review, while `BuildDefaultFrameRecipe()` emits only enabled passes/resources into `Graphics.RenderGraph`.

### Pass module naming

The promoted graphics layer keeps a 1:1 mapping between logical passes documented above and the C++20 module names under `src/graphics/renderer/Passes/`. Agents and reviewers should treat the table below as canonical when reconciling task text, doc text, and source.

| Logical pass | Source modules | Source class(es) |
|--------------|----------------|------------------|
| `CullingPass` | `Extrinsic.Graphics.Pass.Culling` | `CullingPass` |
| `PickingPass` (logical stage) | `Extrinsic.Graphics.Pass.Selection.EntityId`, `Extrinsic.Graphics.Pass.Selection.FaceId`, `Extrinsic.Graphics.Pass.Selection.EdgeId`, `Extrinsic.Graphics.Pass.Selection.PointId` | `EntityIdPass`, `FaceIdPass`, `EdgeIdPass`, `PointIdPass` |
| `DepthPrepass` | `Extrinsic.Graphics.Pass.DepthPrepass` (internal to `SurfacePass`) | `DepthPrepassPass` |
| `ShadowPass` | `Extrinsic.Graphics.Pass.Shadows` | `ShadowPass` |
| `SurfacePass` | `Extrinsic.Graphics.Pass.Forward.Surface`, `Extrinsic.Graphics.Pass.Deferred.GBuffers` | `SurfacePass`, `GBufferPass` |
| `CompositionPass` | `Extrinsic.Graphics.Pass.Deferred.Lighting` | `DeferredLightingPass` |
| `LinePass` | `Extrinsic.Graphics.Pass.Forward.Line` | `LinePass` |
| `PointPass` | `Extrinsic.Graphics.Pass.Forward.Point` | `PointPass` |
| `PostProcessPass` | `Extrinsic.Graphics.Pass.PostProcess.*` | bloom/FXAA/SMAA/ToneMap/Histogram passes |
| `SelectionOutlinePass` | `Extrinsic.Graphics.Pass.Selection.Outline` | `SelectionOutlinePass` |
| `DebugViewPass` | `Extrinsic.Graphics.Pass.DebugView` | `DebugViewPass` |
| `ImGuiPass` | `Extrinsic.Graphics.Pass.ImGui` | `ImGuiPass` |
| `Present` | `Extrinsic.Graphics.Pass.Present` | `PresentPass` |

Notes:

- `CullingPass` is a real rendergraph pass, not a per-pass helper. It is owned by `Extrinsic.Graphics.Pass.Culling` and drives the GPU draw buckets consumed by depth, surface, line, point, shadow, and selection passes.
- `PickingPass` is a logical name for the picking/selection ID stage. The source intentionally splits it into `Pass.Selection.EntityId` / `FaceId` / `EdgeId` / `PointId` modules so each primitive domain has independent contracts and tests. Splitting the implementation modules is **acceptable**; collapsing them into a single source module is **not required** by this architecture doc.
- `SelectionSystem` owns the CPU-visible pending-pick/readback seam. `EncodedSelectionId` reserves the high four bits for `SelectionPrimitiveDomain` (`Entity`, `Face`, `Edge`, `Point`) and the low 28 bits for stable primitive-domain payload. `PickReadbackResult` reports hit/no-hit plus the extracted stable entity ID; graphics reports results but never mutates ECS selection state.
- The frame recipe declares `PickingPass` dependencies on surface, line, and point cull buckets (`Cull.SurfaceOpaque.*`, `Cull.Lines.*`, and `Cull.Points.*`) so each split selection ID pass can reuse the canonical GPU-driven bucket contracts.
- `Pass.Selection.Outline` is the source module for `SelectionOutlinePass`. Although it shares the `Selection.*` source prefix with the picking ID modules, it is a separate logical pass (scheduled after post-process) and must not be merged with `PickingPass` in the pipeline order.

Lighting-path coexistence is now explicit:

- **Forward mode:** `SurfacePass` writes directly to `SceneColorHDR`; `CompositionPass` is a no-op; `LinePass` and `PointPass` accumulate onto the same HDR target.
- **Deferred mode:** `SurfacePass` writes only the G-buffer MRT set (`SceneNormal`, `Albedo`, `Material0`) plus depth; `CompositionPass` resolves those buffers into `SceneColorHDR`; `LinePass` and `PointPass` then execute as forward overlays on top of the lit HDR scene.
- **Hybrid contract (staging):** recipe/pipeline contracts treat `Hybrid` as a deferred-backed path for resource declaration and composition scheduling. Until a dedicated hybrid composer lands, it reuses the same G-buffer + `CompositionPass` contract as deferred while preserving a distinct typed lighting-path value. The real hybrid/transparent/special-material follow-up is tracked by [GRAPHICS-025](../../tasks/backlog/rendering/GRAPHICS-025-hybrid-transparent-special-material-path.md).

This establishes the current composition rule: deferred-capable opaque surfaces live in `SurfacePass`, while line/point/debug content remains in the forward lane. The same ordering is the extension point for future transparent or special-material forward overlays.

## Validation / Audit Expectations

- Render-graph introspection reports per-pass attachment metadata (resource name, format, load/store ops, imported flag).
- Render-graph introspection reports per-resource first/last read and write pass indices.
- Temporary audit logging may dump pass order, resource creation, transitions, and formats from `RenderDriver`.
- Any pass using `LOAD` without a guaranteed earlier write in-frame or an imported resource should emit a warning.
- `ValidateCompiledGraph()` returns `RenderGraphValidationResult` with structured diagnostics (error/warning severity).
- Missing required resources and transient resources without producers are validation **errors** (not warnings).
- Imported-resource write policies (`ImportedResourceWritePolicy`) enforce authorized writers per imported resource. Unauthorized writes are validation **errors**.
- Default policy: only `Present.LDR` may write to the imported Backbuffer.

## Robustness Requirements

- Reject non-finite positions/normals on submission/upload paths.
- Skip degenerate triangles.
- Clamp line widths and point radii to safe ranges.
- Condition EWA covariance (when active) and fall back safely if ill-conditioned.
- Keep push constants within device limits (compile/runtime checks). `RHI::GpuScenePushConstants` carries the scene-table BDA, frame index, and draw-bucket selector for GPU-driven depth/surface/G-buffer draws and must remain within Vulkan's 128-byte guaranteed minimum. Legacy `RHI::MeshPushConstants` is currently 120 bytes, leaving only 8 bytes before that limit; treat mesh push constants as budget-constrained and move additional mesh-draw data to a UBO/SSBO or other descriptor-backed payload rather than growing push constants further.
- Keep backbuffer ownership explicit: only `FrameSetup` imports it, only `Present` finalizes it when `SceneColorLDR` exists.

## Performance Intent

- CPU frame contribution target for rendering systems: under 2 ms.
- Retained rendering avoids per-frame geometry rebuilds.
- Transient paths use per-frame host-visible buffers with bounded growth and telemetry overflow counters.
- Recipe-driven setup avoids allocating optional G-buffer/debug targets when they are not requested.

## Where Active Work Lives

- Canonical rendering backlog index: [GRAPHICS-001 — Rendering parity inventory and task index](../../tasks/backlog/rendering/GRAPHICS-001-rendering-parity-inventory.md).
- Rendering backlog dependency DAG and agent selection rules: [tasks/backlog/rendering/README.md](../../tasks/backlog/rendering/README.md).
- Medium/long-horizon planning: [docs/roadmap.md](../roadmap.md).
- Historical migration narrative: [docs/migration/archive/plan.md](../migration/archive/plan.md) (archival index).
