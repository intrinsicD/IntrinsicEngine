# PLAN.md Thorough Review

This review evaluates `PLAN.md` against the engine target of **<2ms CPU frame time**, GPU-driven rendering principles, and robust geometry-processing constraints.

## Executive Assessment

The proposed “three primitive passes” architecture is directionally strong:

- It removes cross-pass routing complexity and duplicated visualization logic.
- It converges retained and transient rendering under a single pass contract per primitive.
- It preserves BDA/bindless-compatible data flow and keeps DebugDraw as a non-owning accumulator.

However, there are several high-risk implementation gaps that should be resolved before broad refactor execution:

1. **Transient buffer allocator and synchronization contract is underspecified** (risk: hazards/stalls).
2. **Edge extraction complexity and cache invalidation policy are too vague** (risk: CPU spikes).
3. **Push-constant payload growth needs explicit Vulkan limits strategy** (risk: portability breakage).
4. **Graph/point-cloud upload path lacks detailed ownership + versioning semantics** (risk: stale GPU data).
5. **Migration phases are correct directionally, but not dependency-safe for continuous integration**.

---

## What Is Strong in the Plan

### 1) Unified primitive ownership is architecturally clean

The decomposition into `SurfacePass`, `LinePass`, and `PointPass` with retained + transient internal paths is an excellent simplification. It aligns with stable render-graph feature registration and reduces feature combinatorics.

### 2) ECS composition model is better than boolean toggles

Using component presence/absence for feature enablement is EnTT-friendly and avoids dynamic branch-heavy code paths in submit systems.

### 3) BDA-centric shared geometry contract is correct

Shared `GeometryHandle` across multiple visualization components avoids vertex duplication and keeps residency/device memory use predictable.

### 4) Point mode as pipeline variants is the right call

Separate pipelines for FlatDisc/Surfel/EWA/Gaussian avoids mega-shader divergence and allows mode-specific blend/depth/raster states.

---

## Critical Risks & Required Clarifications

## A. Transient data lifecycle and synchronization

### Findings

The plan states each pass packs transient data into per-frame host-visible buffers and then draws retained + transient in one pass, but does not define:

- ring-buffer sizing strategy,
- allocation failure behavior,
- synchronization points between CPU writes and GPU reads,
- memory barrier templates for host-write → vertex/SSBO read.

### Required additions

Define a strict per-pass transient allocator contract:

- `N = frames_in_flight` ring pages per pass.
- Monotonic linear alloc per frame (`Reset(frameIndex)` only after GPU fence retire).
- hard cap per primitive type (line/point/triangle bytes), with overflow telemetry + graceful drop policy.
- explicit barriers (or coherent mapping assumptions) documented for Vulkan backend.

### Suggested API

- `bool TryAllocTransientBytes(size_t size, size_t alignment, TransientSlice& out)`
- overflow: skip draw, emit telemetry counter `TransientOverflow.<Pass>`.

---

## B. Edge extraction and caching policy is under-specified

### Findings

`LinePass` takes over unique-edge extraction from mesh indices. Without strict invalidation and complexity controls, this can violate the <2ms CPU target when topology churns.

### Required additions

1. **Topology versioning** in `GeometryGpuData` (or sidecar metadata):
   - increment on index buffer changes.
   - cache key = `(GeometryHandle, TopologyVersion)`.
2. **Complexity guardrails**:
   - edge extraction target complexity should be near linear average: `O(T)` expected over triangles with hash/set de-dup.
   - memory `O(E)` with pre-reserve heuristics.
3. **Budgeted rebuild**:
   - max edge-cache rebuilds per frame (e.g., 1–2 large meshes) to avoid frame spikes.

---

## C. Push constant portability and packet design

### Findings

Planned push-constant layouts are 96 bytes (`Line`) and 128 bytes (`Point`), which may pass on modern desktop GPUs but can fail on tighter limits or reduce compatibility.

### Required additions

- hard validation against `maxPushConstantsSize` during pipeline creation.
- fallback packet path via small dynamic UBO/SSBO when size > budget.
- enforce 16-byte alignment + ABI pack checks with static assertions.

---

