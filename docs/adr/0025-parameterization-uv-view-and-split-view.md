# ADR 0025: Parameterization UV view — derived second view, not a separate entity

- **Status:** Accepted
- **Date:** 2026-07-13
- **Owners:** Graphics/Runtime/UI
- **Related tasks:** GEOM-063 (retired), RUNTIME-176 (retired), UI-036
  (retired), GRAPHICS-122 (retired optional GPU upgrade)

## Context

The end-to-end parameterization family (the retired GEOM-063 dispatch surface;
implemented LSCM, harmonic/Tutte, and BFF strategies; the runtime facade
delivered by retired RUNTIME-176; and the editor panel delivered by retired
UI-036) produces a per-vertex UV map (`v:texcoord`) for a selected mesh.
Unlike point-cloud consolidation, which mutates positions in place and needs no
new view, a parameterization result is only meaningful when the user can **see
the 2D UV layout** alongside the 3D mesh, inspect its aggregate diagnostics,
and **control** the map interactively (pins and supported boundary values). This
forces two coupled questions:

1. Should the UV layout be rendered as **its own ECS entity**?
2. How is the **resizable split view** (3D mesh ↔ 2D UV layout) delivered?

Verified current-state constraints (2026-07-13 rendering/UI architecture survey):

- **Single camera / single viewport / single backbuffer.** The renderer extracts
  one `RenderWorld` with one `CameraViewSnapshot` and one `Viewport`, draws the
  scene offscreen (`SceneColorHDR` → `SceneColorLDR`), blits to the swapchain with a
  fullscreen-triangle present pass, and draws ImGui as an overlay pass over the whole
  backbuffer. There is **no** multi-camera / multi-viewport / preview-view concept,
  and the 3D scene is **not** hosted inside an `ImGui::Image`.
- **ImGui docking is disabled** (`ImGuiConfigFlags_DockingEnable` is not set;
  `io.IniFilename` is intentionally `nullptr`). A resizable/movable **window** is
  free via `Runtime.EditorWindowRegistry`; a **docked splitter** would require
  enabling docking and solving layout persistence.
- **`ImDrawList` and bindless `ImGui::Image` already flow end-to-end.** The ImGui
  adapter copies arbitrary draw lists generically and forwards per-draw bindless
  texture indices, so a panel can draw 2D vector content (`AddLine`/`AddConvexPolyFilled`)
  or host a GPU texture (`ImGui::Image((ImTextureID)bindlessIndex, size)`) with **no
  renderer change**.
- **Entities render through presence-based hint components** (`RenderSurface`,
  `RenderEdges`, `RenderPoints`, `GeometrySources`, `SelectableTag`, `StableId`,
  `VisualizationConfig`) under the **single scene camera**. Derived geometry (gizmos,
  debug primitives, visualization overlays) is submitted as **render packets**, not
  entities, and is also drawn under that one camera.
- UV data already exists CPU-side as `v:texcoord`. The parameterization family
  returns aggregate `Geometry.Parameterization.Diagnostics`; atlas chart/seam
  records belong to `Geometry.UvAtlas` and are not synthesized by the runtime
  parameterization view model.

### How modern DCC tools do this

- **Blender** — a dedicated **UV Editor** area (resizable split, sync-selected with
  the 3D view) draws the UV layout as 2D edges/faces of the *same mesh* over an
  optional image/checker; it is an editor view of the mesh's UVs, not a separate
  scene object.
- **Houdini** — the **UV view** renders the `uv` attribute *as the same geometry in
  UV space*; the layout is literally the mesh drawn with UVs as positions in a
  separate view — a projection of one object, not a second object.
- **Maya** — the **UV Editor** is a separate panel showing UV shells over the 0–1 /
  UDIM tile grid with a checker background; again a second view of the mesh's UVs.
- **RizomUV / Unfold3D** — a dual, resizable 3D-|-UV layout with heavy interactive
  control (pin/cut/weld, brush relax, LSCM/ABF/optimize); the UV side is the
  flattened mesh, not a distinct entity.

The consensus is unambiguous: the UV layout is a **second view of the same mesh in
UV space**, with synced identity/selection and checker/texel-density/distortion
overlays — never a separate scene object.

## Decision

**The parameterization UV layout is a derived view/projection of the existing mesh
entity, not a separate ECS entity.** It shares the mesh's topology, `StableId`
identity, selection, and `v:texcoord` attribute. It is rendered in a dedicated,
resizable UV view that is a *different space* (UV) of the same entity.

**Delivery (staged, cheapest first):**

- **Delivered baseline — CPU `ImDrawList` layout in a resizable split window (retired UI-036).**
  A single registered editor window is split by a **manual draggable two-pane
  splitter** (ImGui child regions / a resizable table column with a stored ratio) into
  a controls pane and a UV-layout pane. The UV pane maps a runtime-built,
  pointer-free `SandboxEditorParameterizationViewModel` (per-vertex UVs,
  triangle index triples, finite bounds, and aggregate diagnostics) into
  pane-local space and draws it with `ImGui::GetWindowDrawList()`. This needs
  **no renderer change and no docking**, and matches the existing menu-first
  floating-window UX.
