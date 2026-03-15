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

### Post-Processing Pipeline (**MVP complete**)

The HDR post-processing foundation is in place. Scene passes render to an `R16G16B16A16_SFLOAT` intermediate target ("SceneColor"), and the `PostProcessPass` converts to LDR via tone mapping with selectable anti-aliasing (None, FXAA, or SMAA). Runtime toggles are exposed via `FeatureRegistry`.

**Tone mapping:** ACES, Reinhard, and Uncharted 2 (Hable 2010 filmic) operators are selectable at runtime. Exposure is adjustable via UI slider.

**Bloom:** 5-level progressive downsample/upsample chain (Jimenez 2014, 13-tap downsample + 9-tap tent upsample). Soft threshold with configurable knee curve. Composited additively in HDR space before tone mapping. Threshold, intensity, and filter radius exposed via UI.

**Color grading:** Full lift/gamma/gain (ASC CDL-style) color grading applied in linear space after tone mapping. Controls: saturation, contrast (midtone pivot at 0.18), per-channel lift (shadow tint), gamma (midtone power), gain (highlight multiplier), white balance (color temperature + green-magenta tint). All parameters exposed via View Settings panel with one-click reset to neutral.

**Render graph debug dump:** `DumpRenderGraphToString()` produces a human-readable snapshot of pass execution order (with per-pass attachment metadata) and resource lifetimes (with read/write pass ranges).

**Render target inspection:** Enhanced Render Target Viewer panel with human-readable resource names, per-attachment format/load/store tooltips, and a resource lifetime table (dimensions, format, imported vs. transient, alive-range). Click any resource to select it for debug visualization.

**Anti-aliasing:** Selectable via `AAMode` enum — None, FXAA, or SMAA (Jimenez et al. 2012). SMAA is a 3-pass morphological AA: luma-based edge detection → blend weight calculation (with procedurally generated area and search lookup textures) → neighborhood blending. Default is SMAA. Configurable edge threshold and search step counts via View Settings panel.

Long-horizon additions (after MVP): SSAO, DOF.

---

## Phase 2 — Core UX

These features turn the engine from a viewer into an interactive tool.

### Transform Gizmos (**MVP complete**)

`Graphics::TransformGizmo` is an ImGuizmo-backed editor wrapper. The engine caches selection/camera state during `OnUpdate()`, then executes the gizmo during the active ImGui frame through a lightweight overlay callback so transform interaction stays in the same input/render path as the rest of the editor UI.

- **Three modes:** Translate (arrows + plane handles), Rotate (circles per axis), Scale (axis lines + uniform center cube).
- **World/local space:** Gizmo orientation follows entity rotation in local mode. For parented entities, manipulation happens in world space and is converted back to parent-local `Transform::Component` via `Transform::TryComputeLocalTransform()`.
- **Snap:** Configurable increments for translation, rotation (degrees), and scale.
- **Multi-selection:** Centroid or first-selected pivot strategy. All selected entities transform together via shared pivot + world-space delta.
- **Viewport toolbar:** ImGui panel for mode switching, space toggle, pivot selection, and snap configuration.
- **Keyboard shortcuts:** W=Translate, E=Rotate, R=Scale, X=Toggle World/Local.
- **Mouse integration:** Gizmo consumes LMB input during drag, blocking entity selection.

Future: undo/redo integration.

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

### Scene Serialization (**MVP complete**)

JSON-based scene save/load is implemented (`Runtime::SaveScene()` / `Runtime::LoadScene()`). The serializer covers entity names, transforms, hierarchy, asset source paths, visibility, and rendering parameters. GPU state is reconstructed on load by re-importing assets from recorded source paths. Editor UI exposes File → Save/Load Scene with dirty-state prompts.

Future extensions: project-level files (scene path + editor layout + camera preset), binary scene format for large scenes, undo/redo integration.

---

## Phase 3 — Rendering Variety

Each item is a render feature registered via `FeatureRegistry`.

### Rendering Modes

When an entity has at least one of the point cloud, graph, or mesh components (meaning Vertices, Edges or Faces) the UI allows to select 1d or 3d properties to be visualized on their respective geometric domains.
- **Scalar field visualization:** Per-vertex, Per-edge, Per-face scalar -> colormap (viridis, jet, coolwarm).
- **Scalar field controls:** Optional isolines/contours, value binning (quantization), and clamping/windowing for robust range control.
- **Vector field visualization:** Per-vertex, Per-edge, Per-face vector -> RGB color or glyphs (arrows, hedgehogs).
- **RGB color visualization:** Per-vertex, Per-edge, Per-face RGB color (e.g. from vertex colors or point cloud attributes).

### Mesh Rendering Modes

Currently only a single forward PBR pass (metallic-roughness) exists via `SurfacePass`. Implement as a swappable `ShadingMode` enum — same vertex pipeline, only fragment shaders differ.

- **PBR extensions:** Clearcoat, sheen, transmission (glTF PBR extensions).
- **Flat shading:** Per-face constant color. Useful for low-poly and CAD visualization.
- **Gouraud / Phong:** Classic per-vertex and per-fragment Blinn-Phong.
- **Matcap:** View-space normal → texture lookup. Fast artistic shading, no lights needed.
- **NPR:** Toon/cel shading, Gooch shading, hatching/cross-hatching, pencil/sketch style.
- **UV checker:** Checkerboard pattern via UVs for parameterization quality inspection.

### Shadow Mapping

No shadow support exists. Shadows are critical for spatial understanding.

- Cascaded shadow maps (CSM) for directional lights.
- Point light shadow maps (cubemap or dual-paraboloid) if point lights are added.
- PCF or variance shadow maps for soft edges.
- Shadow pass reuses `SurfacePass` geometry, writes depth only.

