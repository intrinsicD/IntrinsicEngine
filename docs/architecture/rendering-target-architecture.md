# IntrinsicEngine — Graphics Rendering Target Architecture

> **Status:** Historical target-design document for the promoted canonical `src/graphics` layout. Current canonical rendering policy lives in `rendering-three-pass.md` and `graphics.md`.
> Resolved decisions are marked ✅. Deferred items are marked **[DEFERRED]**. Remaining open questions are in §13.
> See `docs/migration/archive/source-promotion-status.md` for the completed source-promotion status that originally gated work against this design.

---

## 0. Philosophy

- **BufferManager-managed geometry.** No per-entity vertex/index buffers. Geometry lives in managed buffers owned by `BufferManager`. Each entity holds a `BufferView` referencing a region within a managed buffer. Multiple managed buffers may exist (for capacity or format reasons); entities can reference different buffers from the manager.
- **GPU drives execution.** A GPU-side LBVH determines visibility. The CPU hands the GPU a scene description; the GPU builds indirect draw commands.
- **Positions and UVs in the vertex buffer only.** Everything else (normals, tangents, per-vertex scalars, colors) lives in per-entity attribute SSBOs accessed by BDA, or in textures (material data). This keeps managed vertex buffers compact and stable.
- **Deferred by default.** All opaque surfaces write to a G-buffer. Lighting is a single full-screen pass. Lines, points, and transparent objects are a forward sub-pass composited on top.
- **Components are switches.** Presence of `SurfaceComponent`, `LineComponent`, or `PointComponent` determines which pipelines render an entity — no boolean flags, no enum routing.
- **Push constants + SSBOs.** Per-entity rendering parameters go in two per-entity SSBOs — `GpuInstanceData` (transform, identity — updated frequently) and `GpuEntityConfig` (visualization, BDA pointers — updated rarely). Push constants carry only the per-draw-call slot index. No per-entity descriptor sets.
- **Visualization is a shader-level concern.** Scalar fields, color maps, isolines, and face/edge/vertex domain colors are resolved entirely in fragment shaders by reading from BDA-addressed attribute SSBOs. No additional passes required.

---

## 1. Layer Hierarchy

```
Runtime::Engine
 │
 ├── GraphicsBackend              (Vulkan stack only — no scene knowledge)
 │    └── VulkanDevice, SwapChain, TransferManager, BufferManager, BindlessSystem
 │
 ├── SceneManager                 (CPU world — authoritative entity state)
 │    └── entt::registry          (all ECS components, all CPU-side data)
 │
 └── Renderer                     (GPU world — frame production)
      │
      ├── GpuWorld                 (§2,3,5,8 — ALL GPU-resident scene data, see below)
      │
      ├── PickingSystem            (§6 — per-domain picking buffers + readback)
      ├── RenderGraph              (§11 — transient DAG, barrier calc, aliasing)
      └── DefaultPipeline          (§11 — ordered pass list as IRenderFeature)
           ├── LightingPassRegistry (§4.5 — ILightingContributor ordered list)
           ├── PickingPass
           ├── DepthPrepass        (optional)
           ├── GBufferPass
           ├── DeferredLightingPass
           ├── ForwardPass         (LinePass + PointPass + OverlaySurfacePass)
           ├── PostProcessPass
           ├── SelectionOutlinePass
           ├── DebugViewPass
           └── ImGuiPass
```

### GpuWorld — the GPU's complete view of the scene

`GpuWorld` is the GPU-side counterpart of `SceneManager`. Just as `SceneManager` owns all CPU entity state, `GpuWorld` owns all GPU-resident scene data. It is a single ownership boundary with five sub-allocators, each with its own strategy:

```
GpuWorld
 │
 ├── GlobalGeometryStore    (§2 — managed vertex + index buffers)
 │    └── BufferManager      byte-range allocator; may manage multiple buffers
 │
 ├── InstanceBuffer         (§3,5 — GpuInstanceData[] SSBO, updated frequently)
 │    └── SlotAllocator      integer slot allocator, max 100k entries
 │
 ├── EntityConfigBuffer     (§5 — GpuEntityConfig[] SSBO, updated rarely)
 │    └── SlotAllocator      parallel to InstanceBuffer (same slot index)
 │
 ├── AABBBuffer             (§3 — GpuAABB[] SSBO, parallel to InstanceBuffer)
 │
 ├── MaterialRegistry       (§8 — GpuMaterialData[] SSBO)
 │    └── SlotAllocator      integer slot allocator, independent of instance slots
 │
 └── BVHBuilder             (§3 — Static LBVH + Dynamic LBVH compute pipelines)
      ├── StaticBVHNodeBuffer  (persistent, rebuilt only when static entities change)
      └── DynamicBVHNodeBuffer (rebuilt every frame)
```

**Why these five sub-allocators and not one?**
- `GlobalGeometryStore`: byte-range allocator via `BufferManager` — geometry is variable-size, shared across multiple views of the same mesh. Multiple managed buffers may exist; entities reference specific buffers via `BufferView`.
- `InstanceBuffer` / `EntityConfigBuffer`: integer slot allocator — one slot per live entity, fixed-size records. Split by update frequency: transforms change every frame, visualization config changes rarely.
- `MaterialRegistry`: integer slot allocator — one slot per material, independent lifetime from entities.

They share the same ownership story (GPU-resident, scene-lifetime) but differ in allocation semantics. They live under one roof (`GpuWorld`) but are not merged into one buffer.

> **Implementation note (first production implementation):**
> The runtime uses a BDA root table (`GpuSceneTable`) instead of binding each scene SSBO separately in descriptor sets. Buffers remain independent storage buffers; only their GPU addresses are gathered into a single scene-table record.

**Key contract:** `Renderer` has no knowledge of ECS internals. It receives an immutable `RenderWorld` snapshot extracted from `SceneManager` each frame. ECS mutation never occurs after extraction.

---

## 2. Global Geometry Store

### 2.1 Buffers

All geometry buffers are owned and managed by `BufferManager`. `GpuWorld::GlobalGeometryStore` allocates regions within these managed buffers. Multiple managed buffers may exist (e.g., when a single buffer reaches its capacity limit, a new one is allocated). Entities reference their data via `BufferView`, which identifies both the managed buffer and the region within it.

| Buffer type | Content | Layout |
|-------------|---------|--------|
| **Managed Vertex Buffer(s)** | `vec3 position`, `vec2 uv` per vertex | interleaved: `[x,y,z, u,v]` → 20 bytes/vertex |
| **Managed Index Buffer(s)** | `uint32_t` triangle indices (reused for edge pairs) | flat uint32 array |

**Invariant:** Normals, tangents, per-vertex scalars, per-vertex colors, and all other per-vertex attributes are **never** stored in vertex buffers. They come from:
- **Material textures** (albedo, normal map, metallic-roughness) — sampled by UV.
- **Per-entity attribute SSBOs** (normals, visualization scalars/colors) — addressed by BDA pointer stored in `GpuEntityConfig`, indexed by `vertex_id` / `face_id` / `edge_id`.

### 2.2 Entity Buffer View Component

```cpp
// ECS::Components::BufferView
struct BufferView {
    uint32_t BufferID;       // which managed buffer this view references
    uint32_t VertexOffset;   // first vertex index within the managed vertex buffer
    uint32_t VertexCount;
    uint32_t IndexOffset;    // first index within the managed index buffer
    uint32_t IndexCount;
    uint32_t GeometryID;     // stable ID for GPU-scene slot lookup
};
```

An entity's `BufferView` can reference any managed buffer from `BufferManager`. Different entities may reference different managed buffers. Shared views (e.g., wireframe reusing a mesh's vertices) reference the same `BufferID` + `VertexOffset` range.

Lifecycle: created/updated by `MeshRendererLifecycle` (and graph/point-cloud equivalents) whenever geometry is uploaded or modified by an operator.

### 2.3 Buffer Management ✅

**Strategy:** `BufferManager` with free-list allocation and deferred compaction.

- `BufferManager` allocates large device-local buffers on demand (e.g. 256 MB per vertex buffer, 512 MB per index buffer). When a buffer is full, a new one is allocated.
- Each managed buffer has a free-list that tracks available ranges (offset + size). Allocations are first-fit.
- **Deletion:** Freed ranges are not immediately reused. They are held for `MAX_FRAMES_IN_FLIGHT` frames (deferred deletion), then returned to the free list.
- **Compaction:** Triggered per-buffer when free-list fragmentation exceeds a threshold (e.g. total free space > 30% of buffer capacity but largest contiguous free range < requested allocation). A single `vkCmdCopyBuffer` pass packs live regions to the front; all `BufferView` components referencing that buffer are updated in the same frame via a maintenance-lane scan.
- **Dynamic geometry (operator result):** If the new vertex count is ≤ old vertex count, overwrite in-place (same range, same buffer). If larger, free old range and reallocate (potentially in a different managed buffer if the current one lacks space).

### 2.4 Vertex Normal SSBO

Vertex normals for smooth-shaded meshes are **not** in managed vertex buffers. They live in a per-entity device-local SSBO. The `GpuEntityConfig.VertexNormalPtr` field holds the BDA of this SSBO (or 0 if absent). The vertex shader fetches `normal = VertexNormalPtr[vertex_id]` and passes it as a varying for interpolation.

This allows geometry-processing operators to recompute normals and upload only the normal SSBO — no vertex buffer rewrite needed.

---

## 3. GPU-Driven Scene

### 3.1 Per-Instance SSBOs

Two persistent SSBOs per entity slot (default max: 100 000 slots), indexed by the same slot ID. See §5.1 for the full struct definitions.

- **`GpuInstanceData[]`** (set=2, binding=0, 96 bytes/entity) — Model transform, EntityID, MaterialSlot, RenderFlags, vertex/index counts and offsets. Updated frequently (every frame for moving entities).
- **`GpuEntityConfig[]`** (set=2, binding=1, 96 bytes/entity) — BDA attribute pointers (normals, scalars, colors, point sizes), visualization config (colormap, scalar range, isoline params), point rendering config. Updated rarely (on vis config change).

The cull shader reads `GpuInstanceData` (transform + counts) to generate indirect draw commands. Fragment shaders read both SSBOs via the shared `SlotIndex` push constant.

### 3.2 AABB SSBO

Parallel array to the instance SSBO. Updated whenever a transform or mesh changes.

```glsl
struct GpuAABB {
    vec3  Min; float _pad0;
    vec3  Max; float _pad1;
};
```

### 3.3 GPU LBVH (Karras 2012) ✅

**Static / Dynamic split** — two separate BVHs, merged at cull time:

#### Dynamic BVH
Built **every frame** from all entities **without** `ECS::StaticTag`. Covers animated transforms, geometry under active operator editing, and any entity the user has not explicitly tagged static.

Compute stages (run each frame before GBufferPass):
1. `morton_encode.comp` — 30-bit 3D Morton code per dynamic AABB centroid.
2. GPU radix sort by Morton code.
3. `lbvh_build.comp` — Karras 2012 internal-node construction (`findMSB(key_i ^ key_j)` for LCP).
4. `lbvh_aabb_propagate.comp` — bottom-up AABB propagation, atomic parent-visit counters.

#### Static BVH
Built **once** from all entities carrying `ECS::StaticTag`. Persists across frames.

**Dirty trigger:** The static BVH is marked dirty when:
- An entity gains `ECS::StaticTag` (entering the static pool).
- An entity loses `ECS::StaticTag` (leaving the static pool → moves to dynamic).
- A static entity's transform or geometry changes (which should be rare by convention).

When dirty, the static BVH is rebuilt once during the compute prologue and remains valid until the next dirty event.

#### `ECS::StaticTag` contract
- Zero-size ECS component. Presence = entity is static.
- Default: absent (all new entities are dynamic).
- Adding/removing `StaticTag` marks the static BVH dirty; `GPUSceneSync` detects this.
- Entities with `StaticTag` may still have transforms set at spawn time; they are considered immovable after that.

#### Culling
`instance_cull.comp` traverses **both** BVHs against the camera frustum and writes `VkDrawIndexedIndirectCommand[]` into the Surface / Line / Point indirect buffers. Draw count goes to `DrawCountBuffer`.

**Outputs:** `DynamicBVHNodeBuffer` + `StaticBVHNodeBuffer` (flat internal+leaf node arrays).

**Uses:**
- **Frustum culling** → indirect draw commands as above.
- **Ray casting** — CPU-side BVH query for picking refinement; later, GPU ray casting for AO / shadows / hit queries.

### 3.4 Indirect Draw Buffers

