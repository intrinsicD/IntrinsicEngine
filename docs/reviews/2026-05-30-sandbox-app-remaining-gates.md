# Working Sandbox App — Remaining Gates (2026-05-30)

## Scope

This note inventories the **remaining gates** between the current branch state
and the `ExtrinsicSandbox` acceptance contract defined by
[`RUNTIME-095`](../../tasks/backlog/runtime/RUNTIME-095-working-sandbox-acceptance.md):
render at least one mesh, one graph, and one point cloud through the default
runtime/graphics path with working camera controls, entity/primitive selection,
selection outline, and core UI panels.

It supersedes the visible-triangle framing of
[2026-05-11 sandbox & graphics gap analysis](2026-05-11-sandbox-graphics-gap-analysis.md):
that review recorded a renderer that "cannot render a single visible pixel."
Since then the visible-triangle scaffold landed and the default recipe gained
operational pass routing, so the gate set has moved from "first pixel" to
"sandbox acceptance." Status below is from code inspection (file:line evidence),
not task-file placement.

## Executive summary

The default recipe now routes every canonical pass it declares, the device
factory can return an operational Vulkan device, and runtime mesh residency is
wired. The remaining gates are concentrated in three areas:

1. **Runtime geometry residency** for graph (extraction wiring) and point cloud
   (no packer yet), plus optional mesh primitive views.
2. **Runtime selection** — no runtime-side controller, stable-entity lookup, or
   primitive refinement exists; only the graphics-side picking/outline path.
3. **Asset + editor UI plumbing** — texture asset
   bridge, asset ingest ownership, or editor shell/panel modules.

The `MinimalDebug` scaffold is still present and must be retired before the
sandbox can claim acceptance on the default recipe.

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
| Retire `MinimalDebug` scaffold | [`GRAPHICS-081`](../../tasks/backlog/rendering/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) | **OPEN** | `kMinimalDebugSurfaceRecipeLabel`, `Pass.Surface.MinimalDebug`, `Pass.Present.MinimalDebug`, `BuildMinimalDebugSurfaceRecipe(...)` and executor routing still present (`FrameRecipe.cppm`/`.cpp`, `Graphics.Renderer.cpp`). RUNTIME-095 forbids treating MinimalDebug as final acceptance. |

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
| Texture asset bridge | [`RUNTIME-080`](../../tasks/backlog/runtime/RUNTIME-080-asset-bridges-texture.md) | **OPEN** | No runtime asset-bridge module. |
| Runtime ImGui platform/renderer adapter | [`RUNTIME-090`](../../tasks/done/RUNTIME-090-imgui-platform-renderer-adapter.md) | **OPEN** | No runtime ImGui adapter. Graphics-side `Extrinsic.Graphics.ImGuiOverlaySystem` + `Pass.ImGui` exist, but nothing produces `ImGuiOverlayFrame` records from runtime. |
| Asset model/texture ingest ownership | [`ASSETIO-001`](../../tasks/backlog/assets/ASSETIO-001-asset-model-texture-ingest-ownership.md) | **OPEN** | Backlog; no ingest ownership wired. |
| Sandbox editor shell + core panels | [`UI-001`](../../tasks/backlog/ui/UI-001-sandbox-editor-shell-panels.md) | **OPEN** | No editor-shell/panel modules in `src/runtime` or `src/app`. Depends on `RUNTIME-090` + `GRAPHICS-079`. |

### E. Optional / non-blocking

- [`RUNTIME-083`](../../tasks/backlog/runtime/RUNTIME-083-visualization-adapters.md)
  visualization adapters — optional for acceptance (RUNTIME-095 lists
  visualization/spatial-debug adapters as optional).

### F. Final acceptance

- [`RUNTIME-095`](../../tasks/backlog/runtime/RUNTIME-095-working-sandbox-acceptance.md)
  itself — **OPEN**, blocked by A–D above.

## Suggested dependency order to unblock RUNTIME-095

1. **RUNTIME-086 Slice B/C** (graph residency wiring) — already in progress; the
   shortest residency win after mesh.
2. **RUNTIME-087** (point-cloud residency) — completes the mesh/graph/point-cloud
   triad RUNTIME-095 must render.
3. **RUNTIME-089 → RUNTIME-092 → RUNTIME-093** (selection controller, stable
   lookup, primitive refinement) — the selection acceptance chain; `RUNTIME-088`
   feeds primitive selection.
4. **RUNTIME-080 + ASSETIO-001** (texture asset bridge, asset model/texture
   ingest ownership) — the asset-plumbing seams RUNTIME-095 names as required
   upstreams. These can land in parallel with the selection chain.
5. **UI-001** (editor panels on the completed ImGui adapter/pass path) — the UI
   acceptance chain.
6. **GRAPHICS-081** (retire MinimalDebug) — closure gate once the default recipe
   carries the sandbox.
7. **RUNTIME-095** — author the CPU/null + opt-in Vulkan acceptance once A–E land.

The remaining asset/UI-plumbing gates (`RUNTIME-080`, `ASSETIO-001`, `UI-001`)
are **required upstreams** per RUNTIME-095's Context section, so they stay on
the acceptance path: do not retire RUNTIME-095 until each is complete.
A procedurally authored acceptance scene can reduce how much *content* breadth
each seam must cover (RUNTIME-095 explicitly does not require every asset format
or visualization mode), but it does not remove the requirement that the texture
asset bridge and asset/texture ingest ownership seams exist and are wired.