### Benchmarking & Profiling (**MVP complete**)

GPU timestamp queries per major render pass via `RHI::GpuProfiler`, CPU per-system timings via `RenderGraph` instrumentation, and a deterministic benchmark runner (`Core::Benchmark::BenchmarkRunner`) with JSON output are in place. The Performance panel shows per-pass GPU+CPU timings. Headless benchmark mode (`--benchmark <frames> --out file.json`) runs a fixed frame count and exits. Threshold-based regression checking via `tools/check_perf_regression.sh` supports avg/p99 frame time and min FPS gates.

Long-horizon additions: GPU-driven pass culling profiling, multi-scene benchmark suites, historical regression tracking with baseline storage.

### Debug Visualization — Remaining

Octree, KD-tree, BVH, bounding volumes, contact manifolds, and convex hulls are all functional via `DebugDraw` + `LinePass`. Remaining work:
- Uniform grid overlay (wireframe cells with occupancy coloring).
- Per-category UI polish.

---

## Phase 4 — Advanced Rendering

New geometry types and rendering techniques building on Phase 1–3 infrastructure.

### Point Cloud Rendering — Advanced Modes

Basic modes (FlatDisc, Surfel, EWA, Sphere impostor) are complete. Each new mode is a shader + pipeline variant registered in `PointPass`.

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

### Primitive Selection (**Click selection + GPU PrimitiveID complete**)

Click-based sub-element selection is implemented with GPU-native precision. A dual-channel MRT picking pipeline produces both `EntityId` and `PrimitiveId` in one frame via three dedicated pick pipelines (mesh, line, point). `ElementMode` (Entity/Vertex/Edge/Face) enables sub-entity picking with highlighted overlays (red spheres for vertices, yellow lines for edges, blue tinted triangles for faces). Shift-click toggles multi-select. `SubElementSelection` tracks per-entity sets of selected vertex, edge, and face indices. `SelectionModule` uses GPU PrimitiveID directly for face/edge/point selection; CPU refinement via KD-tree is retained as fallback for vertex-from-triangle resolution. Geodesic distance UI integration is functional (heat-method distances from selected source vertices).

**Remaining area selection modes:**
- Lasso, box, paint-brush area selection.
- Connected-component flood fill, angle-based region growing.

### GPU Geometry Processing Backend

Infrastructure required to move heavyweight geometry operators from CPU to GPU.

- **CUDA support (Phase 1 — done):** `RHI::CudaDevice` wrapper with driver API lifecycle (init, context, streams, memory alloc/free), `RHI::CudaError` domain error type, conditional CMake build via `INTRINSIC_ENABLE_CUDA`. Runtime tests skip gracefully when no CUDA driver is present.
- **CUDA-Vulkan interop (Phase 2 — next):** Export Vulkan buffers/timeline semaphores for zero-copy import into CUDA. `RHI::CudaInterop` class with explicit cross-API synchronization.
- **Compute dispatch abstraction (Phase 3):** `Runtime::ComputeBackend` subsystem owned by `Engine`, selecting between Vulkan Compute, CUDA, and CPU based on availability.
- **First GPU workload (Phase 4):** GPU-accelerated K-means clustering for point clouds, validating the compute pipeline end-to-end.

### Geometry Processing — Remaining

Core operators are complete (16 mesh operators + DEC + graph builders/layouts + collision/spatial queries). Remaining:
- **Exact Boolean CSG:** Robust triangle clipping + stitched remeshing for partial-overlap union/intersection/difference. The baseline (disjoint/full-containment) is done.
- **Ultra-fast GPU K-means clustering:** CUDA-accelerated k-means for point clouds and feature-space segmentation workflows.
- **Mesh and point cloud denoising:** Edge-aware/spectral denoising operators for scanned and reconstructed data.
- **Mesh parameterization:** Robust UV/atlas generation and distortion-controlled parameterization methods.
- **Spectral mesh processing:** Laplacian/eigendecomposition-driven filters, embeddings, and editing operators.
- **Shape and point cloud registration (ICP variants):** Point-to-point, point-to-plane, and robust weighted ICP pipelines.
- **Additional state-of-the-art geometry processing methods:** Continue integrating current research-grade operators as first-class runtime tools.

**Top next (dependency-ordered):** Ordered from foundational geometry robustness and correspondence to advanced deformation/reconstruction so later operators can reuse earlier data products and solvers. Heat-method geodesic distance is already implemented; vector heat geodesics (parallel transport) remains.
- **Vector heat geodesics:** Parallel transport extension of the existing heat-method solver for direction/frame field applications.
- **Robust global registration (TEASER++/FGR/Super4PCS):** Outlier-tolerant coarse alignment before local ICP refinement.
- **Non-rigid registration (CPD/embedded deformation):** Deformable alignment for temporal scans and articulated shapes.
- **Field-aligned quad remeshing (MIQ/Instant Meshes class):** Direction-field-driven quad dominant remeshing for downstream UV/edit workflows.
- **Direction/frame field design (N-RoSy/cross fields):** Foundational orientation fields powering remeshing and anisotropic operators.
- **ARAP/projective-dynamics deformation:** Interactive constrained editing with stable, physically plausible deformations.
- **Fast winding number + signed distance pipelines:** Robust inside/outside queries for booleans, repair, and collision preprocessing.
- **TSDF volumetric fusion + meshing:** Multi-view scan fusion and high-quality surface extraction for reconstruction workflows.

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
| **Point** | FlatDisc, Surfel, EWA, Sphere, PotreeAdaptive, GaussianSplat |

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
