# Modular GPU-Driven Rendering Pipeline Plan (Code-Aware Reuse + Gap Audit)

## 0) Scope

This plan is a **code-aware redesign map**: what already exists in IntrinsicEngine, what should be reused as-is, what should be refactored, and what must be added to reach a truly modular GPU-driven pipeline.

Target: **< 2 ms CPU frame prep**, GPU-first visibility, immutable frame packets, explicit ownership boundaries.

**Performance target methodology:** "CPU frame prep" covers extraction + culling + packet build (from `ExtractRenderWorld()` entry to `BuildGraph()` entry). Measured via `Core::Profiling` markers as P95 wall-clock across 1 000 frames on a mid-range desktop (8-core, discrete GPU) with a 10 K-entity mixed scene (meshes + graphs + point clouds). The 2 ms budget excludes GPU command recording and submission.

### Cross-reference to tasks/backlog/legacy-todo.md

This plan overlaps with and refines several tasks/backlog/legacy-todo.md work items. The mapping is:

| tasks/backlog/legacy-todo.md Item | Plan Phase | Relationship |
|---|---|---|
| **B1** (Render Prep as Job-Scheduled Work) | Phase A (§5.1) | Plan Phase A extracts visibility + packets; B1 schedules them as jobs. Phase A is a prerequisite. |
| **B2** (GPU Submission Hardening) | Phase C (§5.3) | GPU-driven indirect submission depends on explicit wait→acquire→record→submit rhythm from B2. |
| **B3** (Frame-Context Transient Ownership) | Phase D (§5.4) | Descriptor epoching and per-frame arena ownership align with B3's transient ownership migration. |
| **B4a/B4b** (Task Graph + Parallelization) | Phase A–C | Visibility dispatch and packet build are the first job-graph candidates from B4a. |
| **B5** (Queue Model + Resource Lifetime) | Phase D (§5.4) | Timeline-based resource retirement feeds the streaming + defrag budget policy. |
| **C4** (Visibility System Improvements) | Phase A + C (§5.1, §5.3) | This plan *is* the concrete implementation plan for C4's design-only items. |
| **C9** (GPU-Driven Indirect Rendering) | Phase C (§5.3) | C9's description is partially stale — GPU-driven surface culling already runs at runtime via `SurfacePass` Stage 3 (`instance_cull_multigeo.comp`). The remaining C9 gap is extending GPU culling to Line/Point passes, which this plan's Phase C addresses. |

When this plan's phases complete, C4 and C9 should be closed in tasks/backlog/legacy-todo.md. B1–B5 remain independent frame-pipeline concerns that this plan depends on but does not subsume.

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

- **CPU Task Graph:** extraction, dirty-resolution, packet assembly scheduling. Owns the `RenderWorld` snapshot lifetime. Runs before GPU work begins; produces immutable inputs for the GPU frame graph.
- **GPU Frame Graph:** cull/compact/indirect + pass execution. Consumes `BuildGraphInput` (spans into `RenderWorld`). Synchronization boundary: the CPU task graph must complete before `BuildGraph()` begins.
- **Async Streaming Graph:** geometry/material streaming + arena patch updates. Runs concurrently with both other graphs on the transfer queue. Commits metadata atomically at frame boundaries (only after `TransferToken::IsCompleted()`). Must never modify data referenced by in-flight GPU frames.

Interaction points: CPU task graph feeds GPU frame graph via `BuildGraphInput`. Async streaming graph feeds CPU task graph via arena slice table updates visible at the next extraction. No direct streaming→GPU frame graph path exists (all data flows through extraction).

### 1.3 Complexity targets

- Extraction over changed entities only: $O(k)$ time, $O(k)$ transient memory.
- Broad-phase culling + compaction: $O(n)$ on GPU.
- Packet building: $O(v)$ where $v$ is visible instance count.

---

## 2) Current-state inventory (what you already have and should reuse)

### 2.1 Reuse as foundation (keep)

1. **Retained GPU scene instance state exists**
   - `Graphics::GPUScene` has scene + bounds SSBOs, slot allocator, queued update batching, and compute scatter update (`scene_update.comp`).
2. **GPU-driven multi-geometry surface culling is live at runtime**
   - `SurfacePass` Stage 3 (`m_EnableGpuCulling = true` by default) builds dense geometry routing tables, dispatches `instance_cull_multigeo.comp`, and consumes the output via `vkCmdDrawIndexedIndirectCount`. This is **not** dormant infrastructure — it is the active surface draw path. (Note: tasks/backlog/legacy-todo.md C9 still describes this as unwired; that description is stale and should be updated.)
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

