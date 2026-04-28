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
- Mesh/graph/point-cloud paths are equal peers in lifecycle, upload, and scheduling.
- Frame construction is recipe-driven: resource allocation/import happens once in `FrameSetup`, then later passes only consume canonical blackboard handles.

## Canonical Frame Resources

The render graph blackboard exposes a fixed canonical resource vocabulary:

| Resource | Format | Lifetime | Producer / Use |
|----------|--------|----------|----------------|
| `SceneDepth` | Swapchain/device depth format | Imported | Depth-tested scene + picking |
| `EntityId` | `R32_UINT` | Frame transient | `PickingPass`, selection/debug sampling |
| `PrimitiveId` | `R32_UINT` | Frame transient | `PickingPass` primitive-domain hint (`2` high bits = domain, `30` low bits = authoritative face ID for surfaces when available, otherwise primitive index) for sub-element selection/debug |
| `SceneNormal` | `R16G16B16A16_SFLOAT` | Frame transient | Deferred-capable surface normal target; also sampleable for debug |
| `Albedo` | `R8G8B8A8_UNORM` | Frame transient | Deferred-capable surface albedo target |
| `Material0` | `R16G16B16A16_SFLOAT` | Frame transient | Deferred-capable surface material/shading parameters |
| `SceneColorHDR` | `R16G16B16A16_SFLOAT` | Frame transient | Geometry/lighting output |
| `ShadowAtlas` | `D32_SFLOAT` | Frame transient | Cascade shadow depth atlas (horizontal strip, one cascade per column block). Produced by `ShadowPass`, sampled by `SurfacePass` (forward) and `CompositionPass` (deferred) |
| `SceneColorLDR` | Swapchain format | Frame transient | Post-process + overlay composition target |
| `SelectionMask` | `R8_UNORM` | Frame transient | Reserved for future outline mask split |
| `SelectionOutline` | Swapchain format | Frame transient | Reserved for future standalone outline target |
| `Backbuffer` | Swapchain format | Imported | Final presentation destination only |

`FrameRecipe` determines which of these are allocated for a frame. Unused optional resources are not created.

## Pass Contract

| Pass | Inputs | Outputs | Initialization / Ownership |
|------|--------|---------|----------------------------|
| `PickingPass` | `SceneDepth` | `EntityId` | Clears both; no swapchain ownership |
| `DepthPrepass` | GPUScene buffers | `SceneDepth` | Depth-only early-Z fill (triangle-list geometry only). Clears depth to 1.0. Recipe-driven: active only when `FrameRecipe::DepthPrepass` is true. Internal to `SurfacePass` (shares draw stream and GPU culling infrastructure) |
| `ShadowPass` | GPUScene buffers, `ShadowCascadeData` (push constants) | `ShadowAtlas` | Renders depth-only from each cascade's light-space VP into a horizontal-strip atlas (`CascadeCount × Resolution` wide). Uses dedicated depth pipeline with `VK_CULL_MODE_FRONT_BIT` to reduce self-shadowing. Recipe-gated by `ShadowParams::Enabled`. Texel-snapped cascade frusta computed in `ComputeCascadeViewProjections()` |
| `SurfacePass` | `SceneDepth`, `ShadowAtlas` (sampled), GPUScene buffers | forward: `SceneColorHDR`; deferred: `SceneNormal` + `Albedo` + `Material0`, `SceneDepth` | Opaque surface lane. Forward path samples `ShadowAtlas` via global set (binding 1, `sampler2DShadow` with `VK_COMPARE_OP_LESS_OR_EQUAL`). When depth prepass is active, loads existing depth and uses `VK_COMPARE_OP_EQUAL` (zero overdraw). Otherwise clears depth with `VK_COMPARE_OP_LESS` |
| `CompositionPass` | deferred: `SceneNormal`, `Albedo`, `Material0`, `SceneDepth`, `ShadowAtlas` (sampled via global set 1) | deferred: `SceneColorHDR` | Fullscreen deferred lighting with PCF shadow sampling from cascade atlas. No-op in forward mode |
| `LinePass` | `SceneColorHDR`, `SceneDepth` | `SceneColorHDR`, `SceneDepth` | Forward-overlay lane for wireframe/graph/debug lines; accumulates via `LOAD` |
| `PointPass` | `SceneColorHDR`, `SceneDepth` | `SceneColorHDR`, `SceneDepth` | Forward-overlay lane for point clouds/debug points; accumulates via `LOAD` |
| `PostProcessPass` | `SceneColorHDR` | `SceneColorLDR` | Initializes LDR target; internal temp when FXAA enabled |
| `SelectionOutlinePass` | `EntityId`, presentation target | presentation target | Alpha-blends via `LOAD`; outlines the **union stencil** of selected/hovered renderable PickIDs across surface, line, and point lanes (including mesh/graph/cloud point modes such as sphere impostors) |
| `DebugViewPass` | selected sampled resource | `DebugViewRGBA`, optional presentation target | Writes preview image, optional viewport composite |
| `ImGuiPass` | presentation target | presentation target | UI overlay via `LOAD` |
| `Present` | `SceneColorLDR` | `Backbuffer` | Explicit final blit/copy |

