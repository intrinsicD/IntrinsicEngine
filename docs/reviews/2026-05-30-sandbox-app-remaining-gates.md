# Working Sandbox App — Remaining Gates (2026-05-30)

> **Closed (2026-06-04).** The scoped working-sandbox acceptance path is now
> retired by [`RUNTIME-095`](../../tasks/done/RUNTIME-095-working-sandbox-acceptance.md)
> at `Operational` on Vulkan-capable hosts. The closing smoke drives the
> runtime `Engine` for bounded frames with one mesh, one graph, one point cloud,
> the reference camera, selection/outline state, and the runtime-owned
> `SandboxEditorUi` attached, then asserts the default recipe records canonical
> `Present` with no canonical `SkippedUnavailable` pass. Broad file-format
> breadth, KTX decode, post-upload material re-resolution, full scene
> serialization, advanced PBR, transparency, Gaussian splats, and legacy
> deletion remain outside this review's acceptance scope.

## Scope

This note originally inventoried the **remaining gates** between the 2026-05-30
branch state and the `ExtrinsicSandbox` acceptance contract defined by
[`RUNTIME-095`](../../tasks/done/RUNTIME-095-working-sandbox-acceptance.md):
render at least one mesh, one graph, and one point cloud through the default
runtime/graphics path with working camera controls, entity/primitive selection,
selection outline, and core UI panels.

It supersedes the visible-triangle framing of
[2026-05-11 sandbox & graphics gap analysis](2026-05-11-sandbox-graphics-gap-analysis.md):
that review recorded a renderer that "cannot render a single visible pixel."
Since then the visible-triangle scaffold landed and the default recipe gained
operational pass routing, so the gate set moved from "first pixel" to
"sandbox acceptance." Rows below preserve the historical 2026-05-30 inspection
state; the closure note above is the current status.

## Executive summary

As of 2026-05-30, the default recipe routed every canonical pass it declared,
the device factory could return an operational Vulkan device, and runtime mesh
residency was wired. The remaining gates were concentrated in three areas:

1. **Runtime geometry residency** for graph (extraction wiring) and point cloud
   (no packer yet), plus optional mesh primitive views.
2. **Runtime selection** — no runtime-side controller, stable-entity lookup, or
   primitive refinement exists; only the graphics-side picking/outline path.
3. **Asset + editor UI plumbing** — texture/model asset handoff and editor UI
   command seams are CPU-contracted; the scoped operational sandbox proof is now
   closed by RUNTIME-095.

2026-06-02 update: `GRAPHICS-081` retired the bootstrap recipe scaffold. Sandbox
acceptance depended on the canonical default recipe, UI/editor panels, and the
remaining asset/runtime acceptance gates rather than any bootstrap path.

2026-06-04 update: `RUNTIME-095` retired the scoped sandbox acceptance path at
`Operational` on Vulkan-capable hosts; remaining rows below are historical
evidence for the path that led to that retirement.

## What has landed (no longer a gate)

- **Default-recipe pass routing** — the executor records culling, depth prepass,
  shadow, forward/deferred surface + lighting, line, point, picking (entity /
  face / edge / point id + readback), selection outline, postprocess (histogram,
  bloom, tonemap, AA edge/blend/resolve), transient debug, visualization
  overlay, debug view, and present
  (`src/graphics/renderer/Graphics.Renderer.cpp` ~1607–2231). Covers
  `GRAPHICS-072..078` (done).
- **Operational device path** — the device factory returns an operational Vulkan
  device when `kPromotedVulkanAvailable && config.EnablePromotedVulkanDevice`,
  else falls back to Null (`src/runtime/Runtime.Engine.cpp`,
  `GRAPHICS-080` done). Note: `CreateRenderer()` returns a single renderer class
  ("NullRenderer") by design; operational-ness is gated on
  `RHI::IDevice::IsOperational()`, not the renderer class — this is the canonical
  design, not an open gate.
- **Mesh residency** — `Extrinsic.Runtime.MeshGeometryPacker` exists and is
  imported by `Runtime.RenderExtraction.cpp` (`RUNTIME-085`, done at
  `CPUContracted`).
- **Camera controllers** — `RUNTIME-081` (done).
- **Spatial-debug + visualization upload helpers** — `RUNTIME-082`,
  `GRAPHICS-077`, `GRAPHICS-078` (done).