One indirect draw buffer per primitive type (Surface / Line / Point). The cull shader writes one command per visible entity into the appropriate buffer based on `RenderFlags`. All three buffers are consumed via `vkCmdDrawIndexedIndirectCount` (Surface, Line) or `vkCmdDrawIndirectCount` (Point).

#### Surface (triangles, `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`)

```
VkDrawIndexedIndirectCommand {
    indexCount    ← GpuInstanceData.IndexCount        // triangle indices
    instanceCount ← 1
    firstIndex    ← GpuInstanceData.IndexOffset
    vertexOffset  ← GpuInstanceData.VertexOffset      (cast to int32_t)
    firstInstance ← entity slot index                  (→ gl_InstanceIndex in shader)
}
```

#### Line (edges, `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`)

Edge pair indices are stored in managed index buffers alongside triangle indices. Each edge is 2 indices.

```
VkDrawIndexedIndirectCommand {
    indexCount    ← GpuInstanceData.IndexCount        // edge pairs × 2
    instanceCount ← 1
    firstIndex    ← GpuInstanceData.IndexOffset       // edge index region
    vertexOffset  ← GpuInstanceData.VertexOffset      // shared vertex buffer
    firstInstance ← entity slot index                  (→ gl_InstanceIndex in shader)
}
```

Line entities share the same vertex buffer region as their parent mesh/graph via `BufferView`. The `IndexOffset`/`IndexCount` fields on the `Line::Component`'s instance slot point to the edge index region, not the triangle index region.

#### Point (billboards, `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`, synthesized quads)

Points have no index buffer. The vertex shader synthesizes billboard quads from point positions read via BDA.

```
VkDrawIndirectCommand {
    vertexCount   ← GpuInstanceData.VertexCount * 6   // 6 verts per point (2 triangles)
    instanceCount ← 1
    firstVertex   ← 0                                 // shader reads positions from BDA
    firstInstance ← entity slot index                  (→ gl_InstanceIndex in shader)
}
```

The point vertex shader computes `pointIndex = gl_VertexIndex / 6` and `cornerIndex = gl_VertexIndex % 6`, then reads `GpuEntityConfig.PtrPositions[pointIndex]` to get the point center and expands it into a billboard quad corner.

---

## 4. Deferred Rendering Pipeline

> **Note:** The engine already has a working deferred/hybrid pipeline (`FrameLightingPath::Deferred`, `::Hybrid`), G-buffer MRT outputs, a composition pass, and `deferred_lighting.frag`. This section specifies **refinements** to the existing implementation: a revised G-buffer layout, the modular `LightingPassRegistry` (§4.5), and the unified ForwardPass entity-ID write-back for selection outline coverage across all primitive types.

### 4.1 G-Buffer Layout

| Attachment | Format | Contents |
|------------|--------|----------|
| `GBuf_Depth`    | `D32_SFLOAT`          | Hardware depth (picking, position reconstruction) |
| `GBuf_EntityId` | `R32_UINT`            | Entity handle per pixel (selection outline, always active) |
| `GBuf_Albedo`   | `R8G8B8A8_UNORM`      | Base color RGB + AO (A) |
| `GBuf_Normal`   | `R16G16B16A16_SFLOAT` | World-space normal XYZ + roughness (W) |
| `GBuf_Material` | `R8G8B8A8_UNORM`      | Metallic (R) + roughness (G) + flags (B) + emissive mask (A) |

`GBuf_EntityId` is always written by `GBufferPass` and read by `SelectionOutlinePass`. This replaces the separate picking-buffer entity channel and resolves §13.13.

### 4.2 Normal Sourcing in Fragment Shader ✅

Two-stage composition, evaluated per-fragment in `gbuffer.frag`:

```
Stage 1 — Base normal (best available geometric normal):

  if GpuEntityConfig.VertexNormalPtr != 0:
      N_base = normalize(interpolated vertex normal from SSBO)
  else:
      N_base = normalize(cross(dFdx(worldPos), dFdy(worldPos)))   ← flat/geometric


Stage 2 — Normal map perturbation (composites on top of base normal):

  if MaterialData.NormalID != 0  AND  UVs are valid (not all-zero):
      T       = derivative_tangent(worldPos, uv)   ← see §4.3
      B       = normalize(cross(N_base, T))
      T       = cross(B, N_base)                   ← re-orthogonalize
      TBN     = mat3(T, B, N_base)
      N_final = normalize(TBN * (sample(NormalMap).xyz * 2.0 - 1.0))
  else:
      N_final = N_base


Stage 3 — Unlit override:

  if RenderFlags & UNLIT:
      skip lighting entirely → write albedo directly to SceneColorHDR
```

**Key insight:** Vertex normals and normal maps are **not alternatives** — they **compose**. Vertex normals define the smooth interpolated surface shape (large-scale curvature). The normal map adds high-frequency detail (bumps, pores, surface texture) in tangent space relative to the vertex normal. A mesh with BOTH gets the intended result: smooth curvature from vertex normals + detail from the normal map.

**Degenerate cases:**
- Vertex normals present, no normal map → smooth shading with interpolated normals.
- Normal map present, no vertex normals → TBN built from geometric (flat) normal + derivative tangent. This produces per-triangle detail without smooth interpolation — acceptable for flat surfaces with texture detail.
- Neither → flat shading via geometric normal. Good for visualization, CAD, debugging.
- Normal map present, UVs all-zero → TBN is degenerate. Fall through to `N_base` only (vertex or geometric). No normal map applied.

### 4.3 Tangent Computation ✅ (derivative-based, no vertex attribute)

Tangents are never stored as vertex attributes. They are computed analytically in the fragment shader from screen-space derivatives of world position and UV:

```glsl
vec3 derivative_tangent(vec3 worldPos, vec2 uv) {
    vec3 dPos_dx = dFdx(worldPos),  dPos_dy = dFdy(worldPos);
    vec2 dUV_dx  = dFdx(uv),        dUV_dy  = dFdy(uv);
    float r = 1.0 / (dUV_dx.x * dUV_dy.y - dUV_dy.x * dUV_dx.y);
    return normalize((dPos_dx * dUV_dy.y - dPos_dy * dUV_dx.y) * r);
}
// bitangent = cross(N, T); TBN = mat3(T, B, N)
```

This requires UVs to be present in the managed vertex buffer. When UVs are all-zero (mesh without parameterization), TBN is degenerate → Stage 2 in §4.2 is skipped, and `N_base` (vertex normal or geometric) is used directly.

**Face normals (flat shading):** When `RenderFlags & FLAT_SHADING` is set, or when no normal source is available, the fragment shader uses `normalize(cross(dFdx(worldPos), dFdy(worldPos)))`. This gives a per-triangle geometric normal with correct deferred lighting, no per-vertex normal data required.

### 4.4 Pass Sequence

```
[Compute prologue]
  scene_update.comp → morton_encode + sort + lbvh_build + lbvh_propagate
  instance_cull.comp → Surface/Line/Point indirect buffers + draw counts

DepthPrepass  [optional, feature-gated]
  → writes GBuf_Depth only
  → GPU-driven, same indirect buffer as GBufferPass

GBufferPass
  → writes GBuf_Depth(CLEAR), GBuf_EntityId, GBuf_Albedo, GBuf_Normal, GBuf_Material
  → GPU-driven: vkCmdDrawIndexedIndirectCount (surface indirect buffer)
  → shaders: gbuffer.vert + gbuffer.frag
  → reads: managed buffers (BDA), GpuInstanceData SSBO, GpuEntityConfig SSBO, GpuMaterialData SSBO, bindless textures

DeferredLightingPass
  → reads GBuf_* + ShadowAtlas
  → writes SceneColorHDR (R16G16B16A16_SFLOAT)
  → fullscreen triangle
  → lighting model: modular (§4.5)

ForwardPass  [writes SceneColorHDR(LOAD), GBuf_Depth(LOAD), GBuf_EntityId(LOAD)]
  ├── LinePass     (mesh wireframe, graph edges, vector field arrows, debug lines)
  │                writes GBuf_EntityId at covered pixels → selection outline for lines
  ├── PointPass    (mesh vertices, graph nodes, point clouds, debug points)
  │                writes GBuf_EntityId at covered pixels → selection outline for points
  └── OverlaySurfacePass (overlay meshes, debug triangles — alpha blend, no depth write)

[DEFERRED] TransparentSurfacePass — §13.5
```

### 4.5 Modular Lighting ✅

`DeferredLightingPass` owns a **`LightingPassRegistry`** — an ordered list of `ILightingContributor` implementations. Each contributor is an `IRenderFeature`:
- Registered at startup in the order below.
- Evaluated in registration order each frame (additive writes to `SceneColorHDR`).
- Individually toggleable at runtime via `FeatureRegistry` (disable = skip contributor, no pipeline rebuild).
- Each contributor reads the G-buffer SSBOs and its own `LightBuffer` data; they share no mutable state.

**Implementation order (each phase is independent and ships when ready):**

| Phase | Contributor | Data source | Notes |
|-------|-------------|-------------|-------|
| 1 | `DirectionalLightContributor` | `CameraBufferObject` (dir, color, intensity) | PBR BRDF, CSM shadow atlas. **Baseline — always present.** |
| 2 | `AmbientContributor` | `CameraBufferObject` (ambient color + intensity) | Flat ambient term. **Baseline — always present.** |
| 3 | `PointLightContributor` | `LightBuffer` SSBO (position, color, radius, falloff) | Requires `LightBuffer` SSBO populated from ECS `PointLight` components. |
| 4 | `SpotLightContributor` | `LightBuffer` SSBO (position, direction, cone angles) | Shares `LightBuffer` layout with point lights (type discriminator field). |
| 5 | `IBLContributor` | Bindless environment cubemap + irradiance map + BRDF LUT | Split-sum approximation (Karis 2013). Precomputed offline or at load time. |
| 6 | `AreaLightContributor` | `LightBuffer` SSBO (LTC matrices, polygon vertices) | Linearly Transformed Cosines (Heitz et al. 2016). |

**`LightBuffer` SSBO** (populated from ECS during `ExtractRenderWorld()`):
```glsl
struct GpuLightEntry {
    vec3  Position;
    uint  Type;          // 0=point, 1=spot, 2=area
    vec3  Direction;     // spot/area only
    float Range;
    vec3  Color;
    float Intensity;
    vec4  SpotAngles;    // inner/outer cone cos angles
    // area light polygon verts / LTC params packed in remaining fields
};
```

Clustered light culling (tiled frustum → light list per tile) is deferred until `PointLightContributor` demonstrates a per-fragment O(lights) cost problem in practice.

---

## 5. Render Components

Presence = enabled. Absence = disabled. No boolean flags. No intermediate `::Data` components — render components absorb all GPU lifecycle and appearance state.

| Component | Enables | Required |
|-----------|---------|----------|
| `ECS::Surface::Component` | GBufferPass draws | `BufferView` + triangle indices |
| `ECS::Line::Component`    | LinePass draws (forward) | `BufferView` + edge index buffer |
| `ECS::Point::Component`   | PointPass draws (forward) | `BufferView` + point mode |

These are the **only** per-entity rendering components. There are no `Mesh::Data`, `Graph::Data`, or `PointCloud::Data` components — those are eliminated (see §15.0, Issue A). Entity type is identified by `DataAuthority::*Tag` zero-size components. CPU geometry data lives in `GeometrySources` components. Visualization selection lives in `ECS::Visualization::Config` (see §15.5).

**What render components hold (after absorbing ::Data responsibilities):**

- **`Surface::Component`** — `BufferView`, material asset handle, GPU instance slot, visibility toggle, cached vertex/face colors (packed ABGR), face color dirty flag, per-vertex color mode (smooth interpolation / nearest-vertex Voronoi / centroid-based Voronoi), centroid labels + positions for Voronoi mode.
- **`Line::Component`** — `BufferView` (shared vertex buffer via BDA), edge index buffer handle, edge count, uniform color/width, overlay flag (no depth test), per-edge color cache (packed ABGR), per-edge color toggle, `SourceDomain` hint (MeshEdge / GraphEdge).
- **`Point::Component`** — `BufferView` (shared vertex buffer via BDA), uniform color/size/size-multiplier, render mode (FlatDisc / Surfel / ImpostorSphere / GaussianSplat), per-point color/radii/normals flags, per-point color + radii caches, `SourceDomain` hint (MeshVertex / GraphNode / CloudPoint).

Lifecycle systems emplace render components with appropriate defaults when an entity gains a `DataAuthority::*Tag` and `GeometrySources` data. Removal of the render component disables that rendering path — no data loss, since all persistent data remains in `GeometrySources`.

### 5.1 Per-Entity GPU Data — Split by Update Frequency ✅