## Data Contract (CPU -> GPU)

All renderable buffers are derived from PropertySet spans (`std::span`) and uploaded through lifecycle/sync systems.

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

Per-frame lighting is carried by `LightEnvironmentPacket` and includes directional + ambient parameters plus typed `ShadowParams` (`Enabled`, `CascadeCount`, cascade splits, depth/normal bias, PCF radius, split lambda). Shadow cascade view-projection matrices and split distances are computed by `ComputeCascadeViewProjections()` with texel-snapped orthographic frusta, packed into `ShadowCascadeData`, and uploaded to the global camera UBO (`CameraBufferObject` fields: `ShadowViewProj[4]`, `ShadowSplits`, `ShadowBiasAndFilter`). The shadow atlas is bound at global set binding 1 as a `sampler2DShadow` with `VK_COMPARE_OP_LESS_OR_EQUAL` for hardware-accelerated PCF. Both forward (`surface.frag`) and deferred (`deferred_lighting.frag`) paths share `shadow_sampling.glsl` for cascade selection and 3×3 PCF sampling.

## Pipeline Order

`DefaultPipeline` execution order:

1. `PickingPass`
2. `DepthPrepass` (internal to `SurfacePass`, recipe-gated)
3. `ShadowPass` (recipe-gated by `ShadowParams::Enabled`)
4. `SurfacePass`
5. `CompositionPass`
5. `LinePass`
6. `PointPass`
7. `PostProcessPass`
8. `SelectionOutlinePass`
9. `DebugViewPass`
10. `ImGuiPass`
11. `Present`

Lighting-path coexistence is now explicit:

- **Forward mode:** `SurfacePass` writes directly to `SceneColorHDR`; `CompositionPass` is a no-op; `LinePass` and `PointPass` accumulate onto the same HDR target.
- **Deferred mode:** `SurfacePass` writes only the G-buffer MRT set (`SceneNormal`, `Albedo`, `Material0`) plus depth; `CompositionPass` resolves those buffers into `SceneColorHDR`; `LinePass` and `PointPass` then execute as forward overlays on top of the lit HDR scene.
- **Hybrid contract (staging):** recipe/pipeline contracts treat `Hybrid` as a deferred-backed path for resource declaration and composition scheduling. Until a dedicated hybrid composer lands, it reuses the same G-buffer + `CompositionPass` contract as deferred while preserving a distinct typed lighting-path value.

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
- Keep push constants within device limits (compile/runtime checks). `RHI::MeshPushConstants` is currently 120 bytes, leaving only 8 bytes before Vulkan's 128-byte guaranteed minimum. Treat mesh push constants as budget-constrained: additional mesh-draw data should move to a UBO/SSBO or other descriptor-backed payload rather than growing the push-constant block further.
- Keep backbuffer ownership explicit: only `FrameSetup` imports it, only `Present` finalizes it when `SceneColorLDR` exists.

## Performance Intent

- CPU frame contribution target for rendering systems: under 2 ms.
- Retained rendering avoids per-frame geometry rebuilds.
- Transient paths use per-frame host-visible buffers with bounded growth and telemetry overflow counters.
- Recipe-driven setup avoids allocating optional G-buffer/debug targets when they are not requested.

## Where Active Work Lives

- Near-term execution queue: `tasks/backlog/legacy-todo.md` (`P0 / P1 / P2 / Planned Constraints`).
- Medium/long-horizon planning: `docs/roadmap.md`.
- Historical migration narrative: `docs/migration/archive/plan.md` (archival index).