### 2.2 Reuse with refactor (do not rewrite from scratch)

1. **GPUScene slot allocator** → keep API shape, add explicit free semantics and deferred reclaim. Note: `GeometryPool` already uses `Core::ResourcePool<GeometryGpuData, GeometryHandle, 3>` with generational `StrongHandle` validation (Index + Generation) and 3-frame retirement. The GPUScene *slot* allocator (simple free-list of `uint32_t`) is the one lacking generation checks — it must gain generational guards to match `GeometryPool`'s safety.
2. **Lifecycle systems** → keep dirty workflows + pass population, move them to writing render-view handles instead of pass-specific duplication over time.
3. **SurfacePass culling kernels** → keep shader/math, extract into dedicated `Graphics.Visibility` module.
4. **RenderExtraction packet model** → keep immutable snapshot pattern, split into domain packets vs pass packets.
5. **MaterialRegistry revisions** → keep revision logic, move descriptor epoching + per-frame consistency contract into render core.

### 2.3 Technical debt hotspots (must change)

1. **Topology storage is not yet truly unified**
   - Mesh/graph/point data rides through per-entity `GeometryGpuData` in `GeometryPool` (ref-counted `BufferLease` pairs). This is actually a strength for flexible geometry sharing (`ReuseVertexBuffersFrom`), but prevents global compaction and makes cross-entity deduplication impossible.
   - **Trade-off:** A single global arena enables defragmentation and reduces descriptor/BDA overhead, but loses the simplicity of per-entity buffer isolation. The migration must preserve `ReuseVertexBuffersFrom` semantics within the arena (shared vertex range, distinct index range).
2. **Visibility ownership is split**
   - Surface is GPU-culled (`instance_cull_multigeo.comp` with per-geometry routing tables, atomic draw-count compaction, and visibility remap buffers). Line/Point are CPU-culled (`CullDrawPackets()` producing `CulledDrawList` index vectors).
   - The GPU path produces `VkDrawIndexedIndirectCommand` + visibility remap per geometry. The CPU path produces `std::vector<uint32_t>` indices into span-based draw packets. These two output schemas must converge.
3. **ECS render components are well-structured but pass-shaped**
   - `Surface::Component`, `Line::Component`, `Point::Component` are pure data carriers (zero Vulkan coupling — verified). They hold `GeometryHandle`, cached colors, visibility flags, and domain hints. Long-term modularity would benefit from canonical render views + pass adapters, but the components themselves are not the bottleneck — the bottleneck is that extraction (`Runtime.RenderExtraction`) rebuilds draw packet vectors every frame from ECS queries rather than incrementally.
4. **No explicit compaction schema authority**
   - Indirect draw format (`DrawIndexedIndirect` + visibility remap + per-geometry draw counter) is defined only in SurfacePass shaders and C++ code. No centrally versioned definition exists.
5. **No arena defragmentation protocol**
   - `GeometryPool` free-list reuses slots but underlying GPU buffers only grow (`m_NextSlot` monotonically increases). Required once single global scene buffers become authoritative.
6. **GPUScene slot allocator lacks generation checks**
   - `GPUScene::AllocateSlot()` returns raw `uint32_t` with no generation counter. A freed-then-reallocated slot could be referenced by a stale ECS component. The `GeometryPool` already has generational `StrongHandle` validation — `GPUScene` does not.

### 2.4 Existing infrastructure to leverage (not mentioned in 2.1/2.2)

The following systems are already production-ready and must be accounted for in the migration plan:

1. **`Core::StrongHandle<Tag>` + `Core::ResourcePool<T, Handle, RetirementFrames>`**
   - Generational handles with compile-time type safety (tag-based discrimination).
   - `ResourcePool` provides deferred deletion with configurable retirement frames.
   - `GeometryPool` already uses `RetirementFrames=3` matching `MAX_FRAMES_IN_FLIGHT`.
   - **Risk:** `ResourcePool` default `RetirementFrames=2` does not match `MAX_FRAMES_IN_FLIGHT=3`. All new pool instantiations must explicitly set `RetirementFrames=3`.

2. **`RHI::TransferManager` + `RHI::StagingBelt`**
   - Timeline semaphore-based async GPU transfer with atomic ticket system.
   - `StagingBelt` is a persistent host-visible ring buffer with Retire/GarbageCollect.
   - Batched uploads via `BeginUploadBatch`/`EnqueueUploadBuffer`/`EndUploadBatch`.
   - **Constraint:** StagingBelt capacity is fixed at construction (no dynamic growth). The streaming budget policy (Phase D) must account for this — either size the belt conservatively or add a growth/fallback path.