Per-entity GPU data is split across two persistent SSBOs with different update cadences. Both use the same slot index (parallel arrays), indexed by `gl_InstanceIndex` (= `firstInstance` from the indirect draw command).

#### GpuInstanceData SSBO (set=2, binding=0) — updated frequently

Transform and identity data. Updated every frame for moving entities, at allocation time for static ones.

```glsl
// std430 — 96 bytes per entity
struct GpuInstanceData {
    mat4   Model;            // 64 bytes — world transform
    uint   EntityID;         // entt entity handle (for picking / selection outline)
    uint   MaterialSlot;     // index into GpuMaterialData[]
    uint   RenderFlags;      // bitfield: Surface|Line|Point + shading mode + flat + unlit
    uint   VertexCount;
    uint   VertexOffset;     // first vertex in managed buffer
    uint   IndexOffset;      // first index in managed buffer
    uint   IndexCount;
    uint   _pad0;
};
// sizeof(GpuInstanceData) = 96 bytes
```

#### GpuEntityConfig SSBO (set=2, binding=1) — updated rarely

Visualization parameters and BDA attribute pointers. Updated only when the user changes visualization settings, an algorithm writes new properties, or attribute SSBOs are re-uploaded.

```glsl
// std430 — 96 bytes per entity
struct GpuEntityConfig {
    // ---- Per-vertex attribute BDA pointers (0 = absent) ----
    uint64_t VertexNormalPtr;   // vec3[] normals for smooth shading
    uint64_t ScalarPropPtr;     // float[] per-vertex, per-face, or per-edge scalar values
    uint64_t ColorPropPtr;      // vec3[] or vec4[] per-vertex/face/edge colors
    uint64_t PointSizePtr;      // float[] per-point sizes (PointPass only, 0 = uniform)

    // ---- Visualization config ----
    float  ScalarRangeMin;
    float  ScalarRangeMax;
    uint   ColormapID;          // bindless 1D LUT texture index
    uint   BinCount;            // 0 = continuous, >0 = quantize into k bins

    float  IsolineCount;        // 0 = disabled
    float  IsolineWidth;        // scalar-space width (fraction of one iso-interval)
    float  VisualizationAlpha;  // uniform alpha for color overlay (1.0 = opaque)
    uint   VisDomain;           // 0=vertex, 1=face, 2=edge

    vec4   IsolineColor;        // RGBA

    // ---- PointPass config ----
    float  PointSize;
    uint   PointMode;           // FlatDisc=0, Surfel=1, ImpostorSphere=2, GaussianSplat=3
    uint   _pad0;
    uint   _pad1;
};
// sizeof(GpuEntityConfig) = 96 bytes
```

**Why split?** Transform changes (camera orbit, animation) touch `GpuInstanceData` every frame for affected entities. Visualization config changes (user adjusts scalar range, switches colormap) are rare — typically once per user interaction. Separating them avoids uploading 96 bytes of unchanged visualization data on every transform update, and avoids dirtying the config SSBO on every frame. The cull shader reads only `GpuInstanceData` (transform + counts); fragment shaders read both.

**Push constants** carry only the entity's slot index:

```glsl
layout(push_constant) uniform PC {
    uint SlotIndex;   // → GpuInstanceData[SlotIndex] and GpuEntityConfig[SlotIndex]
};
```

This keeps push constants minimal (4 bytes). Shaders access all per-entity data via the two SSBOs.

---

## 6. Selection System

### 6.1 Picking Pass

Fires only when a pick is pending. Renders into three picking buffers:

| Buffer | Format | Content |
|--------|--------|---------|
| `Pick_Depth`       | `D32_SFLOAT` | For world-position reconstruction |
| `Pick_EntityId`    | `R32_UINT`   | Entity handle, encoded (same as GBuf_EntityId) |
| `Pick_PrimitiveId` | `R32_UINT`   | Domain-specific primitive ID |

### 6.2 Per-Domain Picking

**Meshes** (`Surface::Component`):
- `Pick_PrimitiveId` ← `gl_PrimitiveID` (face index) from raster.
- CPU refinement: reconstruct world pos from `Pick_Depth` → object space → KDTree for closest `vertex_id`; edge adjacency for closest `edge_id`.

**Graphs** (`Line::Component`, domain = GraphEdge):
- `Pick_PrimitiveId` ← `edge_id`. Each edge expanded to screen-space capsule of radius `EdgePickRadius` (UI-configurable).
- CPU refinement: closest `vertex_id` = nearer endpoint of `edge_id`.

**Point Clouds** (`Point::Component`, domain = CloudPoint):
- `Pick_PrimitiveId` ← `vertex_id`. Each point expanded to radius `PointPickRadius` (UI-configurable, matches visual `PointSize`).

### 6.3 Picker Result

```cpp
struct PickResult {
    bool         IsBackground = true;
    entt::entity Entity       = entt::null;
    uint32_t     FaceId       = UINT32_MAX;   // meshes only
    uint32_t     EdgeId       = UINT32_MAX;   // meshes + graphs
    uint32_t     VertexId     = UINT32_MAX;   // meshes + graphs + point clouds
    glm::vec3    WorldPosition{};
    glm::vec3    LocalPosition{};             // in entity's object space
};
```

`PickingSystem::CurrentResult` is updated in the maintenance lane by `PickingSystem::ProcessReadback()`. Consumed by `SelectionModule`.

### 6.4 Selection Outline

`SelectionOutlinePass` reads `GBuf_EntityId` for Sobel-edge outline generation. The `GBuf_EntityId` attachment is written by **all rendering passes**, not just `GBufferPass`:

1. **GBufferPass** writes `EntityID` for opaque surfaces (initial clear + write).
2. **ForwardPass** loads `GBuf_EntityId` (no clear) and **LinePass** / **PointPass** write `EntityID` at their covered pixels, overwriting the surface entity ID where lines/points are visible.

This ensures selection outlines work for all entity types — surfaces, wireframes, graph edges, point clouds — via a single unified `GBuf_EntityId` buffer. The separate `Pick_EntityId` buffer is not needed for outline rendering — it is only needed for click-position readback. This resolves §13.13.

**Line/Point fragment shaders** must output `EntityID` as an MRT attachment (same format as `GBuf_EntityId`: `R32_UINT`). The `EntityID` is read from `GpuInstanceData[SlotIndex].EntityID` in the vertex shader and passed as a flat varying to the fragment shader.

---

## 7. Point Cloud Rendering

Point clouds render in the **ForwardPass** (PointPass sub-pass), after deferred composition. They do not write to the G-buffer.

| Mode | Description | Shader | Status |
|------|-------------|--------|--------|
| `FlatDisc`      | Camera-facing billboard, circular clip | `point_flatdisc.vert/frag` | ✅ |
| `Surfel`        | Normal-aligned oriented quad | `point_surfel.vert/frag` | ✅ |
| `ImpostorSphere`| Billboard with per-fragment sphere depth correction | `point_sphere.vert/frag` | ✅ |
| `GaussianSplat` | Oriented anisotropic splat, view-dependent color | `point_splat.vert/frag` | **[DEFERRED — §13.8]** |

Point size is per-entity (`GpuEntityConfig.PointSize`) or per-point (`GpuEntityConfig.PointSizePtr[point_id]`). UI sliders write to `Point::Component` which propagates to `GpuEntityConfig` on the next dirty sync.

---

## 8. Material System

### 8.1 Material SSBO (set=3, binding=0)

```glsl
// std430 — 64 bytes
struct GpuMaterialData {
    vec4   BaseColorFactor;       // RGBA tint multiplied with albedo texture
    float  MetallicFactor;
    float  RoughnessFactor;
    // float  DisplacementScale;  // [DEFERRED — §13.6] — uncomment when tessellation ships
    uint   _reserved0;            // placeholder for DisplacementScale
    uint   Flags;                 // alpha mode, double-sided, emissive, unlit
    uint   AlbedoID;              // bindless texture index (0 = white)
    uint   NormalID;              // bindless texture index (0 = use vertex normal or derivative)
    uint   MetallicRoughnessID;   // bindless texture index (0 = use factors)
    // uint   DisplacementID;     // [DEFERRED — §13.6] — uncomment when tessellation ships
    uint   _reserved1;            // placeholder for DisplacementID
    uint   EmissiveID;            // bindless texture index (0 = none)
    uint   _pad[2];
};
```

### 8.2 Normal Map Absence — Albedo-Only Fallback ✅

When `NormalID == 0` **and** `GpuEntityConfig.VertexNormalPtr == 0` **and** `Flags & UNLIT`:
- The GBuffer fragment shader writes albedo to `GBuf_Albedo` but writes a sentinel (e.g. `vec3(0)`) to `GBuf_Normal`.
- `DeferredLightingPass` reads the `GBuf_Normal` length: if near-zero, it skips BRDF evaluation and outputs albedo directly — **albedo-only, no lighting applied**.

When `NormalID == 0` **but** vertex normals or derivatives are available, lighting proceeds normally (§4.2 Stage 1 provides the base normal; Stage 2 is skipped since `NormalID == 0`).

### 8.3 Descriptor Sets (GBuffer Pass)

| Set | Binding | Content | Update frequency |
|-----|---------|---------|-----------------|
| 0 | 0 | `CameraBufferObject` UBO | per-frame |
| 1 | 0 | Bindless texture array | at load time |
| 2 | 0 | `GpuInstanceData[]` SSBO | per-frame (transforms) |
| 2 | 1 | `GpuEntityConfig[]` SSBO | on vis config change |
| 3 | 0 | `GpuMaterialData[]` SSBO | on material change |

---

## 9. Visualization

Visualization changes how **color** is derived in fragment shaders. It does **not** add new passes. All attribute data is accessed via BDA pointers in `GpuEntityConfig`.

### 9.1 Attribute SSBO Pattern ✅

All per-primitive visualization data (scalars, colors) is uploaded to per-entity device-local SSBOs whose BDA pointers are stored in `GpuEntityConfig`. The shader indexes by:
- `vertex_id` — fetched in the **vertex shader**, passed as a varying → **smooth interpolation** across faces.
- `face_id` (`gl_PrimitiveID`) — fetched in the **fragment shader** → **flat per-face** value.
- `edge_id` — fetched in the **vertex shader** (line primitives, same value for both endpoints) → **uniform per-edge** value.

This is **Option B** for all geometry types (meshes, graphs, point clouds). No UV mapping is required for visualization. Material textures still use UV (§4.3). Resolves §13.9.

### 9.2 Scalar Field Visualization

**Supported types:** `bool`, `int32_t`, `uint32_t`, `float`, `double` — all converted to `float32` at upload.

**Domain flag** (`GpuEntityConfig.VisDomain`):
- `VERTEX (0)`: vertex shader fetches `ScalarPropPtr[vertex_id]` → interpolated in fragment shader.
- `FACE (1)`: fragment shader fetches `ScalarPropPtr[gl_PrimitiveID]` → flat per-face.
- `EDGE (2)`: vertex shader fetches `ScalarPropPtr[edge_id]` → uniform per edge (LinePass only).

**Fragment shader pipeline:**
```glsl
float s = fetch_scalar(domain, vertex_id_or_prim_id);
float t = clamp((s - ScalarRangeMin) / (ScalarRangeMax - ScalarRangeMin), 0.0, 1.0);
if (BinCount > 0) t = floor(t * float(BinCount)) / float(BinCount);
vec3 color = texture(colormapLUT[ColormapID], t).rgb;
```

**Colormaps:** Pre-uploaded 256×1 `R8G8B8A8_UNORM` 1D textures for Viridis, Jet, Coolwarm, Plasma, Inferno, Turbo, Grayscale. Registered in the bindless array at startup.

#### 9.2.1 Isolines (Fragment Shader, Mesh-Only)

Smooth anti-aliased isolines without mesh topology dependence. The isoline width is specified in **scalar space** (fraction of one iso-interval), making it zoom-independent. `fwidth()` is used only for anti-aliasing — the smoothstep transition width — not for the line width itself.

```glsl
// In fragment shader, after scalar fetch
if (IsolineCount > 0.0) {
    float sv = s * IsolineCount;
    float fw = fwidth(sv);                       // screen-space rate of change (for AA only)
    float dist = abs(fract(sv + 0.5) - 0.5);     // distance to nearest isoline in scalar space
    float halfWidth = IsolineWidth * 0.5;         // IsolineWidth is scalar-space (e.g., 0.05)
    float aaEdge = fw;                            // AA transition = ~1 pixel in scalar space
    float line = 1.0 - smoothstep(halfWidth - aaEdge, halfWidth + aaEdge, dist);
    color = mix(color, IsolineColor.rgb, line * IsolineColor.a);
}
```