## D. Graph and point-cloud GPU upload semantics

### Findings

The plan states new device-local uploads for graph and point-cloud geometries, but omits mutation/update semantics and ownership transfer timing.

### Required additions

- define immutable vs dynamic upload modes:
  - immutable: one-shot staging + device-local commit.
  - dynamic: double-buffered device-local or persistent host-visible staging policy.
- define generation/version increments for hot-reload or live edit.
- define whether `GeometryHandle` swap is atomic at system boundary.

---

## E. Phase sequencing and safety for incremental integration

### Findings

The migration list is comprehensive but can create intermediate broken states if executed strictly in order without compatibility shims.

### Required additions

Insert explicit compatibility gates:

1. **Dual-write systems** early: keep old and new components in sync until all consumers are migrated.
2. **Feature flags**:
   - `r.Render.SurfacePassV2`
   - `r.Render.LinePassV2`
   - `r.Render.PointPassV2`
3. **Deletion only after parity tests** pass in CI and render captures match baseline.

---

## Geometry-Processing Robustness Gaps

For a research-grade geometry engine, the plan should explicitly define degeneracy handling:

- zero-area triangles in surface normals/tangent basis,
- duplicate/near-duplicate vertices for point modes,
- non-manifold edges during edge extraction,
- NaN/Inf sanitization in transient debug input.

Recommended invariant checks:

- reject/skip non-finite positions before upload.
- classify triangle degeneracy by area threshold $A < \varepsilon$ and mark for skip.
- line width clamping to physically safe pixel range.
- point radius clamping to avoid huge clip-space quads.

Complexity note:

- Surface retained submission: typically `O(V + I)` preprocessing, draw dispatch mostly GPU-side.
- Edge extraction: `O(T)` expected with hash de-dup (`T` triangles).
- Point grouping by mode: `O(N)` over point components.

---

## Data-Oriented Design Review

### Positive

- Shared geometry buffers and BDA pointers are DOD-aligned.
- Separation of retained immutable data and transient per-frame data is cache-friendly.

### Needed improvements

- Replace/avoid `std::vector<EdgePair>` in hot component storage for large scenes; consider pass-local SoA caches keyed by handle.
- Define compact packed formats for transient primitives (`ABGR8`, quantized normal optional).
- Ensure pass iteration works on dense views and avoids random component indirections.

---

## Testing & Verification Matrix (Missing in Plan)

The plan should include explicit validation gates:

1. **Correctness**
   - visual parity captures old vs new for mesh wireframe, graph edges/nodes, point cloud modes.
   - deterministic draw counts for representative scenes.
2. **Performance**
   - CPU timings per pass (`AddPasses`) with telemetry percentiles.
   - transient upload byte counters and overflow counters.
3. **Robustness**
   - fuzzed transient submissions with degenerate/non-finite payloads.
   - large-scene stress tests (millions of lines/points).
4. **GPU**
   - validation layer clean runs for synchronization and descriptor indexing.

---

## Concrete Plan Amendments (Recommended)

1. Add a **“Transient Memory Contract”** section with allocator + sync details.
2. Add **topology versioning** and cache budget controls for line edge extraction.
3. Add **push-constant fallback** strategy with static/runtime checks.
4. Add **compatibility feature flags** and dual-write migration guard phase.
5. Add **degeneracy + numeric sanitization policy** for all primitive submissions.
6. Add **CI acceptance criteria** before deleting old passes.

---

## Suggested Revised Execution Order (Safer)

1. Introduce new components + dual-write adapters.
2. Introduce Surface/Line/Point passes behind feature flags (off by default).
3. Add transient allocator contract and telemetry first.
4. Migrate one primitive path at a time with golden image parity.
5. Enable feature flags by default after parity + perf gates.
6. Delete legacy passes and shaders only after two green CI cycles.

---

## Final Verdict

The architecture is fundamentally strong and matches modern GPU-driven engine practice. With the added memory/synchronization contract, cache invalidation/versioning rules, and migration guardrails, this plan can be executed safely and should materially reduce renderer complexity while improving extensibility.

Current readiness: **7.5/10** (high potential, but missing low-level contracts that determine production stability).
