# Htex-Inspired Halfedge-Pair Patch System

This document describes a proposed **float-render-target-first** patch system for IntrinsicEngine, inspired by Htex but adapted to the engine’s existing halfedge, PropertySet, and bindless-texture architecture.

The goal is to make **undirected edges** the storage primitive for surface-space render targets while keeping the implementation compatible with the current geometry kernel and render graph.

## 1. Mathematical / Topological Model

Let a halfedge mesh be $M = (V, H, E, F)$, with the usual operators:

- `Next(h)`
- `Prev(h)`
- `Twin(h)` / `OppositeHalfedge(h)`
- `Edge(h)`
- `Face(h)`

For every interior edge $e \in E$, define one square patch domain

$$
Q_e = [0,1]^2.
$$

The two halfedges incident to the same undirected edge share the same patch. The patch is conceptually split along the diagonal, so one halfedge owns one triangle of the square and the twin halfedge owns the mirrored triangle.

For a triangle corner expressed in local barycentric coordinates $(\lambda_0, \lambda_1, \lambda_2)$, the patch mapping is a piecewise-affine map

$$
\phi_h : \Delta \rightarrow Q_{Edge(h)}
$$

chosen so that orientation is consistent across the halfedge/twin pair. In practice, we only need a small shader helper that:

1. identifies the canonical halfedge orientation for the edge,
2. reflects the local coordinates when the opposite orientation is used,
3. emits patch UVs in $[0,1]^2$.

For v1, **boundary edges are one-sided**: they still map to a patch layer, but only one triangle side is populated.

### Complexity

- Patch enumeration: $O(|H|)$ time, $O(|E|)$ space.
- Patch lookup in shaders: $O(1)$.
- Optional neighborhood sampling (`Next`/`Prev`): constant-time topological indirections plus texture fetches.

## 2. Engine Fit

This feature fits the engine as a **new geometry-side patch authoring module** plus an **optional graphics-side pass**.

Relevant existing integration points:

- `src/Runtime/Geometry/Geometry.HalfedgeMesh.cppm`
  - halfedge connectivity and edge indexing
  - `Halfedge::Mesh::Edge(HalfedgeHandle)`
  - `Halfedge::Mesh::OppositeHalfedge(HalfedgeHandle)`
  - `Halfedge::Mesh::NextHalfedge(HalfedgeHandle)` / `PrevHalfedge(...)`
- `src/Runtime/Geometry/Geometry.MeshUtils.cppm`
  - `BuildHalfedgeMeshFromIndexedTriangles(...)`
  - `ExtractIndexedTriangles(..., triangleFaceIds)`
- `src/Runtime/RHI/RHI.Texture.cppm`
  - texture handle / bindless slot ownership
- `src/Runtime/RHI/RHI.TextureSystem.cppm`
  - stable GPU texture allocation and publication
- `src/Runtime/RHI/RHI.Bindless.cppm`
  - bindless descriptor updates
- `src/Runtime/Graphics/Graphics.RenderPipeline.cppm`
  - frame-resource declaration
- `src/Runtime/Graphics/Graphics.Passes.*`
  - optional pass integration for bake/preview/debug views

The patch system should **not** replace `SurfacePass`, `LinePass`, or `PointPass`. It is an orthogonal lane that can be previewed through `DebugViewPass` or an explicit feature-flagged pass.

## 3. Phase-1 Scope

Phase 1 is intentionally limited to **float render targets only**.

Supported payloads in v1:

- float color / scalar fields
- float diagnostics (weights, coverage, normalization)
- visualization of existing float-derived labels after color mapping

Deferred to later phases:

- native integer labels
- bitmasks
- packed categorical IDs
- mixed-type payloads
- sparse residency / virtual atlas pages

### Why float first

The engine already has strong float-render-target support in the canonical HDR path and debug sampling. A float-first patch system can be validated without forcing a new integer decode pipeline into the render graph.

## 4. CPU-Side Data Model

The CPU-side implementation should follow the repo’s SoA/data-oriented style.

### Patch topology sidecar

One edge-indexed record per undirected edge:

```cpp
struct HalfedgePatchMeta
{
    uint32_t EdgeIndex;        // stable within the current mesh revision
    uint32_t LayerIndex;       // texture-array layer or atlas page
    uint16_t Resolution;       // square patch size
    uint16_t Flags;            // boundary, dirty, etc.
    uint32_t Face0;            // optional incident face for halfedge 0
    uint32_t Face1;            // optional incident face for halfedge 1
};
```