**Zoom behavior:** `IsolineWidth` is constant in scalar space, so isolines maintain their visual proportion relative to the scalar field gradient regardless of camera distance. At extreme zoom-out, when `fw` exceeds the isoline spacing, individual isolines naturally merge — this is correct behavior (the scalar field cannot be resolved at that scale). At extreme zoom-in, lines remain crisp with sub-pixel anti-aliasing.

Only meaningful for vertex-domain scalars on meshes (interpolated across triangles). Disabled for face/edge domain and for graphs/point clouds via `RenderFlags`.

### 9.3 Face-Based Color / Scalar (Meshes)

For face-domain rendering (`VisDomain = FACE`):
- `ScalarPropPtr[gl_PrimitiveID]` gives the face scalar value → colormap pipeline (§9.2).
- `ColorPropPtr[gl_PrimitiveID]` gives a `vec3`/`vec4` face color directly.
- Face normal: always computed from `cross(dFdx(worldPos), dFdy(worldPos))` — flat shading, per-fragment, no extra data. Correct deferred lighting is applied with this normal.

Use cases: cluster labels, face quality metrics, segmentation results, face-area visualization.

### 9.4 Edge-Based Color / Scalar (Meshes via LinePass)

Mesh edge visualization is rendered by the `LinePass` using the `Line::Component` (already present for wireframe). Per-edge scalar or color is stored in a per-entity edge attribute SSBO and addressed via `ScalarPropPtr` or `ColorPropPtr` with `VisDomain = EDGE`.

The vertex shader on line primitives maps each endpoint to the same `edge_id` → uniform color across the edge segment.

### 9.5 RGB / RGBA Color Visualization ✅

- `ColorPropPtr` holds a `vec3[]` or `vec4[]` buffer (flag in `RenderFlags` distinguishes).
- Vertex shader fetches by `vertex_id` (or fragment shader by `gl_PrimitiveID` for face domain).
- `VisualizationAlpha` (from `GpuEntityConfig`) multiplies the alpha of vec3 colors (default 1.0).
- For `vec4` properties, the per-element alpha is used directly.

### 9.6 Texture Mapping & Htex Fallback **[DEFERRED — §13.10]**

UVs in the managed vertex buffer are used by material texture sampling (albedo, normal map, etc.) in the G-buffer pass. For meshes with a UV parameterization, visualization data can optionally be baked into property textures instead of SSBOs. **This path is deferred.** The SSBO path (§9.1) is the primary visualization mechanism.

Htex (Halfedge Textures) for UV-free meshes: **[DEFERRED — §13.10]**. The existing `HtexPatchPreviewPass` remains as-is until the deferred pipeline integration is specified.

### 9.7 Vector Field Visualization

Vector fields are rendered as overlay line segments via `LinePass`. Each active `VectorFieldEntry` spawns a child `Graph` entity.

**Supported domains:**
| Domain | Base point | End point |
|--------|-----------|-----------|
| `Vertex` | `vertex_position[i]` | `vertex_position[i] + vector[i] * scale` |
| `Edge`   | `(v0 + v1) / 2` (edge midpoint, sampled on CPU) | midpoint + `vector[edge_i] * scale` |
| `Face`   | face centroid `(v0+v1+v2)/3` (sampled on CPU) | centroid + `vector[face_i] * scale` |

CPU-side baking happens in `PropertySetDirtySyncSystem` when `VectorFieldsDirty = true`. Baked vertex pairs are uploaded to the child Graph entity's managed buffer region.

**Per-field controls** (in `VectorFieldEntry`):
- `Normalize : bool` — normalize all vectors before scaling (UI toggle).
- `Scale : float` — uniform length multiplier.
- `LengthPropertyName : string` — per-arrow length from a scalar property (overrides uniform Scale per arrow).
- `ArrowColor : ColorSource` — scalar/vec3/vec4 property for per-arrow color; falls back to uniform `Color`.
- `Overlay : bool` — disable depth test (renders on top of everything).

The child Graph entity uses `Line::Component` and `Point::Component` (nodes suppressed if `VectorFieldMode = true`). No new render pass.

**Arrowheads: [DEFERRED — §13.11]**

---

## 10. Overlays

An **overlay** is a secondary entity (mesh, graph, or point cloud) parented to a primary entity via `Hierarchy::Component`.

- Created/destroyed independently of the parent via `OverlayEntityFactory`.
- Has its own `BufferView` → its own region in a managed buffer.
- Rendered by the same pass as its data type.
- Can set `Overlay = true` on `Line::Component` / `Point::Component` to disable depth testing.
- Destruction: ECS `on_destroy` frees the `BufferView` region via deferred deletion.

| Overlay Type | Example Uses |
|-------------|-------------|
| Graph overlay | Vector field arrows, algorithm result wireframe |
| Point cloud overlay | Selected vertex highlights, sampled debug points |
| Surface overlay | Boolean result mesh, debug triangle fill (alpha blend) |

**Transient debug content** (`DebugDraw::Line()`, `DebugDraw::Triangle()`, etc.) remains per-frame host-visible transient buffers — **not** modeled as overlay entities. This is the correct split: `DebugDraw` = one-shot immediate mode; overlay entities = persistent multi-frame visualizations. Resolves §13.12.

---

## 11. Pass Order

```
[Compute prologue — before any render pass]
  scene_update.comp         ← scatter instance SSBO updates

  [Static BVH — only when dirty (StaticTag added/removed or static entity changed)]
  morton_encode_static.comp + sort + lbvh_build.comp + lbvh_aabb_propagate.comp
  → StaticBVHNodeBuffer (persistent, survives across frames)

  [Dynamic BVH — every frame]
  morton_encode_dynamic.comp + sort + lbvh_build.comp + lbvh_aabb_propagate.comp
  → DynamicBVHNodeBuffer

  instance_cull.comp        ← traverses both BVHs → Surface/Line/Point indirect buffers + draw counts

Pass 01 — PickingPass          [conditional: only on pending pick]
           writes Pick_Depth, Pick_EntityId, Pick_PrimitiveId
           mesh: face_id | graph: edge_id (radius-expanded) | cloud: vertex_id (radius-expanded)

Pass 02 — DepthPrepass         [optional, feature-gated]
           writes GBuf_Depth only — GPU-driven via Surface indirect buffer

Pass 03 — GBufferPass          [opaque surfaces, Surface::Component]
           writes GBuf_Depth, GBuf_EntityId, GBuf_Albedo, GBuf_Normal, GBuf_Material
           GPU-driven: vkCmdDrawIndexedIndirectCount (surface indirect buffer)

Pass 04 — DeferredLightingPass [fullscreen triangle, LightingPassRegistry]
           reads GBuf_*, ShadowAtlas, LightBuffer SSBO
           writes SceneColorHDR
           contributors (in order): Directional → Ambient → [PointLight] → [SpotLight] → [IBL] → [AreaLight]
           [] = registered but feature-gated, not yet implemented

Pass 05 — ForwardPass          [SceneColorHDR LOAD, GBuf_Depth LOAD, GBuf_EntityId LOAD]
           ├── LinePass     (mesh edges, graph edges, vector field overlays, debug lines)
           │                GPU-driven: Line indirect buffer (vkCmdDrawIndexedIndirectCount)
           │                writes GBuf_EntityId at covered pixels
           ├── PointPass    (mesh vertices, graph nodes, point clouds, debug points)
           │                GPU-driven: Point indirect buffer (vkCmdDrawIndirectCount)
           │                writes GBuf_EntityId at covered pixels
           └── OverlaySurfacePass (alpha-blend, no depth write — debug triangles, overlay meshes)
           [DEFERRED] TransparentSurfacePass — §13.5

Pass 06 — PostProcessPass
           ├── Bloom (progressive downsample/upsample)
           ├── ToneMap → SceneColorLDR (ACES / Reinhard / Hable)
           └── FXAA / SMAA

Pass 07 — SelectionOutlinePass (reads GBuf_EntityId, fullscreen Sobel)
Pass 08 — DebugViewPass        [optional]
Pass 09 — ImGuiPass
Pass 10 — Present
```

---

## 12. Data Flow Summary

```
SceneManager (CPU, authoritative)
    │
    ▼  SIMULATE PHASE — ECS DAGScheduler
    │  PropertySetDirtySync
    │  → MeshRendererLifecycle → GraphLifecycle → PointCloudLifecycle
    │    (upload geometry to GpuWorld.GeoStore, update instance + config slots)
    │  → GPUSceneSync         (transform → GpuWorld.InstanceBuffer)
    │  → EntityConfigSync     (vis config, BDA ptrs → GpuWorld.EntityConfigBuffer)
    │  → StaticBVHDirtySync   (StaticTag changes → BVHBuilder.StaticDirty)
    │  → PrimitiveBVHBuild    (CPU BVH for picking)
    │  → VectorFieldSync      (bake child graph verts)
    │  → entt::dispatcher::update()
    │
    ├──────────────── EXTRACTION BOUNDARY ───────────────────────────
    │
    ▼  ExtractRenderWorld()  →  immutable RenderWorld
    │  (camera, lights, draw packets, debug draw, pick request, shadows)
    │
    ▼  RENDER PHASE
    │  Renderer::BeginFrame()    (GPU timeline wait, deferred deletions)
    │  GpuWorld::SyncFrame()     (scatter instance + config SSBO updates)
    │  BVHBuilder::Build()       (static BVH if dirty; dynamic BVH always)
    │  instance_cull.comp        (both BVHs → indirect draw buffers)
    │
    ▼  RenderGraph::Execute(RenderWorld)
    │   PickingPass (conditional)
    │   DepthPrepass (optional)
    │   GBufferPass (GPU-driven, vkCmdDrawIndexedIndirectCount)
    │   DeferredLightingPass (LightingPassRegistry contributors)
    │   ForwardPass: LinePass + PointPass + OverlaySurfacePass
    │   PostProcess: Bloom → ToneMap → AA
    │   SelectionOutline + DebugView + ImGui
    │
    ▼  vkQueueSubmit + vkQueuePresentKHR
    │
    ▼  MAINTENANCE PHASE
        PickingSystem::ProcessReadback() → PickResult → SelectionModule
        GpuWorld::CollectGarbage()       (timeline-based deferred deletions)
        TransferManager::GarbageCollect()
        Telemetry, hot-reload bookkeeping
```

---

## 13. Open Questions & Deferred Items

### Resolved ✅

| # | Decision |
|---|----------|
| §13.1 | **Free-list allocator** with deferred compaction on fragmentation threshold. In-place overwrite for same-count edits, reallocation for larger. |
| §13.3 | **Derivative-based TBN** computed in fragment shader from `dFdx/dFdy(worldPos, uv)`. No tangent vertex attribute. Falls back to vertex normal SSBO or geometric derivative when UVs absent. |
| §13.7 | **Per-entity GPU data** fully specified in §5.1. Split into `GpuInstanceData` (96 bytes, frequent updates) + `GpuEntityConfig` (96 bytes, rare updates). BDA pointers for vertex normals, scalar props, color props, per-point sizes live in `GpuEntityConfig`. |
| §13.9 | **Option B (SSBO + BDA)** for all geometry types and all domains. Vertex-domain = vertex shader fetch → varying interpolation. Face-domain = fragment shader `gl_PrimitiveID` → flat. Edge-domain = vertex shader on line primitives. |
| §13.12 | `DebugDraw` remains per-frame transient. Overlay entities for persistent multi-frame content. |
| §13.13 | `GBuf_EntityId` is always written by `GBufferPass`. Used by `SelectionOutlinePass`. Eliminates redundant picking-buffer entity channel. |
| §13.14 | No normal map + no vertex normals + `UNLIT` flag → albedo-only output, no BRDF evaluation. When normals are available (vertex SSBO or derivatives), lighting proceeds normally. |
| §13.2  | **Static/Dynamic BVH split** via `ECS::StaticTag`. See §3.3 extension below. |
| §13.4  | **Modular `ILightingContributor` registry.** Implementation order: Directional → Ambient → PointLight → SpotLight → IBL → AreaLight. See §4.5. |
| §15.A  | **::Data component elimination.** `Mesh::Data`, `Graph::Data`, `PointCloud::Data` removed. Data → `GeometrySources`. Visualization → `ECS::Visualization::Config`. GPU state → render components. Algorithm state → per-algorithm ECS components. See §15.0 Issue A. |
| §15.C  | **Normal map + vertex normal composition.** Vertex normals and normal maps compose (not override). Base normal from vertex SSBO or geometric derivative; normal map perturbs in TBN space relative to base. See §4.2. |
| §15.D  | **Algorithm module self-registration.** Each compile-time module registers its menu entry, widget factory, component descriptor, and event sink. Inspector and menu bar are fully registry-driven. See §15.6. |

