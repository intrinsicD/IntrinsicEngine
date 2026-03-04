# IntrinsicEngine — Roadmap

This document tracks medium- and long-horizon feature planning for IntrinsicEngine. Completed work is recorded in git history; only future-facing items belong here.

---

## Prioritization

Features are ordered by their dependency graph. Each phase builds on the previous.

```
Dependency graph (→ means "is required by"):

Post-processing ──→ Shadow mapping (composites into HDR buffer)
(HDR pipeline)    → Transparency / OIT (needs HDR blend target)
                  → Mesh rendering modes (HDR output assumed)

Sub-entity select → Geometry processing (interactive operator input)
                  → Measurement tools (click-to-pick points)
```

---

## Phase 1 — Foundation

### Post-Processing Pipeline

The forward pass currently writes directly to the swapchain. An HDR intermediate render target (R16G16B16A16_SFLOAT) is the prerequisite for nearly every visual feature that follows.

**Minimum viable chain:**
- **Tone mapping:** HDR → LDR conversion (ACES, Reinhard, filmic, AgX).
- **FXAA / TAA:** Anti-aliasing. TAA is important for temporal stability on thin geometry like wireframes and point clouds.

**Incremental additions (can land alongside later phases):**
- SSAO (HBAO+ or GTAO) for contact shadows.
- Bloom (bright-pass threshold + Gaussian blur cascade + additive blend).
- Depth of field (optional, for presentation renders).
- Color grading (LUT-based or parametric).

**Architecture:** Post-processing should be a chain of `RenderGraph` passes on the HDR color buffer. Each effect is an independent, runtime-toggleable pass.

---

## Phase 2 — Core UX

These features turn the engine from a viewer into an interactive tool.

### Transform Gizmos

Translate/rotate/scale handles rendered via `LinePass` transient path. Without these, the only way to move objects is typing numbers into the Inspector.

- Three axis arrows + plane handles + center sphere (translate).
- Three axis rings / trackball (rotate).
- Three axis handles with cube endpoints (scale).
- Snap modes: grid snap, angle snap.
- Space modes: world-space vs. local-space orientation.
- Multi-object pivot: centroid, first-selected, or custom.

### UI Improvements

Each improvement is independent and can land incrementally:
- **Dockable panel layout:** ImGui docking branch — free panel arrangement, save/restore layouts.
- **Viewport toolbar:** Render mode switching, wireframe overlay, debug views directly in the 3D viewport.
- **Undo/redo stack:** Command-pattern undo for all property changes.
- **Multi-object editing:** Edit shared properties across all selected entities.
- **Asset browser:** Thumbnail previews, drag-and-drop import, directory navigation.
- **Console / log panel:** Scrollable, filterable log output (currently stdout only).
- **Keyboard shortcuts:** Configurable hotkeys for common operations.
- **Context menus:** Right-click on entities for delete, duplicate, rename, focus camera.
- **Dark/light theme:** Configurable ImGui theme presets.

### Scene Serialization

No save/load mechanism exists. Required for any practical workflow.

- **Scene save/load:** Serialize entity hierarchy, component data, asset references (JSON, binary, or glTF extension).
- **Project files:** Scene + asset references + editor layout as a project bundle.

---

## Phase 3 — Rendering Variety

Each item is a render feature registered via `FeatureRegistry`.

### Mesh Rendering Modes

Currently only a single forward PBR pass (metallic-roughness) exists via `SurfacePass`. Implement as a swappable `ShadingMode` enum — same vertex pipeline, only fragment shaders differ.

- **PBR extensions:** Clearcoat, sheen, transmission (glTF PBR extensions).
- **Flat shading:** Per-face constant color. Useful for low-poly and CAD visualization.
- **Gouraud / Phong:** Classic per-vertex and per-fragment Blinn-Phong.
- **Matcap:** View-space normal → texture lookup. Fast artistic shading, no lights needed.
- **NPR:** Toon/cel shading, Gooch shading, hatching/cross-hatching, pencil/sketch style.
- **Curvature visualization:** Per-vertex mean/Gaussian curvature → diverging colormap.
- **Scalar field visualization:** Arbitrary per-vertex scalars → configurable colormaps (viridis, jet, coolwarm).
- **Normal visualization:** Normals as color (world/view-space RGB) or hedgehog lines.
- **UV checker:** Checkerboard pattern via UVs for parameterization quality inspection.