- **Delivered optional upgrade — GPU-shaded offscreen UV target (retired
  GRAPHICS-122).** For dense
  meshes and texel-density/texture/heatmap shading,
  `Extrinsic.Graphics.UvView` renders the selected surface's existing resident
  geometry with texcoords as positions into a retained offscreen target, and
  the panel presents its bindless index with an ImGui image draw. The typed,
  gated `UvViewPass` is a dedicated derived-view branch of the existing frame
  recipe; it does not add a second scene camera or general viewport contract.
  Runtime publishes the GPU target only after the current request and extent
  complete successfully. Until then, and whenever the device or geometry is
  unavailable, the CPU layout stays active with an explicit fallback status.
  Per-face distortion is accepted only when the successful result's canonical
  topology-to-face, exact-position, and exact-UV fingerprint matches the
  current view snapshot, preventing undo or later geometry/UV edits from
  projecting stale diagnostics.

**Control** flows through the config lane delivered by retired RUNTIME-176:
stable CPU strategy tokens and the typed LSCM pin/solver, harmonic boundary/pin,
and BFF boundary-mode values plus the UV-view render mode, background, and
heatmap toggle are serializable config applied through the validated
preview→apply path, with the retired UI-036 panel and agents/config files as
co-equal drivers. The bounded BFF contract has no cone placement/cutting
control. `gpu_shaded` selects presentation only; the config has no
optimized/GPU parameterization-solver selector while no such implementation
exists.

## Consequences

- Positive: one source of truth for mesh identity, topology, and `v:texcoord`; UV
  selection stays synced with the 3D mesh with no duplicate-entity bookkeeping; the
  first delivery ships with zero renderer/graphics changes and no docking dependency;
  the GPU upgrade is isolated and optional; the design matches every mainstream DCC.
- Trade-offs / risks: the CPU `ImDrawList` layout emits lines on the CPU (fine for
  typical meshes; the GRAPHICS-122 GPU path is intended for very dense meshes); a
  *docked* splitter (versus the manual two-pane splitter) and a *true* second
  viewport remain future opt-ins, not part of this decision.
- Baseline: GEOM-063 (surface), RUNTIME-176 (facade + config + UV view model),
  and UI-036 (panel + split view). GRAPHICS-122 supplies the optional
  GPU-shaded retained target while preserving that CPU baseline.

## Alternatives Considered

- **UV as its own ECS entity — rejected.** An entity with `GeometrySources = (u,v,0)`
  and render hints would reuse the entity/picking path, but it renders under the
  **single 3D scene camera**, so it appears *inside* the 3D scene (offset by a
  transform), not in a separate resizable pane — it does not by itself produce a split
  view. It also fragments the mesh's identity (two entities, two `StableId`s, two
  selection targets for one mesh) and contradicts the universal DCC model of a UV
  *view* of one object. Rejected as the primary model.
- **True second viewport + second camera — deferred.** The architecturally "correct"
  long-term split view, but it contradicts today's single-view assumptions in
  `RenderFrameInput`, `RenderWorld`, and `FrameRecipe` (one camera/viewport/backbuffer)
  and would require a multi-view concept across runtime extraction, the renderer
  contract, and the recipe. Deferred; not required for the family to be integrated and
  choosable.
- **Enable ImGui docking for a docked splitter — deferred.** One flag in
  `Runtime.ImGuiAdapter::ConfigureIo()`, but it also requires solving layout
  persistence (`IniFilename` is intentionally null). The manual two-pane splitter
  delivers a resizable split without that policy change; docking can be revisited
  independently if a full dockspace is ever wanted.

## Validation

- RUNTIME-176 retired at `CPUContracted`: contract tests assert the additive
  schema-v1 config round-trip, pointer-free UV view model, undoable
  `v:texcoord` writeback, deterministic configured execution, and
  `Editor`/`AgentCli`/`Programmatic` apply-source parity under the Null/default
  runtime path. No backend fallback telemetry is expected for this CPU-only
  family.
- UI-036 retired at `Operational`: the default CPU gate covers window
  registration, the view-model→pane-space projection, result rendering, and a
  produced ImGui frame. Its production Vulkan replay selected and
  parameterized a mesh through the registered window, showed the UV layout,
  kept the checker/grid inside the UV pane, and preserved the updated 3D UV
  state.
- GRAPHICS-122 retired at `Operational` with CPU contracts for request
  validation, retained-target readiness, resize generation, explicit
  background/heatmap degradation, and non-operational CPU fallback. Its final
  `gpu;vulkan` selection passed both the direct semantic readback of asymmetric
  islands across checker, texel-density, real bindless-texture, and
  low/mid/high/invalid heatmap probes, and the real Agent/CLI →
  `ReferenceTriangle` → EditorShell → ImGui runtime path.
