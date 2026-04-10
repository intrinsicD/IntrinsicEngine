# Modular GPU-Driven Rendering Pipeline Plan (Code-Aware Reuse + Gap Audit)

## 0) Scope

This plan is a **code-aware redesign map**: what already exists in IntrinsicEngine, what should be reused as-is, what should be refactored, and what must be added to reach a truly modular GPU-driven pipeline.

Target: **< 2ms CPU frame prep**, GPU-first visibility, immutable frame packets, explicit ownership boundaries.

---

## 1) Mathematical & Architectural Analysis

### 1.1 Visibility invariant

For each renderable instance $i$ with local bounding sphere $(c_i, r_i)$ and world matrix $M_i$, define:

$$
\hat{c}_i = M_i\begin{bmatrix}c_i\\1\end{bmatrix},\qquad
\hat{r}_i = r_i \cdot \max(\|M_i^{(0)}\|_2,\|M_i^{(1)}\|_2,\|M_i^{(2)}\|_2).
$$

Frustum acceptance (plane-normal form):

$$
\forall j\in\{1..6\}:\quad n_j^\top \hat{c}_i + d_j \ge -\hat{r}_i.
$$

This is already mirrored in CPU tests and compute shaders; keep it as the canonical culling contract.

### 1.2 Three-graph ownership alignment

- **CPU Task Graph:** extraction, dirty-resolution, packet assembly scheduling.
- **GPU Frame Graph:** cull/compact/indirect + pass execution.
- **Async Streaming Graph:** geometry/material streaming + arena patch updates.

### 1.3 Complexity targets

- Extraction over changed entities only: $O(k)$ time, $O(k)$ transient memory.
- Broad-phase culling + compaction: $O(n)$ on GPU.
- Packet building: $O(v)$ where $v$ is visible instance count.

---

## 2) Current-state inventory (what you already have and should reuse)

## 2.1 Reuse as foundation (keep)

1. **Retained GPU scene instance state exists**
   - `Graphics::GPUScene` has scene + bounds SSBOs, slot allocator, queued update batching, and compute scatter update (`scene_update.comp`).
2. **GPU-driven multi-geometry surface culling exists**
   - `SurfacePass` already builds dense geometry routing tables, dispatches `instance_cull_multigeo.comp`, and emits packed indirect/visibility buffers.
3. **Three-pass rendering topology exists**
   - Surface/Line/Point pass separation and packet consumption are real and test-covered.
4. **Material buffer path exists**
   - `MaterialRegistry` has a GPU SSBO + revision tracking and explicit sync (`SyncGpuBuffer()`).
5. **Lifecycle + ECS integration exists**
   - Mesh/Graph/PointCloud lifecycle systems allocate GPUScene slots and populate per-pass components.
6. **Extraction immutability baseline exists**
   - `Runtime.RenderExtraction` builds immutable per-frame `RenderWorld`; `RenderOrchestrator` passes spans into `BuildGraphInput`.
7. **Culling correctness tests exist**
   - CPU reference tests mirror shader behavior and include sentinel/degenerate bounds handling.

## 2.2 Reuse with refactor (do not rewrite from scratch)

1. **GPUScene slot allocator** → keep API shape, add generational handles and explicit free semantics.
2. **Lifecycle systems** → keep dirty workflows + pass population, move them to writing render-view handles instead of pass-specific duplication over time.
3. **SurfacePass culling kernels** → keep shader/math, extract into dedicated `Graphics.Visibility` module.
4. **RenderExtraction packet model** → keep immutable snapshot pattern, split into domain packets vs pass packets.
5. **MaterialRegistry revisions** → keep revision logic, move descriptor epoching + per-frame consistency contract into render core.

## 2.3 Technical debt hotspots (must change)

1. **Topology storage is not yet truly unified**
   - Mesh/graph/point data still mostly rides through geometry-specific buffers in `GeometryPool`; single global vertex/index arena is not yet authoritative.
2. **Visibility ownership is split**
   - Surface is GPU-culled, Line/Point are currently CPU-culled.
3. **ECS stores pass-shaped data**
   - `Surface::Component`, `Line::Component`, `Point::Component` are practical now, but long-term modularity needs canonical render views + pass adapters.
4. **No explicit compaction schema authority**
   - Indirect/visibility formats are pass-local, not centrally versioned.
5. **No arena defragmentation protocol**
   - Required once single global scene buffers become authoritative.

---

## 3) Proposed modular boundaries (hard contracts)

## 3.1 `ECS.RenderViews` (authoritative scene-facing render intent)

ECS components should expose only stable handles/views:

- `RenderGeometryView { StrongHandle<GpuGeometrySlice> }`
- `RenderMaterialView { StrongHandle<GpuMaterialRecord> }`
- `RenderInstanceView { StrongHandle<GpuInstanceRecord>, VisibilityMask }`

No Vulkan handles or descriptor ownership in ECS components.

## 3.2 `Graphics.GpuScene` (authoritative GPU data ownership)

Owns:

- `GpuSceneVertexArena` (single buffer, BDA)
- `GpuSceneIndexArena` (single buffer, BDA)
- `InstanceSSBO`, `BoundsSSBO`, `TransformSSBO`, `MaterialSSBO`, `DrawMetaSSBO`
- handle tables + generation counters + relocation remap table

Exports stable slice/view handles only.

## 3.3 `Graphics.Visibility` (single visibility authority)

Owns:

- Frustum cull compute
- (optional) Hi-Z occlusion cull
- Visible list compaction
- Indirect argument writing
- counters + reason codes

All passes consume visibility output; passes must never run their own cull policy.

## 3.4 `Graphics.RenderPackets` (immutable packet authority)

Builds pass-agnostic packet headers and typed packet slices:

- `SurfacePacket`
- `LinePacket`
- `PointPacket`

Packets reference GPUScene handles and visibility ranges, not raw ECS components.

## 3.5 `Graphics.Passes.*` (execution only)

Consume packets + global descriptors, issue draw/dispatch only.

---

## 4) Data layout design (SoA-first)

## 4.1 Canonical hot-path SoA tables

- `InstanceTable`: model matrix, material id, geometry id, flags.
- `BoundsTable`: sphere center/radius (+ optional cone/obb later).
- `GeometryTable`: vertex/index offsets, counts, topology kind.
- `MaterialTable`: packed PBR factors + bindless ids.

All indexable by stable handles with generation checks.

## 4.2 Arena model

Single large topology storage:

- vertex arena for all position/normal/tangent/custom streams (streamed by metadata)
- index arena for all primitive index streams (tri/line/point index indirection)

This matches your “one vertex + one index GPU scene” objective while preserving domain-specific interpretation through metadata.

## 4.3 ACTIVE.md visualization sketch integration (mandatory contract)

Adopt the explicit rule from your sketch:

- **Topology and attributes are separated.**
- **Positions remain in the shared topology vertex arena.**
- **All other per-primitive attributes are stored in indexable buffer-backed SoA tables** (SSBO tables), keyed by primitive/domain id.

Concrete attribute-table policy:

1. `VertexAttrTable` (normals, colors, UV chains, labels, custom scalar/vector channels).
2. `EdgeAttrTable` (edge colors/weights/vector magnitudes/domain flags).
3. `FaceAttrTable` (flat normals, face colors, material override indices).
4. `PointAttrTable` (point radii, normals/covariance for splats/surfels, class labels).
5. `VectorAttrTable` (vector color, scale, quality/confidence).

Each table is:

- a BufferManager-owned SSBO allocation,
- referenced by a stable handle (`RHI::BufferHandle`/engine strong handle wrapper),
- index-addressable in shader via base offset + primitive index.

This keeps descriptors stable and avoids pass-specific duplicate buffers.

## 4.4 ECS contract for BufferManager-backed attribute views

ECS should store **handles and views only**, never raw Vulkan objects:

- `RenderTopologyView { StrongHandle<GpuGeometrySlice> }`
- `RenderAttributeViews {`
  - `StrongHandle<GpuAttrSlice> VertexAttrs;`
  - `StrongHandle<GpuAttrSlice> EdgeAttrs;`
  - `StrongHandle<GpuAttrSlice> FaceAttrs;`
  - `StrongHandle<GpuAttrSlice> PointAttrs;`
  - `StrongHandle<GpuAttrSlice> VectorAttrs;`
  - `}`
- `RenderInstanceView { StrongHandle<GpuInstanceRecord>, VisibilityMask }`

All underlying memory is allocated/freed/defragmented by BufferManager + GpuScene allocator layers.
Lifecycle systems only update these handles when topology/attributes are rebuilt.

## 4.5 Visualization capability contract (algorithm-output complete)

Visualization must be able to consume **all algorithm outputs** from the DoA/SoA indexed attribute tables and override standard textured shading when toggled.

### Supported scalar fields (from indexed attribute tables)

- Types: `bool`, signed/unsigned integer, `float`, `double`, and unit-scalars.
- Domains: vertex / edge / face / point / vector.
- Mapping: scalar -> colormap LUT via normalized range
  $$
  t = \mathrm{saturate}\left(\frac{x - x_{\min}}{x_{\max} - x_{\min} + \varepsilon}\right)
  $$
  with robust fallback when $(x_{\max} - x_{\min}) \approx 0$.