3. **`RHI::BufferManager` + `RHI::BufferLease`**
   - Reference-counted buffer ownership via Lease pattern (move-only, RAII release).
   - `GetDeviceAddress()` for BDA. `IsHostVisible()` disambiguates upload mode.
   - **Constraint:** No suballocation — each buffer is monolithic. The unified arena (Phase B) must replace this model for topology storage while preserving the Lease pattern for standalone buffers.

4. **Vulkan Sync2 barrier model**
   - `vkCmdPipelineBarrier2` with `VK_PIPELINE_STAGE_2_*` / `VK_ACCESS_2_*` throughout.
   - RenderGraph automatically generates barriers from resource usage DAG.
   - This is the correct foundation for arena ownership transfers and visibility dispatch barriers.

5. **Descriptor set layout allocation (sets 0–3 spoken for)**
   - Set 0: Global camera + lighting UBO (`CameraBufferObject`).
   - Set 1: Per-pass SSBO (line buffer, point buffer, etc.).
   - Set 2: Instance SSBO (GPU-driven surface path).
   - Set 3: Material SSBO (`MaterialRegistry`).
   - **Constraint:** New attribute table SSBOs need descriptor set slots. Options: extend set 3, add set 4, or use BDA (no descriptor needed). BDA is preferred for consistency with existing vertex/index access patterns.

6. **`PersistentDescriptorPool` vs `DescriptorAllocator`**
   - Persistent pools for long-lived sets (instance, cull, compute). Transient allocator reset per-frame.
   - SurfacePass already maintains per-frame descriptor set arrays (`m_InstanceSet[FRAMES]`).
   - No epoch/generation system on descriptor sets themselves — this is a gap for Phase D.

---

## 3) Proposed modular boundaries (hard contracts)

### 3.1 `ECS.RenderViews` (authoritative scene-facing render intent)

ECS components should expose only stable handles/views:

- `RenderGeometryView { StrongHandle<GpuGeometrySlice> }`
- `RenderMaterialView { StrongHandle<GpuMaterialRecord> }`
- `RenderInstanceView { StrongHandle<GpuInstanceRecord>, VisibilityMask }`

No Vulkan handles or descriptor ownership in ECS components.

### 3.2 `Graphics.GpuScene` (authoritative GPU data ownership)

Owns:

- `GpuSceneVertexArena` (single buffer, BDA)
- `GpuSceneIndexArena` (single buffer, BDA)
- `InstanceSSBO`, `BoundsSSBO`, `TransformSSBO`, `MaterialSSBO`, `DrawMetaSSBO`
- handle tables + generation counters + relocation remap table

Exports stable slice/view handles only.

### 3.3 `Graphics.Visibility` (single visibility authority)

Owns:

- Frustum cull compute
- (optional) Hi-Z occlusion cull
- Visible list compaction
- Indirect argument writing
- counters + reason codes

All passes consume visibility output; passes must never run their own cull policy.

### 3.4 `Graphics.RenderPackets` (immutable packet authority)

Builds pass-agnostic packet headers and typed packet slices:

- `SurfacePacket`
- `LinePacket`
- `PointPacket`

Packets reference GPUScene handles and visibility ranges, not raw ECS components.

### 3.5 `Graphics.Passes.*` (execution only)

Consume packets + global descriptors, issue draw/dispatch only.

---

## 4) Data layout design (SoA-first)

### 4.1 Canonical hot-path SoA tables

- `InstanceTable`: model matrix, material id, geometry id, flags.
- `BoundsTable`: sphere center/radius (+ optional cone/obb later).
- `GeometryTable`: vertex/index offsets, counts, topology kind.
- `MaterialTable`: packed PBR factors + bindless ids.

All indexable by stable handles with generation checks.

### 4.2 Arena model

Single large topology storage:

- vertex arena for all position/normal/tangent/custom streams (streamed by metadata)
- index arena for all primitive index streams (tri/line/point index indirection)

This matches your “one vertex + one index GPU scene” objective while preserving domain-specific interpretation through metadata.

### 4.3 Topology/attribute separation contract

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
- index-addressable in shader via base offset + primitive index,
- **lazily allocated per entity type** — a mesh entity allocates VertexAttr/EdgeAttr/FaceAttr but not PointAttr/VectorAttr; a point cloud entity allocates PointAttr only. Tables are not globally persistent empty buffers.

This keeps descriptors stable and avoids pass-specific duplicate buffers.

### 4.4 ECS contract for BufferManager-backed attribute views

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

