# GRAPHICS-039 — Clustered light binning (planning)

## Goal
Lock down the contract for a clustered (froxel-grid) light culling pass that bins extracted lights into view-space cells and exposes the resulting per-cell light list to surface shading, so that many-light scenes shade in O(lights-per-cell) rather than O(total-lights). Planning only — no shader bodies or pipeline changes land here.

## Non-goals
- No many-lights ReSTIR sampling (covered by `GRAPHICS-046`).
- No tiled-deferred or visibility-buffer materialization (`GRAPHICS-043`).
- No new light types beyond the existing `LightSnapshot` shape.
- No shadow caster changes; shadow assignment remains owned by `ShadowSystem`.
- No CPU-side light culling; entirely GPU-resident.

## Context
- Status: done (2026-05-18, branch `claude/graphics-rendering-tasks-dKlmC`).
- Commit reference: pending current change.
- Owner layer: `graphics/renderer` (compute pass + bind layout), `graphics/rhi` (existing storage-buffer surface).
- The current `LightSystem` extracts lights into a flat GPU buffer; surface shaders iterate the full list. This is fine for handful-of-lights scenes; modern AAA scenes carry hundreds of analytic lights.
- Clustered shading is the canonical pattern (Olsson/Persson 2012, Filament implementation, UE clustered+forward, Frostbite). Cells are typically 16×16×24 (XY tiles × log-Z slices).
- Cross-links: `GRAPHICS-009` (deferred lighting), `GRAPHICS-042` (PBR completeness wants per-cell IBL probe binning too), `GRAPHICS-046` (ReSTIR DI samples the same cell list).

## Design decisions to record
1. **Cluster grid shape.** Lock cell count XYZ (suggested 16×9×24 at 16:9, scalable; record the formula). Z slicing is logarithmic in view-Z; record the near/far policy.
2. **Cluster build pass.** Compute pass that produces an axis-aligned bounding box per cell. Decide between rebuild-every-frame (simpler, robust to camera changes) and reuse-when-camera-static (cheaper). Default: rebuild every frame.
3. **Light-to-cluster assignment pass.** Compute pass that tests each extracted light's bounding volume against each cell's AABB, emits per-cell light-index lists. Storage shape: a single packed index buffer + a per-cell offset/count header.
4. **Light bounding shapes.** Point: sphere. Spot: cone (decide cone-vs-AABB test). Directional: skip (always affects all cells). Record exact intersection routines.
5. **Per-cell capacity.** Decide hard limit (suggested 256 lights/cell) and overflow policy (clamp + diagnostic counter `LightClusterOverflowCount`). Record memory budget at the chosen cell count.
6. **Surface-shader integration.** Decide bind set + binding for the cluster index buffer + offset header. Surface shaders compute the cluster from `gl_FragCoord` + view-Z and iterate only the assigned indices.
7. **IBL probe extension.** The same clustering structure can hold IBL probe indices for `GRAPHICS-042`. Record the rule for sharing the index storage vs. parallel buffers (default: parallel buffer, same cell layout).
8. **Snapshot interaction.** `LightSystem` reads from the existing extracted `LightSnapshot` records; no new extraction fields. Record the rule.
9. **Async-compute affinity.** Cluster build and assignment passes are tagged with `QueueAffinity::AsyncCompute` per `GRAPHICS-037` once that lands. Default: graphics queue.
10. **Diagnostics.** `LightClusterOverflowCount`, `LightsCulledCount`, `EmptyClusterCount`. Atomic increments.
11. **Test split.** `contract;graphics` for cluster build, light assignment, overflow handling, all under null RHI; opt-in `gpu;vulkan` smoke for shader correctness.
12. **Layering audit.** No live ECS access. No new RHI surfaces.

## Recorded decisions