### Supported color fields

- **3D color** (`rgb`) direct.
- **4D color** (`rgba`) with alpha compositing in visualization mode.
- Domain-aware interpolation:
  - vertex domain interpolated on triangles/lines/points as applicable,
  - face/edge domain flat per primitive,
  - point/vector domain direct fetch.

### Supported vector-field visualization

- Uniform color + uniform scale.
- Uniform color + varying scale (per-vector magnitude or scalar field).
- Per-vector color (`rgb`/`rgba`) + uniform scale.
- Per-vector color (`rgb`/`rgba`) + varying scale.
- Optional direction encoding (hue by azimuth/elevation) from indexed vector tables.

Vector glyph endpoints:
$$
p_1 = p_0 + s_i \, v_i,\quad s_i \in \{s_{uniform}, s_{attribute}(i)\}.
$$

### Visualization override semantics

- Add `VisualizationMode` toggle per render domain:
  - `Off`: standard texture/material shading path.
  - `On`: visualization path overrides texture mapping/material response.
- Visualization path still uses same topology pass ownership (`SurfacePass`, `LinePass`, `PointPass`), but selects visualization shader branch/pipeline permutation.
- Texture mapping is bypassed when `VisualizationMode=On` unless explicitly requested by a hybrid mode.

### Future-ready deformation hook

- Reserve `DisplacementAttr` channel in vertex-domain attribute table.
- Optional displacement in visualization mode:
  $$
  p'_i = p_i + \alpha_i \, n_i
  $$
  where $\alpha_i$ can come from scalar fields or explicit displacement attributes.
- Degenerate cases (missing normals / NaN displacement / extreme magnitude) clamp or fallback to original positions.

---

## 5) What to change exactly (implementation delta from current code)

## 5.1 Phase A — Boundary extraction (minimal disruption)

1. Create `Graphics.Visibility` module.
   - Move culling dispatch/build logic out of `SurfacePass` into this module.
   - Keep existing shader binaries and push-constant structure initially.
2. Add `ECS.RenderViews` components.
   - Keep existing `Surface/Line/Point` components alive during transition.
3. Add `Graphics.RenderPackets` module.
   - Build immutable pass packets from `RenderWorld` + visibility outputs.

## 5.2 Phase B — Unified GPU scene topology authority

1. Introduce global vertex/index arenas in `Graphics.GpuScene`.
2. Add slice allocator + free list + relocation remap.
3. Convert lifecycle upload paths:
   - Mesh/Graph/PointCloud lifecycle systems upload positions + topology into global arenas.
   - All non-position attributes are uploaded into BufferManager-owned indexed attribute tables and exposed as `GpuAttrSlice` handles.
4. Keep compatibility adapter to provide existing `GeometryHandle` while migration is in progress.

## 5.3 Phase C — Full GPU visibility authority

1. Replace CPU cull for line/point with `Graphics.Visibility` outputs.
2. Emit per-pass indirect buffers from one compaction framework.
3. Add reason-code counters:
   - frustum reject
   - zero/invalid bounds reject
   - optional occlusion reject
4. Add visualization override path:
   - `VisualizationMode` toggles per pass/domain.
   - shader permutations for scalar colormap, rgb/rgba attributes, and vector-field coloring/scaling.
   - explicit precedence rule: visualization mode overrides standard texture mapping.

## 5.4 Phase D — Robustness and streaming

1. Add descriptor epoching for N-frames-in-flight consistency.
2. Add async streaming patch queue:
   - staged uploads into free arena ranges
   - atomic metadata commit per frame boundary
3. Add defragmentation pass + remap fixups.

---

## 6) “What am I missing?” — explicit checklist

Critical pieces still needed for true modularity:

1. **Strong handle generation checks** for all GPU scene records.
2. **Relocation-safe arena protocol** with remap table and deferred reclaim.
3. **Descriptor epoch contract** across frames-in-flight.
4. **Centralized indirect schema** (single versioned definition).
5. **Visibility diagnostics API** for debug UI and telemetry.
6. **Degenerate bounds policy** (radius <= 0, NaN transforms, invalid scales).
7. **Line/point GPU culling parity** with surface path.
8. **LOD policy** (distance + projected-size + optional error metric).
9. **Streaming budget policy** (per-frame upload byte caps).
10. **Validation harness** CPU/GPU visibility parity on deterministic scenes.
11. **Ownership linting** (compile-time module boundaries + runtime asserts).
12. **Pass isolation** (no ECS queries inside pass execution).
13. **Attribute-table ABI** for vertex/edge/face/point/vector domains (versioned layouts + shader compatibility checks).
14. **Visualization permutation control** (bounded shader variant strategy + fallback when attributes missing).