### 4.5 Visualization capability contract (future requirements)

*This section captures requirements for the visualization system, not an implementation plan. The concrete implementation path is Phase C item 6 (§5.3). The existing `VisualizationConfig` + `ColorSource` system (per-entity, property-driven, with colormap LUT) provides the CPU-side foundation; Phase C extends it to GPU-driven attribute table reads.*

Visualization must be able to consume **all algorithm outputs** from the SoA indexed attribute tables and override standard textured shading when toggled.

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

### 5.1 Phase A — Boundary extraction (minimal disruption)

1. Create `Graphics.Visibility` module.
   - **Do not move code out of `SurfacePass`.** Instead, introduce `Graphics.Visibility` as a new module that SurfacePass *calls into*. The SurfacePass becomes a thin adapter that delegates to `Visibility::DispatchFrustumCull()`.
   - Consolidate: `CullDrawPackets()` (from `Graphics.RenderPipeline`) and the compute dispatch in SurfacePass share the same frustum math. The `Visibility` module unifies the frustum plane extraction and sphere test.
   - Keep existing shader binaries (`instance_cull_multigeo.comp`, `instance_cull.comp`) and push-constant structure initially.
   - Keep existing per-geometry routing table construction in SurfacePass — Phase C will generalize it.
2. Add `ECS.RenderViews` components.
   - Keep existing `Surface/Line/Point` components alive during transition. These are pure data carriers (no Vulkan coupling) so dual-path maintenance cost is low.
   - `RenderViews` components add handle indirection; existing components remain the data source during migration.
3. Add `Graphics.RenderPackets` module.
   - Extract `SurfaceDrawPacket`, `LineDrawPacket`, `PointDrawPacket` (and picking variants) from `Graphics.RenderPipeline.cppm` into a dedicated module.
   - Build immutable pass packets from `RenderWorld` + visibility outputs.
   - **Note:** Current draw packets contain `std::vector` fields (e.g. `EdgeColors`, `Colors`, `Radii` in Line/Point packets). These must be replaced with `std::span` or arena-allocated ranges for true immutability.

### 5.2 Phase B — Unified GPU scene topology authority

1. Introduce global vertex/index arenas in `Graphics.GpuScene`.
2. Add slice allocator + free list + relocation remap.
3. **Expand GPUScene slot allocation to Line/Point entities.** Currently only Surface instances have GPUScene slots with `GpuInstanceData` + bounding spheres. Line and Point entities need slots too — Phase C cannot GPU-cull them without this. Add `GeometryID` to GPUScene entries for all entity types, not just surfaces.
4. Convert lifecycle upload paths:
   - Mesh/Graph/PointCloud lifecycle systems upload positions + topology into global arenas.
   - All non-position attributes are uploaded into BufferManager-owned indexed attribute tables and exposed as `GpuAttrSlice` handles.
5. Keep compatibility adapter to provide existing `GeometryHandle` while migration is in progress.

**Decision gate:** Before committing to the global arena, measure fragmentation in real editor workloads (load/unload 50+ assets in sequence). If fragmentation stays below 20% of peak allocation, the per-entity buffer model may be sufficient with only the GPUScene slot + generational handle improvements. The arena adds significant complexity (defragmentation, BDA pointer fixups) and should only proceed if fragmentation is a measured problem.

### 5.3 Phase C — Full GPU visibility authority

1. Replace CPU cull for line/point with `Graphics.Visibility` outputs.
   - Line/Point draw packets must carry `GeometryID` (currently only Surface instances do). Add `GeometryID` to `GPUScene` entries for line/point entities, or introduce a parallel lightweight visibility buffer for non-surface primitives.
2. Emit per-pass indirect buffers from one compaction framework.
   - Centralize the indirect schema: `DrawIndexedIndirectCommand` + `VisibilityRemap[]` + `DrawCount` per geometry, as currently implemented in `instance_cull_multigeo.comp`.
   - Line/Point indirect commands use `VkDrawIndirectCommand` (non-indexed for lines, or indexed where applicable). Define both schemas in `Graphics.Visibility`.
3. Add reason-code counters:
   - frustum reject
   - zero/invalid bounds reject
   - optional occlusion reject
4. **Transient content exclusion.**
   - `DebugDraw` content (lines, points, triangles) must bypass GPU culling. These are per-frame transient uploads with no GPUScene slot or bounding sphere.
   - Add a `TransientPacket` type (or a `NeverCull` flag on existing packets) that the visibility system skips and passes issue unconditionally.