---


### Deferred Items

| # | Item | Prerequisite |
|---|------|-------------|
| §13.5 | **Transparency / OIT** — OIT approach not chosen (Weighted Blended, depth peeling, PPLL). Transparent surfaces slot into `ForwardPass` after `OverlaySurfacePass`. | Stable deferred pipeline |
| §13.6 | **Vertex Displacement Mapping** — `DisplacementID` field is in `GpuMaterialData` as a placeholder. Visually meaningful only with tessellation shaders or very dense input meshes. | Tessellation shader support |
| §13.8 | **Gaussian Splatting** — Requires per-splat covariance + SH coefficients in a separate `SplatAttributeBuffer` SSBO (BDA ptr in `GpuEntityConfig`). Requires depth-sort before rendering. Likely needs a dedicated `GaussianSplatPass` (forward, after deferred). | GS data model design |
| §13.10 | **Htex + Deferred Pipeline** — Htex atlas indices must reach `GBufferPass` fragment shader. Existing `HtexPatchPreviewPass` is forward-only. Integration path unspecified. | Stable GBuffer pass |
| §13.11 | **Vector Field Arrowheads** — Options: geometry-shader cone, billboard quad tip, pre-baked arrow mesh per vector. No implementation yet. | Vector field pipeline stable |
| — | **Transparency for point clouds** — Sorted splat blending (alpha-composited PointPass). | OIT / transparency pass |
| — | **GPU K-means / spectral clustering** — Heavy geometry compute on Vulkan compute queues (CUDA-Vulkan interop for CUDA path). | CUDA-Vulkan interop (C14) |
| — | **SSAO / GTAO** — Screen-space AO pass between `DeferredLightingPass` and `ForwardPass`. Requires depth prepass for HiZ. | Stable deferred pipeline |

---

## 14. Simulate → Render Loop & System Boundaries

### 14.1 The Frame Loop

The loop has four strictly sequential phases. The **extraction boundary** is the hard wall between mutation and rendering — nothing writes ECS state after it.

```
┌─────────────────────────────────────────────────────────────────────┐
│  SIMULATE PHASE  (CPU — ECS systems, task graph)                    │
│                                                                     │
│  Platform tick                                                      │
│    glfwPollEvents, resize detection, idle throttle sleep            │
│                                                                     │
│  Fixed-step ticks  [0..N steps, dt = 1/60s]                        │
│    physics, animation, gameplay scripts                             │
│    world.CommitTick()  ← authoritative state becomes consistent     │
│                                                                     │
│  Variable-tick ECS systems  (DAGScheduler order)                    │
│    PropertySetDirtySyncSystem    dirty flags → GpuDirty             │
│    MeshRendererLifecycleSystem   geometry → GpuWorld.GeoStore       │
│    GraphLifecycleSystem          graph geometry → GpuWorld.GeoStore │
│    PointCloudLifecycleSystem     cloud → GpuWorld.GeoStore          │
│    GPUSceneSyncSystem            transforms → GpuWorld.InstanceBuf  │
│    EntityConfigSyncSystem        vis config → GpuWorld.EntityConfigBuf│
│    StaticBVHDirtySyncSystem      StaticTag changes → BVH dirty      │
│    PrimitiveBVHBuildSystem       CPU BVH for picking                │
│    VectorFieldSyncSystem         bake vfield child graph verts      │
│                                                                     │
│  Deferred event dispatch                                            │
│    entt::dispatcher::update()    SelectionChanged, GeoModified...   │
│                                                                     │
│  OnRender callbacks                                                 │
│    DebugDraw accumulation, gizmo update                             │
│                                                                     │
├──────────────────────── EXTRACTION BOUNDARY ────────────────────────┤
│                                                                     │
│  PrepareEditorOverlay                                               │
│    GUI::BeginFrame() + DrawGUI() → EditorOverlayPacket              │
│                                                                     │
│  ExtractRenderWorld()  ← NO MORE ECS WRITES AFTER THIS             │
│    reads SceneManager (read-only snapshot)                          │
│    reads GpuWorld (query slots, BDA pointers, material handles)     │
│    reads DebugDraw accumulators (frozen copies)                     │
│    produces: immutable RenderWorld                                  │
│                                                                     │
├─────────────────────────────────────────────────────────────────────┤
│  RENDER PHASE  (GPU — render graph)                                 │
│                                                                     │
│  Renderer::BeginFrame()                                             │
│    GPU timeline wait, deferred deletions, geometry pool GC          │
│    GpuWorld::SyncFrame()  scatter pending instance + config updates  │
│                                                                     │
│  BVH compute prologue                                               │
│    StaticBVH build (if dirty)                                       │
│    DynamicBVH build (every frame)                                   │
│    instance_cull.comp → Surface/Line/Point indirect draw buffers    │
│                                                                     │
│  RenderGraph::Execute(RenderWorld)                                  │
│    [all passes execute on immutable RenderWorld]                    │
│    PickingPass → GBufferPass → DeferredLighting → Forward →         │
│    PostProcess → SelectionOutline → DebugView → ImGui               │
│                                                                     │
│  vkQueueSubmit + vkQueuePresentKHR                                  │
│                                                                     │
├─────────────────────────────────────────────────────────────────────┤
│  MAINTENANCE PHASE  (CPU — after GPU submit)                        │
│                                                                     │
│  PickingSystem::ProcessReadback() → PickResult → SelectionModule    │
│  GpuWorld::CollectGarbage()       deferred deletions by timeline    │
│  TransferManager::GarbageCollect()                                  │
│  MaterialRegistry::ProcessDeletions()                               │
│  Telemetry capture                                                  │
│  ShaderHotReload bookkeeping                                        │
└─────────────────────────────────────────────────────────────────────┘
```

### 14.2 System Definitions (Simulate Phase)

Each ECS system has exactly **one responsibility**. Systems are registered in `RegisterEngineSystems()` and ordered by `DAGScheduler` based on declared read/write dependencies.

| System | Reads | Writes | What it does |
|--------|-------|--------|-------------|
| `PropertySetDirtySyncSystem` | `DirtyTag::*` components | `GpuDirty` flag, escalated dirty tags | Translates domain dirty tags (vertex/edge/face) into `GpuDirty`. Attribute-only changes extract cached color/radius vectors without full re-upload. |
| `MeshRendererLifecycleSystem` | `DataAuthority::MeshTag`, `GeometrySources::*`, `GpuDirty` | `GpuWorld::GeoStore` (managed buffer region), `BufferView`, `Surface::Component`, instance slot | Phase 1: upload geometry to managed buffers. Phase 2: allocate/update instance slot + AABB. Phase 3: populate `Surface::Component`. No `Mesh::Data` — reads directly from GeometrySources. |
| `GraphLifecycleSystem` | `DataAuthority::GraphTag`, `GeometrySources::Nodes/Edges`, `GpuDirty` | `GpuWorld::GeoStore`, `BufferView`, `Line::Component`, `Point::Component`, instance slot | Same three phases as mesh lifecycle, for graph edge pairs and node positions. Upload mode selected per-entity by `ECS::StaticTag` presence. No `Graph::Data` — reads directly from GeometrySources. |
| `PointCloudLifecycleSystem` | `DataAuthority::PointCloudTag`, `GeometrySources::Vertices`, `GpuDirty` | `GpuWorld::GeoStore`, `BufferView`, `Point::Component`, instance slot | Reads positions/normals spans from GeometrySources zero-copy, uploads via Staged mode. No `PointCloud::Data`. |
| `GPUSceneSyncSystem` | `Transform::Component` changes | `GpuWorld::InstanceBuffer` (model matrix) | Transform-only updates: writes model matrix to instance slot without re-uploading geometry. |
| `EntityConfigSyncSystem` | `Visualization::Config`, `GeometrySources`, `BufferView`, render components | `GpuWorld::EntityConfigBuffer` (BDA ptrs, vis config) | Syncs the `GpuEntityConfig` record when any per-entity visualization or attribute config changes. Uploads attribute SSBOs (normals, scalar props, color props) and writes BDA pointers. Does NOT touch `GpuInstanceData` (transforms are handled by `GPUSceneSyncSystem`). |
| `StaticBVHDirtySyncSystem` | `ECS::StaticTag` add/remove | `GpuWorld::BVHBuilder.StaticDirty` flag | Detects when entities gain/lose `StaticTag` and marks the static BVH for rebuild. |
| `PrimitiveBVHBuildSystem` | Geometry changes | CPU-side BVH (in `GeometryCollisionData`) | Rebuilds CPU BVH for entities that changed, used by the CPU picking refinement path. |
| `VectorFieldSyncSystem` | `Visualization::Config::VectorFields`, `VectorFieldsDirty` | Child graph entity managed buffer region | CPU-bakes vector field endpoint pairs (base + base+vector*scale) for each domain. Uploads to child Graph entity's managed buffer region. |
| `MeshViewLifecycleSystem` | `Surface::Component` (present/absent), `GeometrySources::Edges` | `Line::Component` (edge view), `Point::Component` (vertex view) | Creates/destroys wireframe and vertex visualization views that share the mesh's managed buffer region via `BufferView.ReuseFrom`. |

### 14.3 Module Boundaries

Each module has a single owner, a declared dependency direction, and a strict interface. No module reaches across a boundary to mutate another module's owned data.

```
Module                   Owns                          Depends on
─────────────────────────────────────────────────────────────────────────
Core.TaskGraph           CPU fiber scheduler           (none — foundational)
Core.FrameGraph          ECS DAG scheduler             Core.TaskGraph

ECS.Scene                entt::registry, ECS systems   Core.FrameGraph
ECS.Components.*         component type declarations   (ECS kernel only)

Runtime.SceneManager     ECS registry instance         ECS.Scene
Runtime.RenderExtraction RenderWorld type + extractor  Runtime.SceneManager,
                                                        Graphics.GpuWorld (read)

Graphics.GpuWorld        Managed vertex/index buffers,  RHI.BufferManager, RHI.Transfer
                         InstanceBuf, EntityConfigBuf,  RHI.ComputePipeline
                         MaterialRegistry, BVHBuilder

Graphics.RenderGraph     transient resource DAG,        RHI.Device, RHI.Buffer
                         barrier calculation, aliasing

Graphics.DefaultPipeline IRenderFeature list,           Graphics.RenderGraph,
                         LightingPassRegistry            Graphics.GpuWorld (read)

Graphics.Passes.*        individual pass logic          Graphics.RenderGraph,
                                                        Graphics.GpuWorld (read)

Runtime.Renderer         owns GpuWorld + RenderGraph   All Graphics.*
                         + DefaultPipeline,
                         drives the render phase
```

**Dependency direction rule:** arrows flow downward. A module may only depend on modules below it in this table. `ECS.Scene` never imports `Graphics.*`. `Graphics.*` never imports `ECS.*`. The interface between them is `RenderWorld` — the immutable extraction product.

### 14.4 Render Graph vs. Task Graph

These are two different DAGs with different purposes:

| | CPU Task Graph (`Core.TaskGraph`) | GPU Render Graph (`Graphics.RenderGraph`) |
|---|---|---|
| **Node** | A CPU job (fiber) | A GPU render/compute pass |
| **Edge** | Data dependency between CPU jobs | Resource producer/consumer relationship |
| **Scheduler** | `Core::DAGScheduler` (topological sort, work-stealing) | `RenderGraph::Compile()` (barrier calculation, aliasing) |
| **Execution** | Worker threads (fiber pool) | `vkCmdExecuteCommands` on primary command buffer |
| **Scope** | Simulate phase + maintenance phase | Render phase only |
| **Mutable world** | Yes — ECS systems mutate components | No — reads only from immutable `RenderWorld` |

The ECS `FrameGraph` (DAGScheduler over ECS systems) is the CPU task graph for the simulate phase. The `Graphics::RenderGraph` is the GPU DAG for the render phase. They never run concurrently within a single frame in the current sequential loop — the extraction boundary separates them.

**Future:** B4b (incremental parallelization) will allow simulate-phase jobs to run on worker threads. The extraction boundary remains the synchronization point — `ExtractRenderWorld()` acts as the `world.CommitTick()` equivalent, consuming all simulate work before the render phase begins.

### 14.5 RenderWorld — the Contract Object

`RenderWorld` is the sole interface between the simulate phase and the render phase. It is immutable from the moment `ExtractRenderWorld()` returns.

