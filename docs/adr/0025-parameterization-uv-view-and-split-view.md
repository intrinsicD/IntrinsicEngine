# ADR 0025: Parameterization UV view — derived second view, not a separate entity

- **Status:** Proposed
- **Date:** 2026-07-13
- **Owners:** Graphics/Runtime/UI
- **Related tasks:** GEOM-063, RUNTIME-176, UI-036, GRAPHICS-122

## Context

The end-to-end parameterization family (GEOM-063 dispatch surface; ARAP/SLIM/BFF/SCP
strategies; runtime facade RUNTIME-176; editor panel UI-036) produces a per-vertex
UV map (`v:texcoord`) for a selected mesh. Unlike point-cloud consolidation, which
mutates positions in place and needs no new view, a parameterization result is only
meaningful when the user can **see the 2D UV layout** — chart placement, seams,
flips, and distortion — alongside the 3D mesh, and **control** the map interactively
(pins, BFF boundary targets, cones). This forces two coupled questions:

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
- UV data already exists CPU-side: `v:texcoord`, `Geometry.UvAtlas` chart/seam
  records, and `Geometry.Parameterization.Diagnostics` per-face distortion.

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

- **First delivery — CPU `ImDrawList` layout in a resizable split window (UI-036).**
  A single registered editor window is split by a **manual draggable two-pane
  splitter** (ImGui child regions / a resizable table column with a stored ratio) into
  a controls pane and a UV-layout pane. The UV pane maps a runtime-built,
  pointer-free `SandboxEditorParameterizationViewModel` (per-vertex UVs, face triples,
  chart/seam records, per-face distortion) into pane-local space and draws it with
  `ImGui::GetWindowDrawList()`. This needs **no renderer change and no docking**, and
  matches the existing menu-first floating-window UX.
- **Optional upgrade — GPU-shaded offscreen UV target (GRAPHICS-122).** For dense
  meshes and texel-density/texture/heatmap shading, render the mesh's UV-space
  residency to a small offscreen target with an orthographic UV camera and present it
  through `ImGui::Image` (bindless). This reuses the mesh's existing `GeometrySources`
  residency in UV space — still a *derived view of the one entity*, not a new entity —
  and falls back to the CPU layout when no device is operational.

**Control** flows through the config lane (RUNTIME-176): strategy/backend/params, BFF
boundary-target mode and cones, and pins are serializable config applied through the
validated preview→apply path, with the UI-036 panel and agents/config files as
co-equal drivers.

## Consequences

- Positive: one source of truth for mesh identity, topology, and `v:texcoord`; UV
  selection stays synced with the 3D mesh with no duplicate-entity bookkeeping; the
  first delivery ships with zero renderer/graphics changes and no docking dependency;
  the GPU upgrade is isolated and optional; the design matches every mainstream DCC.
- Trade-offs / risks: the CPU `ImDrawList` layout emits lines on the CPU (fine for
  typical meshes; the GRAPHICS-122 GPU path is the answer for very dense meshes); a
  *docked* splitter (versus the manual two-pane splitter) and a *true* second
  viewport remain future opt-ins, not part of this decision.
- Follow-ups: GEOM-063 (surface), RUNTIME-176 (facade + config + UV view model),
  UI-036 (panel + split view), GRAPHICS-122 (optional GPU-shaded target).

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

- UI-036: default CPU gate covers window registration, the view-model→pane-space
  projection, and result/fallback rendering (model-level, no ImGui frame); the
  interactive proof (parameterize a disk mesh, see the UV layout beside the updated
  3D checker) is cited from a Vulkan-capable host run.
- RUNTIME-176: contract tests assert the config round-trip, the pointer-free UV view
  model, `Editor`/`AgentCli`/`Programmatic` apply-source parity, and honest backend
  fallback telemetry on the Null device.
- GRAPHICS-122: opt-in `gpu;vulkan` readback smoke asserts a non-empty UV target on a
  Vulkan-capable host and CPU-layout fallback on a non-operational device.