5. **Pick pipeline integration.**
   - Pick passes (`pick_mesh`, `pick_line`, `pick_point`) must either use an unculled draw path or dispatch their own visibility with an inflated/full frustum. GPU-culled entities that are frustum-rejected but mouse-hovered become unpickable otherwise.
6. Add visualization override path:
   - `VisualizationMode` toggles per pass/domain. The existing `VisualizationConfig` + `ColorSource` system (per-entity, property-driven, with colormap LUT) provides the CPU-side data. The GPU path needs specialization constant-based shader variants.
   - Use specialization constants (not separate SPIR-V binaries) for mode selection. Limit to at most 4 variants per domain × 3 domains = 12 pipeline permutations per pass.
   - Explicit precedence rule: visualization mode overrides standard texture mapping (consistent with existing `VisualizationConfig` priority documented in CLAUDE.md).

### 5.4 Phase D — Robustness and streaming

1. Add descriptor epoching and GPUScene slot retirement for N-frames-in-flight consistency.
   - `PersistentDescriptorPool` already provides persistent sets. What's missing is a generation token per descriptor set that gates stale-set usage. Wrap `VkDescriptorSet` in a `DescriptorSetHandle` with epoch validation.
   - Coordinate with `GPUScene::kMaxFramesInFlight` (3) and `ResourcePool::RetirementFrames` (must be 3 everywhere).
   - **GPUScene slot reuse hazard:** When a slot is freed and reallocated between frames, frame N-2 (still in flight) may reference old data at that slot index. The GPU has no generation check. **Chosen approach:** Apply the same `RetirementFrames=3` deferred-reclaim policy that `GeometryPool` uses — a freed GPUScene slot enters a retirement queue and cannot be reallocated until 3 frames have elapsed (matching `MAX_FRAMES_IN_FLIGHT`). This avoids per-frame shadow copies of the instance SSBO and avoids versioned indirect buffers, at the cost of slightly delayed slot reuse. Under steady-state editor churn this is acceptable; burst load/unload cycles may temporarily exhaust the slot pool, which should log a warning and defer the new allocation rather than crash.
2. Add async streaming patch queue:
   - Build on existing `RHI::TransferManager` (timeline semaphore-based) and `RHI::StagingBelt` (ring buffer).
   - Staged uploads into free arena ranges; use `TransferToken` to track completion.
   - Atomic metadata commit per frame boundary: update arena slice table only after `TransferToken::IsCompleted()` returns true.
   - **Budget policy:** `StagingBelt` capacity is fixed at construction. Either (a) size it for peak burst (e.g. 64MB), (b) add a growth path via fallback staging buffers, or (c) throttle upload volume per frame and queue excess for next frame.
3. Add defragmentation pass + remap fixups.
   - Arena free-list must coalesce adjacent free ranges (Phase B baseline).
   - Full defrag: copy live ranges to compact positions via transfer queue, update handle → offset tables, invalidate stale BDA pointers in instance SSBO.
   - Gate defrag behind a per-frame budget (e.g. max 2MB relocated per frame) to avoid frame spikes.

---

## 6) “What am I missing?” — coverage matrix

Every gap item maps to the phase/track that addresses it. Items not covered by any phase are **unresolved** and need design work before implementation.

| # | Gap | Phase | Track | Status |
|---|-----|-------|-------|--------|
| 1 | Strong handle generation checks for GPUScene slots | B (§5.2) | T2 | Addressed |
| 2 | Relocation-safe arena protocol with remap + deferred reclaim | B + D (§5.2, §5.4) | T2 | Addressed |
| 3 | Descriptor epoch contract across frames-in-flight | D (§5.4) | T2 | Addressed |
| 4 | Centralized indirect schema (single versioned definition) | A + C (§5.1, §5.3) | T1 | Addressed |
| 5 | Visibility diagnostics API for debug UI and telemetry | A (§5.1) | T5 | Addressed |
| 6 | Degenerate bounds policy (radius <= 0, NaN, invalid scales) | A (§5.1) | T5 | Addressed (existing shader guards) |
| 7 | Line/Point GPU culling parity with Surface path | C (§5.3) | T1 | Addressed |
| 8 | LOD policy (distance + projected-size + error metric) | — | — | **Unresolved** — deferred until demand from large-scene benchmarks |
| 9 | Streaming budget policy (per-frame upload byte caps) | D (§5.4) | T2 | Addressed |
| 10 | Validation harness: CPU/GPU visibility parity | A (§5.1) | T5 | Addressed |
| 11 | Ownership linting (module boundaries + runtime asserts) | All phases | T5 | Ongoing |
| 12 | Pass isolation (no ECS queries during execution) | A (§5.1) | T4 | Addressed |
| 13 | Attribute-table ABI (versioned layouts + shader compat) | B (§5.2) | T2 | Addressed |
| 14 | Visualization permutation control | C (§5.3) | T1 | Addressed (§8.8) |
| 15 | Pick pipeline compatibility with unified visibility | C (§5.3) | T6 | Addressed |
| 16 | Shadow pass integration with `Graphics.Visibility` | C (§5.3) | T6 | Addressed |
| 17 | Debug draw transient carve-out | C (§5.3) | T4 | Addressed |
| 18 | PostProcess dependency chain preservation | All phases | — | Addressed (render graph invariant) |
| 19 | `StagingBelt` fixed-capacity risk | D (§5.4) | T2 | Addressed |
| 20 | `RetirementFrames` consistency (must be 3 everywhere) | B (§5.2) | T2 | Addressed |
| 21 | DebugDraw integration test (transient renders under full GPU cull) | C (§5.3) | T5 | **New** — add to Track T5 |