```cpp
struct RenderWorld {
    // Camera & view
    RenderViewPacket       View;         // camera matrices, viewport, FOV
    double                 Alpha;        // interpolation factor for fixed-step

    // Scene draws (extracted from ECS, reference GpuWorld slots)
    std::span<SurfaceDrawPacket>  SurfaceDraws;
    std::span<LineDrawPacket>     LineDraws;
    std::span<PointDrawPacket>    PointDraws;

    // Lighting
    LightEnvironmentPacket Lighting;     // directional + ambient (UBO upload)
    std::span<GpuLightEntry> Lights;     // point/spot/area (LightBuffer SSBO)

    // Editor / interaction
    EditorOverlayPacket    EditorOverlay;
    PickRequestSnapshot    PickRequest;
    DebugViewSnapshot      DebugView;
    GpuSceneSnapshot       GpuScene;     // active instance count

    // Transient debug content (frozen copies from DebugDraw accumulators)
    std::span<DebugLine>     DebugLines;
    std::span<DebugLine>     DebugOverlayLines;
    std::span<DebugPoint>    DebugPoints;
    std::span<DebugTriangle> DebugTriangles;

    // Shadow
    ShadowCascadeMatrices  Shadows;
};
```

Passes receive `RenderWorld` (or a lightweight `RenderPassContext` that wraps it with GPU plumbing). They read. They never write back.

---

*All architectural decisions resolved. This document is the implementation specification. Deferred items in §13 are tracked backlog — they do not block core pipeline work.*

---

## 15. Domain Discovery, Property UI & Algorithm Modules

### 15.0 Issues — Read This First

Before the clean design, here are the places where the ideas as stated **can't work as described**. Each has a resolution.

#### ✅ Issue A — PropertySet Duality & ::Data Component Elimination (resolved)

**Resolution — Shared-Ownership Mesh Interface:** Each `GeometrySources` domain component holds its PropertySet via `std::shared_ptr<PropertySet>`. The `Halfedge::MeshView` interface, when created for an entity, takes shared ownership of these PropertySets — it holds `shared_ptr` copies, not raw references. `meshView.VertexProperties()` IS `GeometrySources::Vertices.Properties` — the same heap object, co-owned.

Concretely: `GeometrySources::Vertices`, `::Edges`, `::Halfedges`, `::Faces` each store `std::shared_ptr<PropertySet> Properties`. When an algorithm creates a `MeshView`, it copies these `shared_ptr`s. This guarantees lifetime safety: even if entt reallocates its component pools (e.g., emplacing a component on another entity), the PropertySet data remains valid because the `MeshView` holds a shared reference. All reads and writes through the mesh interface operate directly on the ECS-owned PropertySet data. No sync, no copy of the data itself, no duality.

**Algorithm-specific scratch data:** When an algorithm needs temporary working data that should NOT appear as a persistent entity property (e.g., intermediate quadric matrices during simplification), it creates a local `MeshScratchProperties` struct that it owns. This data is never written to `GeometrySources` and is discarded when the algorithm completes.

**Resolution — Eliminating `Mesh::Data`, `Graph::Data`, `PointCloud::Data`:** These intermediate `::Data` components are eliminated entirely. Their responsibilities are redistributed:

| Current `::Data` field | Destination | Rationale |
|------------------------|-------------|-----------|
| `MeshRef` / `GraphRef` / `CloudRef` | **Eliminated.** Algorithms create mesh/graph/cloud interfaces on-demand from `GeometrySources`. | Interfaces are computation tools, not persistent state. |
| `VisualizationConfig` | **`ECS::Visualization::Config`** — standalone ECS component (see §15.5). | Visualization config is entity-wide, not tied to a specific render pass. |
| Rendering params (colors, sizes, widths, modes, overlay) | **Absorbed into `Surface::Component` / `Line::Component` / `Point::Component`.** | These are pure rendering concerns — they belong on the render component. |
| GPU state (`GpuGeometry`, `GpuSlot`, `GpuDirty`, cached attribute vectors) | **Absorbed into render components.** `GpuDirty` becomes a `DirtyTag` component. | GPU lifecycle state is per-render-component, not per-data-authority. |
| K-means state | **`ECS::KMeans::Component`** (see §15.5). | Algorithm-specific state has no business on geometry data components. |
| CUDA resources | **`ECS::KMeans::CudaResources`** (companion component, `#ifdef INTRINSIC_HAS_CUDA`). | Same — algorithm-specific GPU resources. |
| Query helpers (`VertexCount()`, `HasColors()`, etc.) | **Derived from `GeometrySources` PropertySets or `BufferView`.** | No delegation through a Ref needed — GeometrySources IS the data. |

**What identifies an entity's geometry type?** `DataAuthority::MeshTag` / `GraphTag` / `PointCloudTag` (zero-size components, already exist). No `::Data` component needed for type identity.

**What the render components become after absorption:**

- **`Surface::Component`** — geometry handle, material handle, GPU slot, visibility, cached vertex/face colors, face color dirty flags, per-vertex color mode (smooth/nearest/centroid Voronoi). Already holds most of this today.
- **`Line::Component`** — geometry + edge view handles, edge count, uniform color/width/overlay, per-edge color cache, domain hint. Absorbs `Graph::Data`'s `DefaultEdgeColor`, `EdgeWidth`, `EdgesOverlay`.
- **`Point::Component`** — geometry handle, uniform color/size/mode/size-multiplier, per-point attribute flags, per-point color/radii caches, domain hint. Absorbs `Graph::Data`'s node rendering params and `PointCloud::Data`'s rendering params.

**Lifecycle systems** read `DataAuthority` tags + `GeometrySources` to determine what to create. They set appearance defaults on render components at creation time. There is no intermediate `::Data` component to bridge.

**Migration note:** `GeometrySources` domain components change from owning `PropertySet` directly to owning `std::shared_ptr<PropertySet>`. The halfedge mesh constructor (`MeshView`) accepts `shared_ptr<PropertySet>` parameters. All construction sites that currently build a mesh from a file or scratch must pass the entity's `GeometrySources` components' shared pointers. The existing `GeometrySourcesPopulate` logic (which already syncs mesh ↔ ECS) is replaced by this shared-ownership construction. All lifecycle systems that currently read from `::Data` components switch to reading from `GeometrySources` + render components.

#### ✅ Issue B — "Runtime Modules" = Compile-Time Registered Units

C++23 modules have no stable ABI. You cannot `dlopen` a `.cppm` module at runtime. `docs/roadmap.md` explicitly bans this.

**Resolution:** Algorithm modules are **compile-time registered units** that call `AlgorithmRegistry::Register(descriptor)` during engine startup. Each module is a C++23 module that is linked into the binary at build time. "Not present" means "not linked into the binary" (conditional CMake target) or "not registered" (startup flag). Not "loaded at runtime." Each compile-time module self-registers its menu entries, widget factories, ECS component descriptors, and event sinks in a single `Register()` call during `Engine::Init()`.

#### ✅ Issue C — Normal Map vs. Vertex Normal Coexistence (resolved)

**Problem:** §4.2 originally treated vertex normals and normal maps as alternatives in a priority chain (normal map wins if present → else vertex normals → else geometric). This is incorrect. In standard PBR rendering, vertex normals and normal maps **compose**: vertex normals define the smooth base surface, and the normal map adds detail perturbation in tangent space relative to that base.

**Resolution:** The normal sourcing chain (§4.2) is rewritten as a two-stage composition:

1. **Compute base normal** from the best available source (vertex SSBO or geometric derivative).
2. **Apply normal map perturbation** (if present) using TBN built from the base normal + derivative tangent.

This means a mesh with BOTH vertex normals AND a normal texture gets the intended result: smooth vertex normals define the large-scale shape, and the normal map adds fine detail (bumps, pores, surface texture) on top. See updated §4.2 for the complete shader flow.

#### ✅ Issue D — Algorithm Module Self-Registration (Menu + Widgets) (resolved)

`ECS::Mesh::Data` currently holds K-means state (`KMeansJobPending`, `KMeansLastInertia`, etc.), `KMeansCentroids`, and more. This is exactly the anti-pattern being replaced. **All of this moves to `ECS::KMeans::Component`.**

**Resolution:** Each algorithm module is a **self-contained compile-time unit** that owns:
1. **Menu registration** — provides a menu path (`"Geometry/Clustering/K-Means"`) and action callback. The main menu bar is built dynamically from `AlgorithmRegistry` entries — no hardcoded menu items in application code.
2. **Widget factory** — provides an ImGui draw function for its Inspector panel (parameter controls + result display).
3. **ECS component descriptor** — registers the component type for runtime reflection so the Inspector can discover and draw panels for algorithm-specific components present on an entity.
4. **Event sink** — subscribes to its own request event type for async job dispatch.

The Inspector and main menu bar query `AlgorithmRegistry` and `ComponentRegistry` to enumerate available algorithms and present components. Adding a new algorithm module to the binary automatically populates the UI — zero changes to `InspectorController` or `MenuBar`. See §15.6 for the full pattern.

---

### 15.1 The Authoritative Data Model

```
Entity
 ├── ECS::DataAuthority::MeshTag         ← IDENTITY (zero-size, one per entity)
 │
 ├── ECS::Components::GeometrySources::Vertices    ← AUTHORITATIVE for all vertex data
 │    └── Properties: shared_ptr<PropertySet>     (co-owned by MeshView when active)
 │         "v:position"   vec3[]    — positions (canonical)
 │         "v:normal"     vec3[]    — normals (written by NormalEstimation or loader)
 │         "v:color"      vec3[]    — vertex colors (written by colorizing algorithms)
 │         "v:geodesic"   float[]   — geodesic distance (written by HeatMethod)
 │         "v:cluster_id" int[]     — cluster labels (written by KMeans)
 │         ... any algorithm result with "v:" prefix
 │
 ├── ECS::Components::GeometrySources::Edges        ← AUTHORITATIVE for all edge data
 │    └── Properties: shared_ptr<PropertySet>     (co-owned by MeshView when active)
 │         "e:v0", "e:v1"           — endpoint indices (topology, canonical)
 │         "e:length"   float[]     — edge lengths (written by MeshQuality)
 │         "e:color"    vec3[]      — per-edge colors
 │         ... any algorithm result with "e:" prefix
 │
 ├── ECS::Components::GeometrySources::Halfedges    ← AUTHORITATIVE for halfedge topology
 │    └── Properties: shared_ptr<PropertySet>     (co-owned by MeshView when active)
 │         "h:to_vertex", "h:next", "h:face"        — topology (canonical)
 │
 ├── ECS::Components::GeometrySources::Faces        ← AUTHORITATIVE for all face data
 │    └── Properties: shared_ptr<PropertySet>     (co-owned by MeshView when active)
 │         "f:halfedge"             — topology (canonical)
 │         "f:normal"   vec3[]      — face normals (written by MeshAnalysis or shader)
 │         "f:area"     float[]     — face areas
 │         "f:curvature" float[]    — mean curvature per face
 │         ... any algorithm result with "f:" prefix
 │
 ├── ECS::Visualization::Config          ← VISUALIZATION SELECTION (entity-wide)
 │    ColorSource VertexColors, EdgeColors, FaceColors
 │    IsolineConfig Isolines
 │    VectorFieldEntry[] VectorFields
 │    bool UseNearestVertexColors
 │
 ├── ECS::Surface::Component             ← RENDER STATE (GPU handles, cached colors)
 ├── ECS::Line::Component                ← RENDER STATE (edge GPU handles, appearance)
 ├── ECS::Point::Component               ← RENDER STATE (point GPU handles, appearance)
 │
 └── [algorithm components, emplaced on-demand]
      ECS::KMeans::Component, ECS::Geodesic::Component, ...
```

**No `Mesh::Data`, `Graph::Data`, or `PointCloud::Data` components exist.** Entity type is determined solely by `DataAuthority::*Tag`. All persistent data lives in `GeometrySources`. All render state lives on render components. All algorithm state lives on algorithm-specific components.

**Halfedge::Mesh as on-demand computation interface:**

When an algorithm needs topology traversal (neighbor iteration, valence queries, boundary detection), it creates a `Halfedge::MeshView` that **shares ownership of the entity's GeometrySources PropertySets** via `shared_ptr`:

```cpp
// Algorithm code — NOT stored on any ECS component
auto meshView = Halfedge::MeshView(
    reg.get<GeometrySources::Vertices>(entity).Properties,    // shared_ptr<PropertySet>
    reg.get<GeometrySources::Edges>(entity).Properties,       // shared_ptr<PropertySet>
    reg.get<GeometrySources::Halfedges>(entity).Properties,   // shared_ptr<PropertySet>
    reg.get<GeometrySources::Faces>(entity).Properties        // shared_ptr<PropertySet>
);
// meshView.VertexProperties() IS GeometrySources::Vertices.Properties — same heap object
// MeshView holds shared_ptr copies → safe even if entt reallocates component pools
// Reads and writes go directly to ECS data, zero copy of the data itself
auto result = Geometry::Smoothing::Smooth(meshView, params);
```

