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
| `SceneColorLDR` | Swapchain format | Frame transient | Post-process + overlay composition target |
| `SelectionMask` | `R8_UNORM` | Frame transient | Reserved for future outline mask split |
| `SelectionOutline` | Swapchain format | Frame transient | Reserved for future standalone outline target |
| `Backbuffer` | Swapchain format | Imported | Final presentation destination only |

`FrameRecipe` determines which of these are allocated for a frame. Unused optional resources are not created.

## Pass Contract

| Pass | Inputs | Outputs | Initialization / Ownership |
|------|--------|---------|----------------------------|
| `PickingPass` | `SceneDepth` | `EntityId` | Clears both; no swapchain ownership |
| `SurfacePass` | `SceneDepth`, GPUScene buffers | forward: `SceneColorHDR`; deferred: `SceneNormal` + `Albedo` + `Material0`, `SceneDepth` | Opaque surface lane. Clears depth plus either HDR color or the G-buffer MRT set |
| `CompositionPass` | deferred: `SceneNormal`, `Albedo`, `Material0`, `SceneDepth` | deferred: `SceneColorHDR` | Fullscreen deferred lighting. No-op in forward mode |
| `LinePass` | `SceneColorHDR`, `SceneDepth` | `SceneColorHDR`, `SceneDepth` | Forward-overlay lane for wireframe/graph/debug lines; accumulates via `LOAD` |
| `PointPass` | `SceneColorHDR`, `SceneDepth` | `SceneColorHDR`, `SceneDepth` | Forward-overlay lane for point clouds/debug points; accumulates via `LOAD` |
| `PostProcessPass` | `SceneColorHDR` | `SceneColorLDR` | Initializes LDR target; internal temp when FXAA enabled |
| `SelectionOutlinePass` | `EntityId`, presentation target | presentation target | Alpha-blends via `LOAD` |
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

## Pipeline Order

`DefaultPipeline` execution order:

1. `PickingPass`
2. `SurfacePass`
3. `CompositionPass`
4. `LinePass`
5. `PointPass`
6. `PostProcessPass`
7. `SelectionOutlinePass`
8. `DebugViewPass`
9. `ImGuiPass`
10. `Present`

Lighting-path coexistence is now explicit:

- **Forward mode:** `SurfacePass` writes directly to `SceneColorHDR`; `CompositionPass` is a no-op; `LinePass` and `PointPass` accumulate onto the same HDR target.
- **Deferred mode:** `SurfacePass` writes only the G-buffer MRT set (`SceneNormal`, `Albedo`, `Material0`) plus depth; `CompositionPass` resolves those buffers into `SceneColorHDR`; `LinePass` and `PointPass` then execute as forward overlays on top of the lit HDR scene.

This establishes the current composition rule: deferred-capable opaque surfaces live in `SurfacePass`, while line/point/debug content remains in the forward lane. The same ordering is the extension point for future transparent or special-material forward overlays.

## Validation / Audit Expectations

- Render-graph introspection reports per-pass attachment metadata (resource name, format, load/store ops, imported flag).
- Render-graph introspection reports per-resource first/last read and write pass indices.
- Temporary audit logging may dump pass order, resource creation, transitions, and formats from `RenderSystem`.
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
- Keep push constants within device limits (compile/runtime checks).
- Keep backbuffer ownership explicit: only `FrameSetup` imports it, only `Present` finalizes it when `SceneColorLDR` exists.

## Performance Intent

- CPU frame contribution target for rendering systems: under 2 ms.
- Retained rendering avoids per-frame geometry rebuilds.
- Transient paths use per-frame host-visible buffers with bounded growth and telemetry overflow counters.
- Recipe-driven setup avoids allocating optional G-buffer/debug targets when they are not requested.

## Where Active Work Lives

- Near-term execution queue: `TODO.md` (`P0 / P1 / P2 / Planned Constraints`).
- Medium/long-horizon planning: `ROADMAP.md`.
- Historical migration narrative: `PLAN.md` (archival index).