---

## 7) Workstream plan (track decomposition)

Split the effort into independent tracks with strict deliverables. Tracks are numbered T1–T6 to avoid collision with tasks/backlog/legacy-todo.md section letters (A–F).

### Track T1 — Visibility & Indirect Authority
- Introduce `Graphics.Visibility` module as a facade that SurfacePass calls into (not a code move — avoids regression risk).
- Consolidate frustum plane extraction from `CullDrawPackets()` and `SurfacePass::PrepareStage3()`.
- Unify indirect output schema: define `VisibilityOutputSchema` struct covering `DrawIndexedIndirectCommand`, `VisibilityRemap[]`, and `DrawCount` per geometry.
- Deliver CPU/GPU parity tests on deterministic scenes.
- Deliver telemetry counters (frustum reject, bounds reject, total visible).

### Track T2 — GPU Scene Unification
- Implement global vertex/index arenas in `Graphics.GpuScene` with slice allocator + free-list.
- Preserve `ReuseVertexBuffersFrom` semantics: shared vertex slice, distinct index slice.
- Port lifecycle upload paths (`MeshRendererLifecycle`, `GraphLifecycle`, `PointCloudLifecycle`) to arena-backed slices.
- Add generational handles to `GPUScene` slot allocator (currently raw `uint32_t`).
- Deliver coalescing free-list (adjacent free ranges merged).
- Deliver relocation-safe remap layer (deferred to Phase D for actual defrag).

### Track T3 — ECS View Contracts
- Add `Render*View` ECS components with proper tag types (not `StrongHandle<uint64_t>`).
- Make lifecycle systems publish views alongside existing pass components (dual-path).
- Keep backward compatibility adapter until passes are switched (end of Phase C deadline).
- Validate that `RenderViews` components carry no Vulkan types (matching existing component pattern).

### Track T4 — Render Packetization
- Extract draw packet types from `Graphics.RenderPipeline.cppm` into `Graphics.RenderPackets`.
- Replace `std::vector` fields in `LineDrawPacket`/`PointDrawPacket` with `std::span` or arena-backed ranges.
- Add `TransientPacket` type for `DebugDraw` content (never culled, no GPUScene slot).
- Build packet builder consuming extraction snapshot + visibility outputs.
- Freeze immutable packet schema and lifetime contract.

### Track T5 — Validation & Telemetry
- CPU/GPU cull parity tests on canonical scenes (unit sphere grid, off-frustum entities, degenerate bounds).
- Stale-handle guard tests (free + realloc slot, verify generation mismatch detected).
- Timeline/sync hazard tests (concurrent `QueueUpdate` + `Sync` + `FreeSlot`).
- GPU counters + frame-time markers via `RHI.Profiler`.
- Pick pipeline regression tests (ensure picking works for GPU-culled entities near frustum edge).
- Transient DebugDraw integration test: verify that DebugDraw lines/points/triangles render correctly even when all retained entities are frustum-culled.

### Track T6 — Pick & Shadow Integration
- Audit pick pipelines (`pick_mesh`, `pick_line`, `pick_point`) for compatibility with `Graphics.Visibility` outputs.
- Define pick visibility policy: unculled draw, or inflated-frustum dispatch.
- Audit shadow pass (`Graphics.Passes.Shadow`) for light-space frustum culling via `Graphics.Visibility`.
- Deliver integration tests for pick-under-cull and shadow-under-cull scenarios.

---

## 8) Migration risks and mitigations

### 8.1 Regression risk during visibility extraction (Phase A)