1. **Cluster grid shape.** Locked at 16×9×24 (X×Y×Z) cells for a 16:9 reference target. For non-16:9 aspect ratios the X/Y counts scale with the formula `clustersX = round(16 * (viewWidth / viewHeight) / (16/9))` and `clustersY = 9` (Y is the fixed axis; X stretches to keep cell aspect roughly square in screen-space). Z slicing is logarithmic in view-Z: `sliceZ = floor(log2(viewZ / near) * (numZSlices / log2(far / near)))`, with `near` and `far` taken from the active `CameraViewSnapshot`. Total cell count for 16:9 = 3,456. Rejected: 32×16×32 (over-bins low-light scenes for marginal accuracy gain in many-light scenes); fixed cell size in screen pixels (cell count varies with resolution, breaks buffer-size determinism).
2. **Cluster build pass.** Compute pass `Pass.ClusterBuild` runs every frame, *not* reuse-when-camera-static. Rationale: the rebuild cost is bounded (`clusters * AABB-compute` = ~3,456 AABBs computed in parallel, ~1 workgroup) and "camera-static" detection adds frame-input state that is fragile under jitter (TAA, animation, sub-pixel camera offsets). The pass outputs `Cluster.AABBs` (one `vec4` min + one `vec4` max per cluster = ~110 KB) into a frame-graph-owned storage buffer.
3. **Light-to-cluster assignment pass.** Compute pass `Pass.LightAssignment` dispatches one workgroup per cluster (3,456 workgroups for 16:9), each workgroup iterates all extracted lights and tests light bound vs cluster AABB. Output: a single `Cluster.LightIndices` packed buffer + a per-cluster `Cluster.LightHeader { uint32_t offset; uint32_t count; }`. Total `Cluster.LightIndices` capacity = `clusters * MaxLightsPerCluster = 3,456 * 256 = ~3.4 MB` of `uint16_t` indices (lights are bounded to 65,535 — well above realistic scene counts). Rejected: per-light "scatter into clusters" pass (atomic contention on per-cluster offset; harder to test deterministically).
4. **Light bounding shapes.** Point lights: bounding sphere centered at `LightSnapshot::Position` with radius = `LightSnapshot::Range` (the explicit cutoff carried on the snapshot today); intersect via sphere-vs-AABB (Arvo's algorithm). Spot lights: bounding cone with apex at `LightSnapshot::Position`, axis `LightSnapshot::Direction`, half-angle `acos(LightSnapshot::OuterConeCos)` (the snapshot stores the *cosine* of the outer half-angle — no `acos` is needed on the hot path because cone-vs-AABB can be expressed directly in terms of the dot product against the cone axis vs. `OuterConeCos`), height = `LightSnapshot::Range`. The cone-vs-AABB test follows Akenine-Möller's separating-axis variant; the cone is approximated by its bounding sphere first as a cheap early-out, then exact cone test on the survivors. `InnerConeCos` controls intra-cone falloff but does not change the conservative outer bound, so cluster culling ignores it. Directional lights: assigned to *all* clusters unconditionally (no AABB test); contribute to the `Cluster.LightHeader` count but stored at a reserved fixed offset in `Cluster.LightIndices` (the first N indices of every cluster's range are reserved for the directional light bank, where N = `numDirectionalLights`). Rejected: AABB-only test for spot lights (over-includes by up to 4× for wide cones); per-light SH/distant-light evaluation (out of scope; `LightSnapshot` shape is unchanged); deriving a separate "effective range" from `1/r^2` falloff (the snapshot's `Range` is already the authored cutoff; redefining it duplicates state and risks divergence from the lighting shaders that read the same field).
5. **Per-cell capacity.** Hard cap `MaxLightsPerCluster = 256` lights. Overflow policy: clamp (drop excess lights deterministically by ascending light index) and increment `LightClusterOverflowCount`. Rationale: 256 is the canonical Frostbite/UE bound; scenes that genuinely exceed it are pathological and would tank shading perf anyway; deterministic clamping (lowest indices win) ensures reproducible visual output. Memory budget at 16:9 = 3.4 MB for indices + 27 KB for headers = ~3.5 MB cluster light data per frame; doubled across `framesInFlight` retains ~7 MB peak. Rejected: variable per-cluster overflow buffers (heap fragmentation on the GPU side, complicates the surface-shader bind layout).
6. **Surface-shader integration.** Bind `Cluster.LightHeader` SSBO at `set = 4, binding = 0` and `Cluster.LightIndices` SSBO at `set = 4, binding = 1` (new descriptor set, parallel to the existing `set = 3` material set). Surface fragment shader computes the cluster via `clusterX = uint(gl_FragCoord.x * clustersX / viewWidth)`, `clusterY = uint(gl_FragCoord.y * clustersY / viewHeight)`, `clusterZ = uint(log2(viewZ / near) * (numZSlices / log2(far / near)))`, reads `Cluster.LightHeader[clusterIndex]`, iterates `count` entries in `Cluster.LightIndices[offset..offset+count]`, and accumulates lighting only for assigned indices. Rejected: bindless light index lookup via push constants (push-constant budget already pressured by material params); per-pass uniform-buffer light list (defeats the entire clustering purpose).
7. **IBL probe extension.** IBL probes share the cluster shape and the assignment-pass shader (extended with a probe-list output) but use a **parallel buffer** `Cluster.ProbeIndices` + `Cluster.ProbeHeader` at `set = 4, binding = 2/3`. Rationale: probes have very different bounding shapes (typically influence boxes or spheres with weight falloff) and different per-cluster capacity (usually ≤ 4 probes per cluster), so packing them into the same index buffer wastes space and complicates the surface-shader fetch. The shared cell layout means one cluster computation, one set of AABBs, two parallel index buffers — minimal cost increase. Probe assignment is gated by `MaterialFlags::ReceivesIBL`; surfaces that don't sample IBL skip the fetch entirely.
8. **Snapshot interaction.** `LightSystem` continues to read from the existing `LightSnapshot` records in `RuntimeRenderSnapshotBatch`. No new extraction fields. The cluster build + assignment passes consume the same `LightSnapshot` SSBO that today drives the existing forward/deferred light list. `LightSnapshot` retains its current shape as declared in `src/graphics/renderer/Graphics.LightSystem.cppm` — `{ LightType (Directional|Point|Spot), Position : vec3, Range : float, Direction : vec3, Intensity : float, Color : vec3, InnerConeCos : float, OuterConeCos : float }`. Cluster assignment derives the spot-cone bound from `Position` + `Direction` + `Range` + `OuterConeCos` per Decision 4 (no `acos` on the hot path); the point-light sphere bound uses `Position` + `Range`. Shadow-atlas mapping is not on the snapshot — `ShadowSystem` owns the atlas-index sidecar today and clustering does not need it. No "ColorIntensity" packed field is introduced; the existing `Color` (vec3) + `Intensity` (float) pair stays as-is.
9. **Async-compute affinity.** `Pass.ClusterBuild` and `Pass.LightAssignment` are tagged `QueueAffinity::AsyncCompute` per the `GRAPHICS-037` framework, gated on `HasAsyncCompute`. Default and fallback affinity = `Graphics` queue. Cross-queue edges: `Pass.LightAssignment` writes `Cluster.LightHeader` / `Cluster.LightIndices` on async-compute; the lighting/forward pass on the graphics queue waits on the assignment-pass timeline-semaphore signal before binding the SSBOs. The framegraph-transient cluster resources use `CONCURRENT` sharing mode per `GRAPHICS-037` Decision 4.
10. **Diagnostics.** Atomic counters on a new `ClusteredLightingDiagnostics` aggregate: `LightClusterOverflowCount` (per-frame total clamp events), `LightsCulledCount` (lights with no contributing cluster — typically because the camera is outside their effective range entirely; reported per frame for sanity-checking), `EmptyClusterCount` (per-frame count of clusters with zero assigned lights), `MaxLightsObservedPerCluster` (per-frame max, useful for budget tuning), `ClusterAssignmentPassExecutions` (overflow protection sanity check). All `std::atomic<uint64_t>` zeroed on engine `Initialize()`.
11. **Test split.** `contract;graphics` tests under null RHI cover (a) deterministic cluster grid construction for canonical resolution + near/far inputs, (b) light-to-cluster assignment for synthetic light sets with known sphere/cone overlaps, (c) overflow clamping behavior (insert 300 lights into a single cluster, assert 256 of the lowest indices land and `LightClusterOverflowCount = 44`), (d) directional-light unconditional assignment, (e) empty/all-empty cluster handling, (f) IBL probe parallel-buffer wiring without surface-shader execution. Opt-in `gpu;vulkan` smoke verifies real-shader correctness on a fixture scene with 32 point + 4 spot + 1 directional + 2 IBL probes, asserting per-pixel lighting matches a CPU reference within tolerance. Excluded from the default CPU gate.
12. **Layering audit.** No live ECS access. Light extraction continues through `RenderFrameInput::Lights`. Cluster build/assignment passes live under `src/graphics/renderer/`. Cluster compute shaders live under `assets/shaders/lighting/cluster_*.comp`. No new `graphics → ecs`, `graphics → runtime`, or `framegraph → vulkan` edges. The new descriptor `set = 4` consumed by surface shaders is declared in the same shader-pair manifests `MaterialSystem` already owns.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-039-Impl-A** — Cluster grid resource + build pass + null-RHI shape tests.
- **GRAPHICS-039-Impl-B** — Light-assignment pass + overflow diagnostic + `contract;graphics` tests.
- **GRAPHICS-039-Impl-C** — Surface-shader integration + per-bucket recipe wiring + integration tests.
- **GRAPHICS-039-Impl-D** — Async-compute affinity wiring (gated by `GRAPHICS-037`).

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] `docs/architecture/rendering-three-pass.md` clustered-shading section is deferred to Impl-A/B/C landing (planning slice forbids code changes; doc rows describe wired behavior).
- [x] `src/graphics/renderer/README.md` light-system section update is deferred to the same.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] No new RHI surfaces.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No new light types in this slice.
- No CPU-side light culling.
- No bypass of `LightSystem` extraction.
- No mixing of mechanical file moves with semantic refactors.