Suggested sidecar buffers:

- `std::vector<HalfedgePatchMeta>`
- `std::vector<uint32_t>` edge-to-layer indirection
- `std::vector<uint32_t>` patch dirty bits / revision stamps
- optional per-patch diagnostics buffer for normalization/coverage

### Recommended module shape

Add a new geometry module pair:

- `src/Runtime/Geometry/Geometry.HtexPatch.cppm`
- `src/Runtime/Geometry/Geometry.HtexPatch.cpp`

Responsibilities:

- enumerate edge patches from a `Halfedge::Mesh`
- compute resolution buckets
- store intrinsic triangle metadata
- build per-patch upload lists
- expose stable patch queries to graphics/runtime code

## 5. Upload Flow

The intended flow is:

1. Build or update a `Halfedge::Mesh`.
2. Enumerate `EdgeHandle`s and allocate one patch per undirected edge.
3. For each edge, compute a target resolution bucket using a simple area heuristic.
4. Emit an intrinsic-triangle stream from the halfedge connectivity.
5. Upload patch payloads into bucketed texture arrays.
6. Publish bindless texture slots through `TextureSystem`.

### Resolution heuristic

Use the combined area of the two intrinsic triangles adjacent to the edge:

$$
A_e = A(\triangle_h) + A(\triangle_{twin(h)}).
$$

Then choose a texel density target and bucket to a supported size:

$$
R_e = \mathrm{bucket}(c \sqrt{A_e}).
$$

For v1, use a small fixed bucket set such as:

- `8x8`
- `16x16`
- `32x32`
- `64x64`
- `128x128`

## 6. GPU Resource Layout

### Phase 1 resource choice

Use **texture arrays**.

This keeps the first implementation simple:

- one array per resolution bucket
- one layer per patch
- `LayerIndex` selects the edge patch
- `BindlessSlot` selects the bucket array

### Suggested formats

For v1 float payloads:

- `R16G16B16A16_SFLOAT` for color-like patch data
- `R16_SFLOAT` or `R32_SFLOAT` for scalar coverage/diagnostic channels if needed

Recommended packing for the first cut:

- `rgba` = float payload
- `a` = normalization / coverage weight when blending or baking

### GPU-side buffers

Suggested SSBOs:

```cpp
struct HtexHalfedgeGpu
{
    uint32_t next;
    uint32_t prev;
    uint32_t twin;
    uint32_t edge;
    uint32_t vertex;
    uint32_t face;
};

struct HtexPatchGpu
{
    uint32_t storageIndex;
    uint32_t layerIndex;
    uint32_t resolution;
    uint32_t flags;
};
```

The exact packing can be adjusted, but the key point is that the shader should never need pointer chasing or heap allocations — just dense arrays of topology and patch metadata.

## 7. Shader Contract

### Inputs

The shader should receive:

- a halfedge ID or intrinsic triangle ID
- local coordinates / barycentrics
- patch metadata buffers
- bindless texture access for the bucketed arrays

### Core helper

The main mapping helper should resemble:

```cpp
float2 TriangleToPatchUV(uint32_t halfedgeId, float2 localUV, uint32_t twinId)
{
    return (halfedgeId > twinId)
        ? localUV
        : float2(1.0f - localUV.x, 1.0f - localUV.y);
}
```

The shader should normalize the convention used by the intrinsic-triangle authoring path and keep the twin halfedge inside the same square patch.

### Phase-1 write path

For float render targets, prefer a raster/fragment path that writes into the appropriate patch layer.

That means:

- emit intrinsic triangles from the halfedge mesh
- route each triangle to the correct patch layer
- convert triangle-local coordinates to patch UVs
- write float payloads into the texture array layer

This can be implemented as a dedicated bake/preview pass or as a compute writer, depending on the final engine scheduling choice.

### Phase-1 read path

For preview and debug views, sample only the current patch first.

Do **not** force the full Htex seamless 3-fetch neighborhood blend in phase 1 unless the test harness needs it.

Neighborhood filtering can be added later using `Next` and `Prev` traversal, with the twin already sharing the same patch layer.

## 8. Render-Graph Integration

The patch system should integrate as an **optional graph lane**.

Recommended behavior:

- no change to default `SurfacePass` / `LinePass` / `PointPass` behavior
- patch authoring is triggered by an explicit feature flag or editor action
- patch preview can be sampled by `DebugViewPass`
- float patch textures are transient frame resources unless explicitly persisted

### Proposed pass structure

Two reasonable options exist:

1. **Bake/preview pass**
   - writes to patch arrays or patch preview textures
   - intended for editor tooling and diagnostics
2. **Read-only debug pass**
   - visualizes already-authored patches
   - useful for validation and inspection

For the first implementation, favor a **single bake/preview pass** rather than introducing multiple specialized passes.

### Render graph contract

The new pass should be added only when the feature is enabled, so `ValidateCompiledGraph()` does not force the resource into every frame recipe.

## 9. K-Means Label Validation Path

The repo already has a stable k-means property pipeline:

- `Runtime::PointCloudKMeans::PublishResult()` publishes `v:kmeans_label`, `p:kmeans_label`, `v:kmeans_color`, and `p:kmeans_color`
- `Graphics::ColorMapper` converts `glm::vec4` / scalar properties into renderable packed colors
- the editor already uses the color property for visualization
- the Htex patch preview pass caches patch metadata and the last desired atlas signature between stable mesh / k-means revisions so it does not rebuild the edge sidecar every frame

For this patch system, k-means is a good regression fixture because it checks two things:

1. labels remain correctly published into PropertySets
2. float patch payloads can represent the corresponding color output

The phase-1 validation should therefore operate on the **color-mapped float output**, not on a native integer patch texture.

## 10. Boundary Policy

Recommended v1 policy:

- prefer manifold triangle meshes
- allow boundary edges, but treat them as one-sided patches
- skip or log non-manifold topology until a dedicated repair path is introduced
- validate that the halfedge/twin pair exists before assigning symmetric patch ownership

This keeps the initial implementation robust while avoiding the complexity of pathological surface repair inside the patch system itself.

## 11. Testing Strategy

Tests should live in the geometry/runtime test stack and should be narrow and deterministic.

### Geometry tests

Add tests for:

- edge enumeration count vs patch count
- halfedge/twin patch ownership
- boundary one-sided behavior
- intrinsic-triangle to patch-UV mapping
- resolution bucket selection

### Runtime / label tests

Add tests for:

- `Runtime::PointCloudKMeans::PublishResult()` continuing to publish the expected label/color properties
- color-mapped float payload generation for known labels
- patch preview output for a deterministic mesh fixture

### Suggested test file

- `tests/Test_HtexPatch.cpp`

## 12. Phased Rollout

### Phase 1 — Float-only MVP

- edge-indexed patches
- texture arrays by resolution bucket
- float payloads only
- optional preview/debug pass
- k-means label color regression

### Phase 2 — Seamless neighborhood filtering

- add `Next` / `Prev` neighborhood fetches
- add normalization channel handling
- investigate seam/corner consistency

### Phase 3 — Non-float payloads

- integer labels
- IDs
- bitmasks
- mixed payload serialization

These later items are intentionally left as backlog items in `TODO.md` until the data-layout tradeoffs are proven.

## 13. Practical File List for an Initial Implementation

If implementation starts, the most likely files are:

- `docs/architecture/htex-halfedge-patch-system.md` ← this note
- `src/Runtime/Geometry/Geometry.HtexPatch.cppm`
- `src/Runtime/Geometry/Geometry.HtexPatch.cpp`
- `src/Runtime/Geometry/CMakeLists.txt`
- `src/Runtime/Graphics/Graphics.RenderPipeline.cppm`
- `src/Runtime/Graphics/Passes/Graphics.Passes.HalfedgePatch.cppm`
- `src/Runtime/Graphics/Passes/Graphics.Passes.HalfedgePatch.cpp`
- `assets/shaders/halfedge_patch.vert`
- `assets/shaders/halfedge_patch.frag`
- `tests/Test_HtexPatch.cpp`
- `README.md`
- `TODO.md`

## 14. Recommendation

The cleanest path is:

1. **float-only edge patches first**
2. **bucketed texture arrays**
3. **explicit preview/debug pass**
4. **k-means color regression test**
5. **defer integer payloads until the packing contract is proven**

That matches IntrinsicEngine’s current strengths: halfedge topology, PropertySet-backed data, and bindless texture management.