The `MeshView` is a stack-local object. It is **never stored on an ECS component**. It holds `shared_ptr` copies to the PropertySets, so it is safe against entt component pool reallocation (e.g., emplacing a component on another entity between view creation and use). All mutations through the view are live in the ECS. When the algorithm returns, the view is discarded and releases its shared ownership. The algorithm's results are already in `GeometrySources` because the view wrote them there directly.

**When an algorithm needs its own scratch data:** It creates a local `MeshScratchProperties` struct that it owns. This data is never written to `GeometrySources` and is discarded when the algorithm completes.

**Rule:** All algorithm results are written to the appropriate `GeometrySources` component, keyed by canonical domain prefix (`v:`, `e:`, `h:`, `f:`). No algorithm writes results to render components or algorithm components — only to `GeometrySources`. Lifecycle systems propagate changes from `GeometrySources` to render components (via dirty tags).

---

### 15.2 Domain Discovery

The UI discovers what an entity can display by inspecting which `GeometrySources` components are present. This already works via `ECS::Components::GeometrySources::BuildConstView()` and `DetectDomain()`.

```
Entity has GeometrySources::Vertices → show vertex domain UI
Entity has GeometrySources::Edges    → show edge domain UI
Entity has GeometrySources::Faces    → show face domain UI
Entity has GeometrySources::Nodes    → show graph node domain UI
```

For each domain, the Inspector enumerates all properties in that domain's `PropertySet` and groups them by value type:

| Property type | UI sections shown |
|--------------|------------------|
| `float`, `int`, `uint`, `bool`, `double` → scalar | Scalar Field selector, isoline config |
| `glm::vec3` | Color Source selector (RGB), Vector Field overlay adder |
| `glm::vec4` | Color Source selector (RGBA with per-element alpha) |
| `uint32_t` with name `*:cluster_id` or `*:label` | Discrete colormap selector |

Properties with canonical structural prefixes (`v:position`, `e:v0`, `e:v1`, `h:*`, `f:halfedge`) are shown in a read-only topology section, not offered as visualization sources.

---

### 15.3 Property → GPU Upload ("Baking")

When the user selects a property for visualization, the engine:

1. Reads the `float[]` or `vec3[]` data from the `GeometrySources` PropertySet.
2. Uploads it to a per-entity device-local SSBO (via `TransferManager` staged upload).
3. Writes the BDA pointer into `GpuEntityConfig.ScalarPropPtr` or `ColorPropPtr`.
4. Sets `GpuEntityConfig.VisDomain` to vertex/face/edge.
5. Sets dirty flag → `EntityConfigSyncSystem` picks it up next simulate phase.

This is triggered by `Visualization::Config` changes (user selects a different `PropertyName`). The "baked" property is the currently active visualization selection — not all properties are uploaded simultaneously, only the active one per domain.

The existing `Visualization::Config::VertexColors.PropertyName` (etc.) is the selector. The `EntityConfigSyncSystem` reads `Visualization::Config` + `GeometrySources` to determine what to upload and where the BDA pointer should point.

---

### 15.4 Inspector UI Layout (New)

```
[Entity Inspector]
│
├─ Name / Transform   (unchanged)
│
├─ Geometry Domains   (discovered from GeometrySources presence)
│   ├─ Vertices  [N vertices]
│   │   ├─ [PropertySet Browser]   — browse all "v:*" properties, see values
│   │   ├─ [Color Source]          — pick a vec3/vec4 property → vertex color
│   │   ├─ [Scalar Field]          — pick a float/int property → colormap
│   │   └─ [Vector Fields]         — add/remove vec3 properties as overlay arrows
│   ├─ Edges     [N edges]
│   │   ├─ [PropertySet Browser]
│   │   ├─ [Edge Color Source]
│   │   └─ [Edge Scalar Field]
│   ├─ Faces     [N faces]
│   │   ├─ [PropertySet Browser]
│   │   ├─ [Face Color Source]     — picks up face scalar → flat shading
│   │   └─ [Isoline Config]        — (vertex domain scalar only)
│   └─ Halfedges [N halfedges]     — topology view only, no visualization
│
├─ Algorithm Results   (discovered from ComponentRegistry — registry-driven)
│   ├─ [KMeans::Component]         — cluster count, inertia, centroid entity
│   ├─ [Geodesic::Component]       — source count, max distance
│   ├─ [Curvature::Component]      — gaussian/mean stats
│   └─ [... any registered algorithm component present on this entity]
│
├─ Algorithms          (from AlgorithmRegistry, filtered by entity domain)
│   ├─ [KMeansWidget]             — drawn by KMeansModule::DrawWidget()
│   ├─ [SimplificationWidget]     — drawn by SimplificationModule::DrawWidget()
│   └─ [... each module draws its own widget; Inspector has zero hardcoded knowledge]
│
└─ Render Components   (Surface / Line / Point config, material selector)
```

**Main Menu Bar (dynamic):**

```
Geometry (menu)
  ├─ Normals
  │   └─ Estimate Normals          ← NormalEstimationModule
  ├─ Smoothing
  │   └─ Laplacian Smooth          ← SmoothingModule
  ├─ Simplification
  │   └─ QEM Decimate              ← SimplificationModule
  ├─ Clustering
  │   └─ K-Means                   ← KMeansModule
  ├─ Analysis
  │   ├─ Gaussian Curvature        ← CurvatureModule
  │   └─ Geodesic Distance         ← GeodesicModule
  └─ ... (each registered module contributes its menu entry)
```

The main menu bar iterates `AlgorithmRegistry::MenuEntries()` grouped by category. Each entry's precondition lambda determines enabled/disabled state (e.g., K-Means is disabled when no entity is selected, or the selected entity has no vertex domain). Menu entries are sorted by registration order within each category.

---

### 15.5 Entity-Wide Components & Algorithm Component Pattern

#### Visualization::Config — Entity-wide visualization selection

`VisualizationConfig` was previously stored on `Mesh::Data`, `Graph::Data`, and `PointCloud::Data`. With those eliminated, it becomes its own ECS component:

```cpp
// ECS.Components.Visualization.cppm
export namespace ECS::Visualization {
    struct Config {
        Graphics::ColorSource VertexColors;      // "v:geodesic" → colormap
        Graphics::ColorSource EdgeColors;         // "e:length" → colormap
        Graphics::ColorSource FaceColors;         // "f:area" → colormap

        Graphics::IsolineConfig Isolines;

        bool UseNearestVertexColors = false;      // Voronoi-style rendering

        std::vector<Graphics::VectorFieldEntry> VectorFields;
        bool VectorFieldsDirty = false;
    };
}
```

Lifecycle: Emplaced when the entity first needs visualization configuration (e.g., user selects a scalar field in the Inspector, or an algorithm writes a result property). Not present on entities that use only material-based rendering. `EntityConfigSyncSystem` reads `Visualization::Config` + `GeometrySources` to populate `GpuEntityConfig` (BDA pointers, colormap, scalar range, domain flags).

#### Algorithm Components — Self-contained per-algorithm state

Each algorithm owns its own ECS component for parameters and results. Nothing algorithm-specific lives in render components, `GeometrySources`, or entity-wide config.

**Example: K-Means**

```cpp
// ECS.Components.KMeans.cppm
export namespace ECS::KMeans {
    struct Component {
        // --- Params (set by UI before job launch) ---
        uint32_t ClusterCount = 8;
        uint32_t MaxIterations = 32;
        Geometry::KMeans::Backend Backend = Geometry::KMeans::Backend::CPU;
        Geometry::KMeans::Initialization Init = Geometry::KMeans::Initialization::Hierarchical;
        uint32_t Seed = 42;

        // --- Job state ---
        bool JobPending = false;

        // --- Results (written by job callback on main thread) ---
        bool HasResults = false;
        bool LastConverged = false;
        float LastInertia = 0.0f;
        double LastDurationMs = 0.0;
        uint32_t LastIterations = 0;
        std::vector<glm::vec3> Centroids{};  // centroid positions
        entt::entity CentroidEntity = entt::null;  // overlay entity for centroid display
        uint64_t ResultRevision = 0;  // increments each run, used to detect stale caches
    };
}
```

The Inspector reads `ECS::KMeans::Component` (if present) to show results. It does NOT store this in `InspectorController` member variables.

**Migration of current `::Data` K-means fields:** Remove all K-means fields from `Mesh::Data`, `Graph::Data`, `PointCloud::Data`. Replace with `ECS::KMeans::Component` emplaced on the entity when K-means is first run. CUDA resources move to `ECS::KMeans::CudaResources` (companion component, `#ifdef INTRINSIC_HAS_CUDA`).

---

### 15.6 Algorithm Module System

An algorithm module is a **self-contained compile-time registered unit** that wires the Geometry implementation to ECS components, the UI (menu entries + Inspector widgets), and the event system. It is NOT a dynamic plugin.

**What each module registers at startup:**

```
AlgorithmModule::Register(AlgorithmRegistry& registry, ...)
  │
  ├── 1. Menu Entry           — path in the ImGui main menu bar (e.g. "Geometry/Clustering/K-Means")
  │                              + precondition lambda (enabled only when selected entity has valid domain)
  │                              + action callback (fires the algorithm's request event)
  │
  ├── 2. Inspector Widget     — ImGui draw function for its parameter controls + result display
  │                              owns all widget state (combo selections, sliders, progress indicators)
  │                              drawn by ComponentRegistry when the algorithm's component is present
  │
  ├── 3. Component Descriptor — registers ECS::KMeans::Component type for runtime reflection
  │                              (Inspector can discover and draw panels for present algorithm components)
  │
  └── 4. Event Sink           — subscribes to KMeansRequestedEvent on entt::dispatcher
                                  dispatches async Geometry job, writes results back on main thread
```

**Module structure:**

```cpp
// Runtime.AlgorithmModules.KMeans.cppm
export module Runtime.AlgorithmModules.KMeans;

import Geometry.KMeans;
import ECS;
import Core.Tasks;
import Runtime.AlgorithmRegistry;

export namespace Runtime::KMeansModule {

    // Called once during Engine::Init(). Registers everything.
    void Register(AlgorithmRegistry& registry, ComponentRegistry& components,
                  entt::dispatcher& dispatcher);
}
```

**Registration model:**

```
Engine::Init():
  AlgorithmRegistry algRegistry;
  ComponentRegistry compRegistry;

  KMeansModule::Register(algRegistry, compRegistry, dispatcher);
  SimplificationModule::Register(algRegistry, compRegistry, dispatcher);
  SmoothingModule::Register(algRegistry, compRegistry, dispatcher);
  // ... each linked module self-registers

Main Menu Bar (built dynamically):
  for each entry in algRegistry.MenuEntries():
      if entry.Category == currentMenuPath:
          if entry.Precondition(selectedEntity):
              ImGui::MenuItem(entry.Label) → entry.Action()

Inspector:
  for each algorithm component type registered in compRegistry:
      if entity has that component:
          draw its result panel + widget (provided by the module)

  for each algorithm module in algRegistry:
      if module supports entity's domain (mesh/graph/cloud):
          draw its parameter widget + Run button (provided by the module)
```

**AlgorithmModuleDescriptor:**

```cpp
struct AlgorithmModuleDescriptor {
    std::string_view   Name;          // "K-Means"
    std::string_view   MenuPath;      // "Geometry/Clustering/K-Means"
    uint32_t           SupportedDomains; // bitfield: Mesh|Graph|PointCloud

    // Precondition: is this algorithm available for the given entity?
    std::function<bool(entt::registry&, entt::entity)> IsAvailable;

    // Action: invoked when the user clicks the menu entry or "Run" button.
    std::function<void(entt::registry&, entt::entity)> Execute;

    // Widget: draws the full Inspector panel (params + results + Run button).
    // The module owns all widget state internally (not on InspectorController).
    std::function<void(entt::registry&, entt::entity, Runtime::Engine&)> DrawWidget;
};
```

**Event flow (algorithm invocation):**

```
UI: "Run KMeans" button clicked (from module's DrawWidget or menu entry)
  → fire KMeansRequestedEvent{entity, params}  via entt::dispatcher
  → KMeansModule sink receives event (main thread)
  → creates Core::Tasks job (runs on worker thread):
       result = Geometry::KMeans::Cluster(positions, params)
  → job completion callback (main thread):
       writes result scalars to GeometrySources::Vertices.Properties["v:cluster_id"]
       updates ECS::KMeans::Component on entity
       emits DirtyTag::VertexAttributes → PropertySetDirtySyncSystem picks up
       → EntityConfigSyncSystem uploads attribute SSBO
       → GPU shows updated colors next frame
```