- **Default-recipe ImGui pass routing** — `GRAPHICS-079` (done). The renderer
  owns the ImGui consumer route, retained font atlas, transient upload helper,
  `FrameRecipe.PresentSource` write topology, and per-command bindless
  user-texture sampling. The opt-in `ImGuiSurfaceGpuSmoke` is registered and
  skips on hosts without an operational GLFW/Vulkan lane.

## Remaining gates

### A. Default-recipe rendering completion

| Gate | Task | State | Evidence |
|---|---|---|---|
| Retire bootstrap recipe scaffold | [`GRAPHICS-081`](../../tasks/done/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) | **DONE** | Scaffold recipe code, pass classes, shaders, tests, and renderer selector/readback hooks removed on 2026-06-02. RUNTIME-095 now depends on the canonical default recipe only. |

### B. Runtime geometry residency (mesh / graph / point cloud)

| Gate | Task | State | Evidence |
|---|---|---|---|
| Graph residency extraction wiring | [`RUNTIME-086`](../../tasks/done/RUNTIME-086-geometrysources-graph-residency.md) | **IN PROGRESS** | Slice A packer `Extrinsic.Runtime.GraphGeometryPacker` exists but is **not** imported by `Runtime.RenderExtraction.cpp`. Slice B (extraction wiring) and Slice C (dirty/retire) remain. |
| Point-cloud residency | [`RUNTIME-087`](../../tasks/done/RUNTIME-087-geometrysources-pointcloud-residency.md) | **OPEN** | No point-cloud packer module exists in `src/runtime/`. |
| Mesh primitive view lifecycle | [`RUNTIME-088`](../../tasks/done/RUNTIME-088-mesh-primitive-view-lifecycle.md) | **IN PROGRESS** | Slice A landed the standalone edge/vertex derivation packers + control surface; Slice B owns the extraction-cache residency wiring. |

### C. Runtime selection

| Gate | Task | State | Evidence |
|---|---|---|---|
| Selection controller + snapshot handoff | [`RUNTIME-089`](../../tasks/done/RUNTIME-089-selection-controller.md) | **OPEN** | No runtime selection module; only graphics-side `Graphics.SelectionSystem.cpp` + `ECS.Component.Selection`. |
| Stable entity lookup sidecar | [`RUNTIME-092`](../../tasks/done/RUNTIME-092-stable-entity-lookup.md) | **OPEN** | No runtime stable-id/live-entity lookup module. |
| Primitive selection refinement | [`RUNTIME-093`](../../tasks/done/RUNTIME-093-primitive-selection-refinement.md) | **IN PROGRESS** | Slice A landed the standalone `Extrinsic.Runtime.PrimitiveSelectionRefinement` module (hint-based mesh/graph/point-cloud refinement); `SelectionController` integration owned by Slice B. |

Graphics-side picking readback + outline (`GRAPHICS-074`) is **done** and is the
upstream these runtime gates consume.

### D. Asset + editor UI plumbing