Moving culling out of `SurfacePass` into `Graphics.Visibility` changes the data flow for the most performance-critical path. **Mitigation:** Introduce `Graphics.Visibility` as a new module that SurfacePass *calls into* rather than extracting code. SurfacePass becomes a thin adapter. Existing culling tests validate parity before and after.

### 8.2 Arena fragmentation under scene churn (Phase B)

A single global vertex/index arena will fragment under repeated load/unload cycles (common in editor workflows). Without defragmentation, the arena high-water mark grows monotonically. **Mitigation:** Phase B must ship with at minimum a free-list compaction pass that coalesces adjacent free ranges. Full defragmentation (relocate + remap) can be deferred to Phase D, but the free-list must be compacting from day one.

### 8.3 Dual-path maintenance burden (Phase A–C overlap)

During the transition, both old per-pass components (`Surface::Component`, `Line::Component`, `Point::Component`) and new `RenderViews` components coexist. Every lifecycle system change must update both paths. **Mitigation:** Strict phase gating — no new features land on the old path once `RenderViews` components exist. Set a deadline for removing the compatibility adapter (end of Phase C).

### 8.4 Pick pipeline breakage during GPU culling unification (Phase C)

When Line/Point move to GPU culling, their pick pipelines must also move (or be explicitly excluded from culling). GPU-culled entities that are frustum-rejected but mouse-hovered become unpickable. **Mitigation:** Pick passes must use a separate, unculled draw path (or their own visibility dispatch with an inflated frustum). Document this contract in `Graphics.Visibility`.

### 8.5 Descriptor set exhaustion under attribute table growth (Phase B)

Adding 5 attribute-table SSBOs would need descriptor slots. Sets 0–3 are already consumed. While Vulkan 1.3 guarantees `maxBoundDescriptorSets >= 4` (and most discrete GPUs support 8–32), relying on set 4+ introduces a portability concern on integrated GPUs. **Mitigation:** Use BDA (buffer device addresses) for attribute tables, matching the existing vertex/index access pattern. No new descriptor sets required — attribute table base addresses travel via push constants or the existing instance SSBO.

### 8.6 Swapchain resize interaction with arenas and visibility buffers

Global vertex/index arenas and the `InstanceSSBO`/`BoundsSSBO` are extent-independent — they survive swapchain resize with no action required. Visibility output buffers (compacted visible lists, indirect command buffers, draw counters) are count-based, not extent-based — their size depends on instance count, not framebuffer resolution. Only render targets themselves need recreation on resize. **No special resize handling is needed for any Phase A–D structures.**

### 8.7 Arena allocation failure recovery (Phase B/D)

