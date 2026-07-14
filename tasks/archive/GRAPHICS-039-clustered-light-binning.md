# GRAPHICS-039 — Clustered light binning (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until Theme A is complete.

## Goal
Lock down the contract for a clustered (froxel-grid) light culling pass that bins extracted lights into view-space cells and exposes the resulting per-cell light list to surface shading, so that many-light scenes shade in O(lights-per-cell) rather than O(total-lights). Planning only — no shader bodies or pipeline changes land here.

## Non-goals
- No many-lights ReSTIR sampling (covered by `GRAPHICS-046`).
- No tiled-deferred or visibility-buffer materialization (`GRAPHICS-043`).
- No new light types beyond the existing `LightSnapshot` shape.
- No shadow caster changes; shadow assignment remains owned by `ShadowSystem`.
- No CPU-side light culling; entirely GPU-resident.

## Context
- Owner layer: `graphics/renderer` (compute pass + bind layout), `graphics/rhi` (existing storage-buffer surface).
- The current `LightSystem` extracts lights into a flat GPU buffer; surface shaders iterate the full list. This is fine for handful-of-lights scenes; modern AAA scenes carry hundreds of analytic lights.
- Clustered shading is the canonical pattern (Olsson/Persson 2012, Filament implementation, UE clustered+forward, Frostbite). Cells are typically 16×16×24 (XY tiles × log-Z slices).
- Cross-links: `GRAPHICS-009` (deferred lighting), `GRAPHICS-042` (PBR completeness wants per-cell IBL probe binning too), `GRAPHICS-046` (ReSTIR DI samples the same cell list).

## Recorded decisions
1. **Cluster grid shape.** Froxel grid **16×9×24** at 16:9, scaled by the tile formula `tilesX = ceil(renderWidth / clusterTilePx)`, `tilesY = ceil(renderHeight / clusterTilePx)` with `clusterTilePx` default 80 px (≈16×9 at 1280×720, growing with resolution); Z fixed at 24 slices. Z slicing is **logarithmic** in view-Z (Olsson exponential): `slice = floor(log(z / near) / log(far / near) * numZSlices)`, clamped to the camera near/far; cells beyond far stay empty. Rationale: matches the canonical Olsson/Filament layout; logarithmic Z keeps per-cell depth ratios uniform so light counts stay balanced; the tile-px formula scales XY with resolution without changing the contract.
2. **Cluster build pass.** A compute pass produces one **view-space AABB per cell**, **rebuilt every frame**. Reuse-when-camera-static is rejected as the default: the staleness bookkeeping and projection-change edge cases outweigh the modest cost of computing 16×9×24 = 3456 tiny AABBs. Rationale: simplicity and robustness; the build is cheap relative to assignment.
3. **Light-to-cluster assignment pass.** A compute pass tests each extracted light's bounding volume against each cell AABB and emits per-cell light-index lists. Storage: a single packed **light-index buffer** plus a per-cell **offset/count header** (`{ uint offset; uint count; }` per cell); indices are written with an atomic bump-allocator into the packed buffer. Rationale: the offset/count + packed-index layout is the canonical compact form and is friendly to the decision-6 surface-shader iteration.
4. **Light bounding shapes.** Point: **sphere** (center + radius) vs cell AABB via the sphere-AABB closest-point test. Spot: **cone**, tested with a bounding-sphere prefilter then a cone-vs-AABB separating-axis approximation — chosen over exact cone-AABB for shader simplicity, accepting small conservative over-inclusion (never drops a contributing light). Directional: **skipped** (affects all cells; handled outside the cluster list). Rationale: conservative inclusion preserves correctness; the sphere prefilter keeps the spot test cheap.
5. **Per-cell capacity.** Hard limit **256 lights/cell**; overflow = **clamp** (keep the first 256 assigned) + increment `LightClusterOverflowCount`. Worst-case memory at 16×9×24: packed index buffer 3456 × 256 × 4 B ≈ 3.5 MB, header 3456 × 8 B ≈ 27 KB. Rationale: 256 is generous for clustered-forward; clamping is fail-soft (slight under-lighting only in pathological cells) and observable rather than a crash or unbounded growth.
6. **Surface-shader integration.** Bind the packed index buffer + offset/count header as a read-only SSBO pair on the **per-frame global descriptor set** (`set 0` global scope, next free bindings alongside the scene-table/light buffer; exact binding indices owned by the implementation child against the then-current global layout). Surface shaders derive their cell from `gl_FragCoord.xy` (→ tile) and view-Z (→ log-Z slice), read the header, and iterate only the assigned indices. Rationale: per-cell iteration replaces the full-list loop; the global set avoids per-material descriptors.
7. **IBL probe extension.** The same cell layout can index IBL probes for `GRAPHICS-042` via a **parallel buffer** (separate packed-index + header sharing the identical cell layout), not shared index storage. Rationale: probes and lights have different counts/lifetimes; parallel buffers keep each list independently sized and let GRAPHICS-042 land without touching the light path.
8. **Snapshot interaction.** `LightSystem` reads the existing extracted `LightSnapshot` records; **no new extraction fields** are added — the cluster passes consume the already-extracted light bounds/type/transform. Rationale: keeps the runtime→graphics snapshot contract stable (same lifecycle-not-contract discipline as GRAPHICS-036).
9. **Async-compute affinity.** Cluster build and assignment are tagged `QueueAffinity::AsyncCompute` per `GRAPHICS-037` **once that lands**; default until then is the **graphics queue**. Rationale: build/assignment is independent of the current frame's raster and overlaps naturally on async compute, but must not depend on GRAPHICS-037 to be correct.
10. **Diagnostics.** Atomic counters `LightClusterOverflowCount` (cells hitting the 256 cap), `LightsCulledCount` (light-cell pairs rejected by the bound test), and `EmptyClusterCount` (cells with zero assigned lights), surfaced through the renderer light-system diagnostics. Atomic increments only; no per-frame strings.
11. **Test split.** `contract;graphics` (null RHI) for cluster-grid build (AABB correctness), light assignment (sphere/cone inclusion, directional skip), overflow clamp + counter, and empty-cell counting; opt-in `gpu;vulkan` smoke for shader-side cell selection + iteration correctness.
12. **Layering audit.** No live ECS access (lights arrive through `LightSnapshot`); no new RHI surface (reuses the existing storage-buffer + compute path); cluster passes live in `graphics/renderer`. No AGENTS.md §2 edge is crossed.

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
- [x] The clustered-shading section for `docs/architecture/rendering-three-pass.md` is deferred to the implementation children (`GRAPHICS-039-Impl-A/B/C`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The light-system section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.

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

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve clustered-light-binning decisions are recorded with explicit answers and trade-off rationales: the 16×9×24 log-Z froxel grid + tile-px scaling formula, rebuild-every-frame cluster AABBs, the packed-index + offset/count assignment storage, the sphere/cone/directional bounding-shape policy, the 256-lights/cell clamp + memory budget, global-set surface-shader integration, the parallel IBL-probe buffer, the unchanged `LightSnapshot` contract, `QueueAffinity::AsyncCompute` (default graphics until GRAPHICS-037), the three diagnostic counters, the test split, and the layering audit. Implementation children `GRAPHICS-039-Impl-A..D` are identified but not opened; no new RHI surface or light type lands. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No new light types in this slice.
- No CPU-side light culling.
- No bypass of `LightSystem` extraction.
- No mixing of mechanical file moves with semantic refactors.