### Shadow Mapping

No shadow support exists. Shadows are critical for spatial understanding.

- Cascaded shadow maps (CSM) for directional lights.
- Point light shadow maps (cubemap or dual-paraboloid) if point lights are added.
- PCF or variance shadow maps for soft edges.
- Shadow pass reuses `SurfacePass` geometry, writes depth only.

### Benchmarking & Profiling

`Core::Telemetry` provides basic lock-free ring-buffered metrics. No GPU profiling or reproducible benchmark infrastructure exists.

- **GPU timing:** Vulkan timestamp queries per render pass, surfaced in the Performance panel.
- **Pipeline statistics:** Vertex/fragment invocations, clipping primitives.
- **CPU frame profiling:** Per-system timing in the FrameGraph, exposed via telemetry.
- **Reproducible benchmark scenes:** Known entity counts, geometry complexity, camera paths.
- **Benchmark runner:** Automated N-frame run, min/avg/max/p99 frame times, JSON/CSV output.
- **Regression detection:** Cross-commit comparison with configurable threshold.
- **Memory profiling:** GPU allocation tracking (VMA statistics), CPU allocator high watermarks.

### Debug Visualization — Remaining

Octree, KD-tree, BVH, bounding volumes, contact manifolds, and convex hulls are all functional via `DebugDraw` + `LinePass`. Remaining work:
- Uniform grid overlay (wireframe cells with occupancy coloring).
- Per-category UI polish.

---

## Phase 4 — Advanced Rendering

New geometry types and rendering techniques building on Phase 1–3 infrastructure.

### Point Cloud Rendering — Advanced Modes

Basic modes (FlatDisc, Surfel, EWA) are complete. Each new mode is a shader + pipeline variant registered in `PointPass`.

- **Gaussian Splatting (3DGS):** Oriented anisotropic splats via a dedicated compute rasterizer or tile-based sort+blend pipeline. Dominant representation in neural radiance field research.
- **Potree-style octree LOD:** Hierarchical out-of-core streaming for billion-point clouds. Octree nodes loaded on demand by camera distance and screen-space error budget.
- **Depth peeling for OIT:** Order-independent transparency for splat blending.

### Graph / Wireframe Rendering — Remaining

Core rendering infrastructure and CPU-side layout algorithms are complete. Remaining:
- **Layout algorithm UI integration:** Interactive selection and real-time re-layout.
- **Halfedge debug view:** Vertices as points, edges as directed arrows, face normals.
- **Barycentric wireframe shader:** Alternative mesh wireframe via fragment-shader barycentric coordinates.

### Transparency / OIT

No transparency support. Required for translucent surfaces, point cloud blending, and X-ray visualization.

- **Weighted blended OIT** (McGuire & Bavoil): Simple, fast, approximate — good default.
- **Per-pixel linked lists:** Exact but memory-hungry and variable performance.
- **Depth peeling:** Exact, predictable memory, but multi-pass.

---

## Phase 5 — Advanced Interaction & Geometry Processing

Sub-mesh selection and geometry operators for research workflows.

### Sub-Entity Selection

Current selection is entity-level only. `HalfedgeMesh` provides the topological foundation.

**Selection modes:**
- Vertex, edge, face selection with highlighted overlays.
- Lasso, box, paint-brush area selection.
- Connected-component flood fill, angle-based region growing.

**Architecture:** Dedicated GPU picking pass writing `(EntityID, PrimitiveID, BarycentricCoords)`. CPU-side adjacency traversal via `HalfedgeMesh`. Per-entity bitsets for selected sub-elements.

### Geometry Processing — Remaining

18 operators are complete. Remaining:
- **Exact Boolean CSG:** Robust triangle clipping + stitched remeshing for partial-overlap union/intersection/difference. The baseline (disjoint/full-containment) is done.