| Gate | Task | State | Evidence |
|---|---|---|---|
| Texture asset bridge | [`RUNTIME-080`](../../tasks/done/RUNTIME-080-asset-bridges-texture.md) | **SUPERSEDED / DONE** | Capability shipped under `ASSETIO-001` as `Extrinsic.Runtime.AssetModelTextureHandoff`; the umbrella was retired without re-implementation on 2026-06-03. |
| Runtime ImGui platform/renderer adapter | [`RUNTIME-090`](../../tasks/done/RUNTIME-090-imgui-platform-renderer-adapter.md) | **DONE** | `Extrinsic.Runtime.ImGuiAdapter` produces `ImGuiOverlayFrame` records from runtime-owned ImGui frames; `GRAPHICS-079` consumes them through the renderer-owned `Pass.ImGui` path. |
| Asset model/texture ingest ownership | [`ASSETIO-001`](../../tasks/done/ASSETIO-001-asset-model-texture-ingest-ownership.md) | **DONE** | Retired at `CPUContracted`: promoted asset routing, geometry/model/texture decoder dispatch, texture GPU-residency handoff, model-scene ECS `GeometrySources` materialization, deterministic embedded child texture assets, and material `AssetId` binding records are covered. RUNTIME-095 closes the scoped operational sandbox proof; broader file-format visual coverage remains future work. |
| Sandbox editor shell + core panels | [`UI-001`](../../tasks/done/UI-001-sandbox-editor-shell-panels.md) | **DONE** | Retired at `CPUContracted`: `Extrinsic.Runtime.SandboxEditorUi` covers scene hierarchy, inspector, selection/refined primitive details, file/import entry command execution through `Engine::ImportAssetFromPath(...)`, camera/controller and mesh primitive-view commands, selected-entity `SpatialDebugBinding` / `VisualizationConfig`, and visualization adapter-binding routing through `RenderExtractionCache`. RUNTIME-095 closes the scoped operational visual/interactive proof with the editor shell attached. |
| Visualization adapter umbrella | [`RUNTIME-083`](../../tasks/done/RUNTIME-083-visualization-adapters.md) | **DONE** | Slices A-E add the runtime `VisualizationAdapters` umbrella, `PropertyScalarAdapter`, `KMeansLabelAdapter`, `VectorFieldAdapter`, `IsolineAdapter`, `HtexMetadataAdapter`, registry contract, runtime-owned extraction-cache adapter bindings, scalar/non-scalar packet handoff into `RuntimeRenderSnapshotBatch::Visualization*`, deterministic Htex/fragment-bake packet contracts with `RecreateHtex` streaming scheduling, and extraction-side packet/error stats. |

### E. Optional / non-blocking

- [`RUNTIME-083`](../../tasks/done/RUNTIME-083-visualization-adapters.md)
  visualization adapters — optional for acceptance (RUNTIME-095 lists
  visualization/spatial-debug adapters as optional).

### F. Final acceptance

- [`RUNTIME-095`](../../tasks/done/RUNTIME-095-working-sandbox-acceptance.md)
  itself — **DONE** (2026-06-04, `Operational` on Vulkan-capable hosts). Slice 1
  landed 2026-06-03 (`Test.RuntimeSandboxAcceptance.cpp`,
  `integration;runtime;graphics`): CPU/null end-to-end acceptance proving one
  mesh + one graph + one point cloud all reside through a single extraction with
  distinct `GpuWorld` handles, a finite/invertible camera-controller frame
  camera, whole-entity selection per family, and the sandbox editor panel frame
  enumerating the scene. Slice 2 landed the same day: a mocked pick readback
  resolves a mesh Face / graph Edge / point-cloud Point through
  `RefinePickReadbackResult`, and the `RenderWorld.Selection` outline snapshot is
  populated for the selected entity. Slice 3 now drives the bounded runtime
  `Engine` on the promoted Vulkan path with `SandboxEditorUi` attached and passes
  the `RuntimeSandboxAcceptanceGpuSmoke.AcceptanceSceneReachesOperationalDefaultRecipePresent`
  smoke.

## Completed dependency order for RUNTIME-095

1. **RUNTIME-086 Slice B/C** (graph residency wiring) — completed the graph
   residency lane after mesh.
2. **RUNTIME-087** (point-cloud residency) — completed the mesh/graph/point-cloud
   triad RUNTIME-095 renders.
3. **RUNTIME-089 -> RUNTIME-092 -> RUNTIME-093** (selection controller, stable
   lookup, primitive refinement) — completed the selection acceptance chain;
   `RUNTIME-088` feeds primitive selection.
4. **ASSETIO-001** (asset model/texture ingest ownership, which subsumed the
   retired `RUNTIME-080` texture bridge as `AssetModelTextureHandoff`) — the
   asset-plumbing seam RUNTIME-095 names as a required upstream.
5. **UI-001** — completed at `CPUContracted`; editor panels and command seams are
   consumed by the RUNTIME-095 smoke through `SandboxEditorUi`.
6. **GRAPHICS-081** — completed on 2026-06-02; no bootstrap recipe acceptance
   path remains.
7. **RUNTIME-095** — completed the CPU/null + opt-in Vulkan acceptance once A–E landed.

The retired `ASSETIO-001` (which subsumed `RUNTIME-080`) and `UI-001` seams were
required upstreams per RUNTIME-095's Context section. The closing acceptance uses
a procedurally authored scene to keep content breadth reviewable, attaches the
runtime-owned editor UI shell, and keeps file/import breadth outside the
RUNTIME-095 stop-state.