**The "not present" guarantee:** If `KMeansModule::Register()` is never called (module not linked or startup flag disabled), the K-Means event sink is never registered, the menu entry never appears, the widget never appears in the Inspector, and the `ECS::KMeans::Component` is never emplaced. The algorithm is completely absent from the engine with zero overhead. No `#ifdef` needed in application code — the conditional is purely at the CMake link level.

---

### 15.7 Vector Field Overlays

Vector fields spawn child `Graph` overlay entities (§9.7). The base-position domain determines what the child stores:

| Domain | Base positions sourced from | Stored as |
|--------|----------------------------|----------|
| `Vertex` | `GeometrySources::Vertices["v:position"]` | Directly from parent managed buffer region |
| `Edge` | Computed: `(v0_pos + v1_pos) / 2` on CPU | `GeometrySources::Edges["e:midpoint"]` (cached property) |
| `Face` | Computed: centroid of face vertices on CPU | `GeometrySources::Faces["f:centroid"]` (cached property) |

Edge midpoints and face centroids are computed **once** and stored as properties in the parent entity's domain PropertySets. Subsequent vector field reconfigurations reuse them without recomputing. The child overlay's managed buffer region contains the baked `[base, base + vector * scale]` endpoint pairs.

**UI widget:** The existing `VectorFieldWidget` in `Runtime::EditorUI` already covers add/remove/configure. Extensions needed:
- Domain picker (Vertex / Edge / Face)
- `Normalize` toggle
- `Scale` slider  
- `LengthPropertyName` property picker (per-arrow scaling from scalar property)
- `ArrowColor` property picker (per-arrow color from scalar/vec3/vec4 property)

---

### 15.8 Overlay Baking (Standalone Entity Conversion)

Any overlay entity (child parented via `Hierarchy::Component`) can be **baked** into an independent entity via the UI. This is a one-way, non-undoable operation.

**Bake procedure (CPU first, then GPU):**
1. Create a new entity. Emplace `ECS::DataAuthority::*Tag`, `NameTag`, `Transform::Component` (world-space transform resolved from overlay + parent chain).
2. Copy all relevant `GeometrySources` properties from the overlay or its parent into new `GeometrySources` components on the new entity. Each domain gets a fresh `shared_ptr<PropertySet>` with deep-copied data.
3. Remove `Hierarchy::Component` from the new entity (detach from parent).
4. The new entity is now a standalone CPU entity with complete `GeometrySources` data.
5. Normal lifecycle systems detect the new entity (has `DataAuthority::*Tag` + `GeometrySources` but no GPU representation) and handle GPU upload on the next frame — allocating a managed buffer region, uploading geometry, creating `GpuInstanceData`/`GpuEntityConfig` slots, and emplacing render components.
6. The parent entity is **not modified**.
7. The original overlay entity is **not destroyed** (user can delete it manually if desired).

**Why CPU-first:** The baked entity is constructed as a complete CPU entity, and GPU resources are derived from that CPU data through the standard lifecycle path. This avoids `vkCmdCopyBuffer` from the parent's GPU region (which may be freed or compacted) and ensures the new entity's GPU representation is authoritative from its own `GeometrySources`. The 1-frame latency before the baked entity appears is acceptable (same as any new entity).

**Why not undoable:** The bake deep-copies potentially large geometry PropertySets. Storing the "before" state for undo would require holding duplicate geometry in memory. The operation is non-destructive to the parent, so it's safe to simply not undo it.

---

### 15.9 Asset Loading → GPU Instance Model

**Two-phase entity lifecycle:**

```
Phase 1 — Asset (CPU only):
  AssetIngestService loads file
  → creates entity with GeometrySources components (CPU data only)
  → entity exists in SceneManager but has NO GPU representation
  → entity is "loaded" but not "visible"
  → StrongHandle<AssetData> stored in ECS::Components::AssetRef::Component

Phase 2 — Instance (CPU + GPU):
  User clicks "Make Visible" (or scene load auto-instantiates)
  → MeshRendererLifecycleSystem emplace Surface/Line/Point components
  → GpuWorld::GlobalGeometryStore allocates managed buffer region
  → Async staged upload begins (TransferManager)
  → On transfer complete: GpuDirty cleared, GpuWorld::InstanceBuffer slot allocated
  → NEXT FRAME: entity appears in rendered output (1-frame latency is inherent)
```

**Multiple instances from one asset:** Multiple entities can hold `AssetRef` pointing to the same asset. Each gets its own `BufferView` (their own managed buffer region) and their own instance slot. Geometry is NOT shared at the GPU level (no instancing of the same buffer region) unless explicitly requested via `ReuseVertexBuffersFrom`. This is fine for a research/editor tool; instanced rendering is a separate concern.

**Handle model:** `StrongHandle<AssetData>` uses the existing `Core::ResourcePool` generational index system. The handle keeps the CPU asset data alive even if the entity is destroyed, allowing re-instantiation. When the last handle is released, the CPU asset is freed.

**1-frame latency is correct and unavoidable.** GPU data is uploaded asynchronously. The entity visually pops in on the frame after the transfer completes. For a research engine this is acceptable. If instant appearance is required in future, a "loading placeholder" mesh (e.g. a bounding box wireframe) could be shown during upload.

---

### 15.10 AlgorithmRegistry & ComponentRegistry — Self-Describing Modules

EnTT has no built-in runtime reflection. Two registries provide the discovery mechanism:

#### AlgorithmRegistry — Menu entries + available algorithm widgets

Each algorithm module registers an `AlgorithmModuleDescriptor` (see §15.6) at startup. The registry provides:

```cpp
class AlgorithmRegistry {
public:
    void Register(AlgorithmModuleDescriptor desc);

    // Iterate all registered module descriptors.
    std::span<const AlgorithmModuleDescriptor> Modules() const;

    // Iterate menu entries grouped by category, sorted by registration order.
    // Used by the main menu bar to dynamically build the Geometry menu.
    void ForEachMenuEntry(std::function<void(const AlgorithmModuleDescriptor&)> visitor) const;

    // Draw all algorithm widgets applicable to the given entity's domain.
    // Used by the Inspector's "Algorithms" section.
    void DrawAvailableWidgets(entt::registry& reg, entt::entity e, Runtime::Engine& eng);
};
```

The main menu bar calls `ForEachMenuEntry()` to populate its menus. The Inspector calls `DrawAvailableWidgets()` to show parameter controls for algorithms that support the selected entity's domain. **No hardcoded algorithm list exists anywhere in the application code.**

#### ComponentRegistry — Algorithm result panel reflection

Algorithm modules also register a component descriptor so the Inspector can discover and draw result panels for algorithm-specific components present on an entity:

```cpp
struct AlgorithmComponentDescriptor {
    std::string_view DisplayName;    // "K-Means Results"
    entt::id_type    ComponentType;  // entt::type_id<ECS::KMeans::Component>().hash()
    std::function<bool(entt::registry&, entt::entity)> HasComponent;
    std::function<void(entt::registry&, entt::entity, Runtime::Engine&)> DrawPanel;
};

class ComponentRegistry {
public:
    void Register(AlgorithmComponentDescriptor desc);
    void DrawAlgorithmPanels(entt::registry& reg, entt::entity e, Runtime::Engine& eng);
};
```

The Inspector calls `ComponentRegistry::DrawAlgorithmPanels()` after the fixed geometry domain sections. Only registered + present component panels are drawn. No hardcoded list in `InspectorController`.

This replaces the current pattern of per-algorithm member variables in `InspectorController` (`m_KMeansUi`, `m_RemeshingUi`, etc.) — those move into the `DrawWidget` / `DrawPanel` closure state owned by the module.

---

### 15.11 Migration Map — What Changes in Existing Code

This section is a checklist of breaking changes implied by §15. Nothing in §15 is backward compatible with the current codebase. Each item is a separate, auditable change.

| # | What changes | From | To | Impact |
|---|-------------|------|----|--------|
| M1 | Property authority | `MeshRef->VertexProperties()["v:*"]` | `GeometrySources::Vertices.Properties["v:*"]` | **All algorithms** — every geometry operator that writes result properties |
| M2 | ::Data elimination (Mesh) | `ECS::Mesh::Data` (MeshRef, VisualizationConfig, K-means state, AttributesDirty) | `GeometrySources` + `Visualization::Config` + `KMeans::Component` + render components | `Mesh::Data` deleted. All lifecycle systems, Inspector, spawn code updated. |
| M3 | ::Data elimination (Graph) | `ECS::Graph::Data` (GraphRef, rendering params, GPU state, K-means state) | `GeometrySources` + `Visualization::Config` + `Line/Point::Component` + `KMeans::Component` | `Graph::Data` deleted. `GraphLifecycleSystem`, Inspector, spawn code updated. |
| M4 | ::Data elimination (PointCloud) | `ECS::PointCloud::Data` (CloudRef, rendering params, GPU state, CUDA state, K-means state) | `GeometrySources` + `Visualization::Config` + `Point::Component` + `KMeans::Component` + `KMeans::CudaResources` | `PointCloud::Data` deleted. `PointCloudLifecycleSystem`, Inspector, spawn code updated. |
| M5 | Visualization config | `VisualizationConfig` embedded in `Mesh::Data`, `Graph::Data`, `PointCloud::Data` | `ECS::Visualization::Config` standalone component | All `data.Visualization.*` callsites → `reg.get<Visualization::Config>(e).*` |
| M6 | K-means state | K-means fields scattered across all three `::Data` components | `ECS::KMeans::Component` + `ECS::KMeans::CudaResources` | `KMeansWidget`, `PointCloudKMeans`, `GeometryWorkflowController` |
| M7 | Algorithm UI state | `InspectorController::m_*Ui` members + hardcoded menu items | `DrawWidget` closure state in each module + `AlgorithmRegistry` menu entries | `InspectorController`, all `DrawXxxWidget` callsites, `MenuBar` |
| M8 | Algorithm invocation | Direct call in widget `Draw*` | `entt::dispatcher` event → module sink → job | `GeometryWorkflowController`, all `DrawXxxWidget` impls |
| M9 | Inspector component sections | Hardcoded per-algorithm `if (has<KMeans>)` | `ComponentRegistry::DrawAlgorithmPanels()` | `InspectorController::Draw()` |
| M10 | Attribute SSBO upload | `CachedVertexColors` vectors in render components | `GpuEntityConfig.ColorPropPtr` / `ScalarPropPtr` BDA | `PropertySetDirtySyncSystem`, `EntityConfigSyncSystem` (new), all lifecycle systems |
| M11 | Geometry buffer ownership | Per-entity `GeometryPool` handles | `GpuWorld::GlobalGeometryStore` via `BufferManager` | `GeometryPool`, all lifecycle systems, `GeometryGpuData::CreateAsync` |
| M12 | GPUScene scope | `Graphics::GPUScene` (instances + AABBs only) | `GpuWorld` (all GPU scene data) | `RenderOrchestrator`, `RenderDriver`, `Graphics.GPUScene` module |
| M13 | Normal map composition | Priority chain (normal map overrides vertex normals) | Two-stage composition (base normal + normal map perturbation in TBN) | `gbuffer.frag` shader, §4.2 |
| M14 | On-demand mesh interface | `Mesh::Data::MeshRef` (persistent shared_ptr) | `Halfedge::MeshView` created on stack by algorithm code | All algorithm callsites that use `MeshRef` for topology traversal |

**Recommended order:**
1. **M1** (property authority redirect) — foundational, all algorithms depend on this.
2. **M5** (extract `Visualization::Config`) — decouples visualization from ::Data, unblocks M2–M4.
3. **M6** (extract K-means to own component) — removes algorithm state from ::Data, unblocks M2–M4.
4. **M2, M3, M4** (eliminate ::Data) — the big structural change. Can be done per-type.
5. **M7, M8, M9** (algorithm module self-registration) — UI restructuring, independent of data model.
6. **M10** (attribute SSBO via BDA) — render pipeline change, independent of ECS changes.
7. **M11, M12** (GpuWorld consolidation) — independent parallel track.
8. **M13** (normal composition) — shader-only change, independent of all ECS work.
9. **M14** (on-demand mesh interface) — can land alongside or after M1.

---

*All architectural decisions resolved. §13 deferred items and §15.11 migration items are the implementation backlog. The ::Data components (Mesh::Data, Graph::Data, PointCloud::Data) are superseded by the GeometrySources + render component + algorithm component model.*