The current per-entity buffer model fails gracefully per-entity (one entity's allocation failure does not block others). A global arena failure would prevent all new geometry from rendering. **Mitigation:** (a) The arena has a growth policy with a hard cap (e.g., 512 MB vertex + 256 MB index). (b) When the cap is reached, `AllocateSlice()` returns `std::unexpected` with a capacity error. (c) The lifecycle system propagates this as a `GeometryUploadFailed` dispatcher event (existing pattern) and skips rendering that entity — it does not crash. (d) The arena logs a warning with current/peak usage to guide capacity tuning.

### 8.8 Shader permutation explosion from visualization modes (Phase C)

Adding per-domain `VisualizationMode` toggles with scalar/rgb/rgba/vector variants can produce a combinatorial explosion, especially when combined with existing pipeline variants (forward/deferred, wireframe/filled, depth-prepass). **Mitigation:** Use specialization constants (not separate SPIR-V binaries) for visualization mode selection. Visualization mode is orthogonal to lighting path — a single spec-constant dimension, not a multiplicative cross-product. Limit to at most 4 specialization constant values per domain × 3 domains = 12 visualization pipeline variants per pass. These compose with (not multiply against) the existing lighting-path variants because the visualization override bypasses the material/lighting branch entirely. Fall back to uniform color when an attribute is missing rather than creating a "missing attribute" shader variant.

---

## 9) Verification gates

### 9.1 Correctness

- CPU/GPU visible set parity on canonical scenes.
- No stale-handle dereference under slot reuse.
- Deterministic packet contents for same extracted snapshot.

### 9.2 Performance

- CPU render preparation P95 under target budget.
- Visibility dispatch + compaction costs stable with instance growth.
- Minimal per-frame allocations in hot path.

### 9.3 Robustness

- Degenerate geometry does not crash or explode bounds.
- Missing normals/radii/covariance degrades to deterministic fallback modes.
- Descriptor + sync validation clean under Vulkan Sync2.

---

## 10) Practical migration order (recommended)

1. **Extract Visibility module first** (highest modular ROI, low risk). Introduce as a facade — SurfacePass calls into it. Add CPU/GPU parity tests before proceeding.
2. **Extract RenderPackets module** (mechanical refactor, low risk). Move draw packet types out of `Graphics.RenderPipeline.cppm`. Replace `std::vector` fields with `std::span`.
3. **Introduce render-view ECS components** (freeze contracts early). Dual-path with existing components during transition.
4. **Unify topology storage into global arenas** (largest change, done once contracts are stable). Add generational handles to GPUScene. Preserve `ReuseVertexBuffersFrom` semantics.
5. **Switch Line/Point to GPU culling + indirect**. Add transient content carve-out for DebugDraw. Verify pick pipeline compatibility.
6. **Integrate pick & shadow passes** with unified visibility. Define pick visibility policy.
7. **Enable streaming + defrag + descriptor epoching**. Build on existing `TransferManager` + `StagingBelt`.

This order minimizes regressions while converging to the desired architecture. Steps 1–3 are mechanical refactors with low regression risk. Step 4 is the highest-risk structural change — it should only proceed once contracts from steps 1–3 are stable and tested.

---

## 11) Final boundary litmus test

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

## 12) C++23 module skeletons for the new boundaries

These are minimal reference shapes to lock the API direction.

```cpp
// Graphics.Visibility.cppm
module;
#include <expected>
#include <span>
#include <cstdint>
#include <vulkan/vulkan.h>
export module Graphics.Visibility;

import Core;
import Graphics.GPUScene;
import RHI;

export namespace Graphics::Visibility
{
    struct VisibilityInput
    {
        const GPUScene* Scene = nullptr;
        glm::mat4 ViewProjection{1.0f};
        uint32_t FrameIndex = 0;
        VkCommandBuffer Cmd = VK_NULL_HANDLE;
    };

    struct VisibilityOutput
    {
        uint32_t VisibleCount = 0;
        RHI::BufferLease* IndirectBuffer = nullptr;    // VkDrawIndexedIndirectCommand array
        RHI::BufferLease* VisibilityBuffer = nullptr;  // instance slot remap per visible draw
        RHI::BufferLease* DrawCountBuffer = nullptr;   // per-geometry atomic draw counts

        struct Diagnostics
        {
            uint32_t FrustumRejected = 0;
            uint32_t BoundsRejected = 0;    // radius <= 0 or NaN
            uint32_t TotalTested = 0;
        } Stats{};
    };

    [[nodiscard]] std::expected<VisibilityOutput, Core::ErrorCode>
    DispatchFrustumCull(VisibilityInput input);
}
```

```cpp
// Graphics.Visibility.cpp
module Graphics.Visibility;
import Core.Profiling;

namespace Graphics::Visibility
{
    std::expected<VisibilityOutput, Core::ErrorCode>
    DispatchFrustumCull(VisibilityInput input)
    {
        if (!input.Scene)
            return std::unexpected(Core::ErrorCode::InvalidArgument);
        if (input.Cmd == VK_NULL_HANDLE)
            return std::unexpected(Core::ErrorCode::InvalidArgument);

        // Extract 6 frustum planes from ViewProjection.
        // Dispatch instance_cull_multigeo.comp.
        // Compact visible ids, write indirect args.
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
    // Tag types for type-safe handle discrimination.
    // StrongHandle<Tag> uses the tag for compile-time type safety only —
    // the tag is not stored at runtime.
    struct GpuGeometrySliceTag {};
    struct GpuAttrSliceTag {};

    struct RenderTopologyView
    {
        Core::StrongHandle<GpuGeometrySliceTag> GeometrySlice{};
    };

    struct RenderAttributeViews
    {
        // Lazily populated per entity type — most fields will be invalid
        // for any given entity. Systems must check IsValid() before use.
        Core::StrongHandle<GpuAttrSliceTag> VertexAttrs{};
        Core::StrongHandle<GpuAttrSliceTag> EdgeAttrs{};
        Core::StrongHandle<GpuAttrSliceTag> FaceAttrs{};
        Core::StrongHandle<GpuAttrSliceTag> PointAttrs{};
        Core::StrongHandle<GpuAttrSliceTag> VectorAttrs{};
    };

    struct GpuInstanceRecordTag {};

    struct RenderInstanceView
    {
        Core::StrongHandle<GpuInstanceRecordTag> InstanceRecord{};
        uint32_t VisibilityMask = 0xFFFFFFFF; // per-pass visibility bits
    };
}
```