---

## 7) Workstream plan (subagent-style decomposition)

To emulate expert subagents, split the effort into independent tracks with strict deliverables.

### Track A — Visibility & Indirect Authority
- Extract culling logic from `SurfacePass` to `Graphics.Visibility`.
- Unify indirect output schema for all passes.
- Deliver parity tests + telemetry counters.

### Track B — GPU Scene Unification
- Implement global vertex/index arenas + slice handles.
- Port lifecycle upload paths to arena-backed slices.
- Deliver relocation-safe remap layer.

### Track C — ECS View Contracts
- Add `Render*View` ECS components.
- Make lifecycle systems publish views, not pass-local render state.
- Keep backward compatibility adapter until passes are switched.

### Track D — Render Packetization
- Build packet builder module consuming extraction snapshot + visibility outputs.
- Freeze immutable packet schema and lifetime contract.

### Track E — Validation & Telemetry
- CPU/GPU cull parity tests.
- stale-handle guard tests.
- timeline/sync hazard tests.
- GPU counters + frame-time markers.

---

## 8) Verification gates

## 8.1 Correctness

- CPU/GPU visible set parity on canonical scenes.
- No stale-handle dereference under slot reuse.
- Deterministic packet contents for same extracted snapshot.

## 8.2 Performance

- CPU render preparation P95 under target budget.
- Visibility dispatch + compaction costs stable with instance growth.
- Minimal per-frame allocations in hot path.

## 8.3 Robustness

- Degenerate geometry does not crash or explode bounds.
- Missing normals/radii/covariance degrades to deterministic fallback modes.
- Descriptor + sync validation clean under Vulkan Sync2.

---

## 9) Practical migration order (recommended)

1. **Extract Visibility module first** (highest modular ROI, low risk).
2. **Introduce render-view ECS components** (freeze contracts early).
3. **Unify topology storage into global arenas** (largest change, done once contracts are stable).
4. **Switch Line/Point to GPU culling + indirect**.
5. **Enable streaming + defrag + LOD**.

This order minimizes regressions while converging to your desired architecture.

---

## 10) Final boundary litmus test

If the engine is modular, each answer is singular:

- topology ownership → `Graphics.GpuScene`
- visibility truth → `Graphics.Visibility`
- material payload truth → `Graphics.MaterialRegistry` / material table
- packet construction → `Graphics.RenderPackets`
- pass execution → `Graphics.Passes.*`
- frame ordering → `Runtime.RenderOrchestrator`
- world intent/data → ECS

If any answer has multiple owners, modularity is still broken.

---

## 11) C++23 module skeletons for the new boundaries

These are minimal reference shapes to lock the API direction.

```cpp
// Graphics.Visibility.cppm
module;
#include <expected>
#include <span>
export module Graphics.Visibility;

import Graphics.RenderPipeline;
import Graphics.GPUScene;

export namespace Graphics::Visibility
{
    struct VisibilityInput
    {
        const GPUScene* Scene = nullptr;
        uint32_t FrameIndex = 0;
    };

    struct VisibilityOutput
    {
        // Handles/ranges to compacted visible ids + indirect argument buffers.
        uint32_t VisibleCount = 0;
    };

    [[nodiscard]] std::expected<VisibilityOutput, const char*> BuildAndDispatch(VisibilityInput input);
}
```

```cpp
// Graphics.Visibility.cpp
module Graphics.Visibility;
import Core.Profiling;

namespace Graphics::Visibility
{
    std::expected<VisibilityOutput, const char*> BuildAndDispatch(VisibilityInput input)
    {
        if (!input.Scene)
            return std::unexpected("VisibilityInput.Scene was null");

        // Dispatch frustum/occlusion kernels, compact visible ids, write indirect args.
        VisibilityOutput out{};
        return out;
    }
}
```

```cpp
// ECS.RenderViews.cppm
module;
#include <cstdint>
export module ECS.RenderViews;

import Core.Handle;

export namespace ECS::RenderViews
{
    struct RenderTopologyView
    {
        Core::StrongHandle<uint64_t> GeometrySlice{};
    };

    struct RenderAttributeViews
    {
        Core::StrongHandle<uint64_t> VertexAttrs{};
        Core::StrongHandle<uint64_t> EdgeAttrs{};
        Core::StrongHandle<uint64_t> FaceAttrs{};
        Core::StrongHandle<uint64_t> PointAttrs{};
        Core::StrongHandle<uint64_t> VectorAttrs{};
    };
}
```