### Clipping Planes & Cross-Sections

Essential for inspecting interiors of scanned objects, point clouds, and volumetric data.

- Up to 6 user-defined clip planes, positioned via gizmos.
- Cross-section fill (solid color or hatch pattern).
- Clip volumes (combine planes into convex regions for ROI isolation).

### Measurement & Annotation Tools

- Point-to-point distance with leader lines.
- Angle measurement (three-point).
- Area measurement (face region selection).
- Volume measurement (closed mesh, via divergence theorem).
- Persistent 3D text annotations as ECS components.

---

## Phase 6 — Long-Term

### Scripting Layer

Python (pybind11) or Lua (sol2) bindings for:
- ECS entity manipulation (create, destroy, set/get components).
- Geometry operator invocation (simplify, smooth, remesh).
- UI panel scripting (ImGui bindings).
- Custom per-frame logic (registered as FrameGraph systems).

Python bindings are particularly valuable for ML/scientific workflow integration (NumPy, PyTorch, Open3D interop). Do *not* attempt `.so`/`.dll` dynamic plugins with C++20 modules — the ABI is not stable.

### Data I/O — Remaining Formats

The two-layer architecture (I/O backend + format loaders) is complete with 8 importers and 3 exporters. Remaining:
- **LAS/LAZ** (LiDAR point clouds).
- **FBX** (via Assimp or ufbx) and deeper glTF extraction (materials, hierarchy, cameras, lights).
- **HDR/EXR** (environment maps, HDR textures).
- **Gaussian splat formats:** `.ply` (3DGS standard), `.splat` (compressed variants).
- Export on worker thread with progress callback.
- I/O backend Phase 1: archive/pack-file backend, async I/O.

---

## Ongoing

- **Shader hot-reload:** File-watcher integration for automatic recompilation on save. Implement alongside mesh rendering modes for fast shader iteration.
- **Port-based testing boundaries:** Type-erased "port" interfaces for filesystem, windowing, and time so subsystems can be tested without Vulkan. Implement opportunistically as new subsystems are added.

---

## Rendering Modality Redesign Vision

*Long-term architectural vision. The three-pass architecture (`SurfacePass`, `LinePass`, `PointPass`) is the stepping stone toward this model.*

The goal is a unified architecture where mesh, graph, and point cloud rendering are explicit top-level **approaches**, each exposing multiple **modes** selectable at runtime per-view and per-entity.

### Target Approach/Mode Matrix

| Approach | Modes |
|----------|-------|
| **Mesh** | ShadedTriangles, Wireframe, VertexPoints, NormalsDebug |
| **Graph** | LineList, TubeImpostor, NodeDiscs, EdgeHeatmap |
| **Point** | FlatDisc, Surfel, EWA, PotreeAdaptive, GaussianSplat |

### Data-Oriented Design

Approach classification produces `ApproachPacketSoA` (entityId, approach, mode, handles) in a single O(N) pass. Mode policy resolves from stacked levels: global defaults → view override → entity tag → debug forced mode. One indirect command buffer per approach, persistent pipelines, mode selection via push constants.

### Migration Path

- Phase 0: Introduce enums + config plumbing (no visual change).
- Phase 1: Extract/rename pass logic into approach passes.
- Phase 2: Graph approach formalization with mode params and UI.
- Phase 3: Point mode expansion (PotreeAdaptive, unified mode table).
- Phase 4: Cleanup legacy toggles and compatibility shims.

Runtime feature flag `RenderApproachV2` gates the full path for one-click rollback.

---

## Design Principles

### Subsystems: Interfaces vs. Concrete Types

**Concrete-by-default**, with dependency injection at construction and **interfaces only at boundaries**.

- Concrete types for hot-path subsystems (FrameGraph, scheduler, render graph, ECS update).
- Ports/adapters (pure virtual or type-erased) for boundary dependencies: filesystem, windowing, time, telemetry sinks.
- Testing model: instantiate concrete subsystems with fake ports (test doubles).
