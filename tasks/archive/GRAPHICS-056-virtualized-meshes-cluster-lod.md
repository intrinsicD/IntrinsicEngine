# GRAPHICS-056 — Virtualized meshes with cluster DAG and continuous LOD (planning, bounded scope)

- Status: completed (2026-06-03; planning-only; `Scaffolded`; bounded scope).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children
  `GRAPHICS-056-Impl-A..E` stay unopened until the bounded-scope cluster-LOD
  path is scheduled; the bounded-scope marker keeps Nanite parity out of scope.

## Goal
Lock down the contract for an opt-in virtualized-mesh pipeline that extends the meshlet representation from `GRAPHICS-044` with a cluster DAG carrying parent/child links for continuous LOD selection, an LOD selector compute pass that picks the appropriate cluster level per visible region, and an explicit fallback to non-virtualized meshlets — *with bounded scope*: this task does **not** target Nanite parity, software rasterization for sub-pixel triangles, or streaming page-residency for cluster pages. Planning only — no shader bodies and no DAG builder here.

## Non-goals
- **Explicitly not Nanite parity.** No software-rasterization fallback. No cluster-page streaming. No two-pass HW/SW raster split. Documented as long-term out-of-scope per the modernization roadmap.
- No removal of legacy indexed-triangle path or basic meshlet path.
- No virtualized-geometry-specific BVH builder (RT BVH continues to consume the underlying triangle representation).
- No CPU-side LOD selection.
- No automatic DAG generation at runtime; DAGs are baked at authoring time.

## Context
- Owner layer: `assets/` (cluster DAG builder), `graphics/assets` (DAG upload), `graphics/rhi` (`GpuGeometryRecord` extension), `graphics/renderer` (LOD selector compute pass).
- Nanite (Karis et al., SIGGRAPH 2021/2024) is the reference for cluster DAG + continuous LOD; full parity is a multi-year effort. This task captures the *bounded* version: cluster-level LOD selection at meshlet granularity that delivers the bulk of the geometry-detail-scaling benefit without the software-raster machinery.
- Cross-links: `GRAPHICS-038` (HZB cull at cluster granularity), `GRAPHICS-044` (meshlet representation prerequisite), `GRAPHICS-053` (mesh shaders are the natural dispatch primitive), `GRAPHICS-054` (work-graph mesh nodes are a future replacement for the LOD selector).

## Recorded decisions
1. **DAG structure.** Per cluster record: `ParentClusterId` (or `kInvalidClusterId` sentinel for roots), a fixed-capacity `ChildClusterId[N]` array with `N = 8` (unused slots = sentinel), `SimplificationError` (float, object-space), and a `BoundingSphere` (vec4 center+radius). Stored as a flat `ClusterDagRecord` SSBO addressed by `ClusterDagBDA`. Rationale: a flat fixed-capacity record is GPU-friendly (one indexed fetch, no pointer chasing) and a child fan-out of 8 matches the typical meshoptimizer greedy-cluster merge factor while keeping the record a small fixed stride; carrying both the per-cluster error and bounds in the record lets the selector and HZB cull read everything they need from one fetch.
2. **Error metric.** Locked: simplification error stored as a world-space surface-deviation radius in object space, projected to screen-space pixels at LOD-selection time via `screenError = objectError * projectionScale / distance`, with a conservative rule that a parent is only collapsed to when its projected error ≤ the recipe pixel budget AND all of its children's projected errors also clear the budget (monotone, hole-free selection). Rationale: storing object-space error and projecting at selection time keeps the baked DAG resolution-independent; the monotone "parent only if children also fit" rule is the standard Nanite-style cut-selection invariant that guarantees a crack-free LOD cut across the DAG, and pinning the exact projection formula makes the selector deterministic and unit-testable for monotonicity.
3. **DAG builder.** Authoring-time only, under `tools/mesh-virtualize/`, building the DAG bottom-up from `GRAPHICS-044` meshlets via greedy clustering + meshoptimizer simplification (`meshopt_simplify`), invoked offline as part of the asset bake (not at runtime). Rationale: bottom-up greedy clustering over already-meshletized geometry reuses the existing meshlet bake output as the DAG leaf level, meshoptimizer is the established simplification dependency, and confining the builder to `tools/` under offline invocation keeps the heavy build cost out of both the runtime and the promoted graphics layers per the non-goals.
4. **LOD selector pass.** A compute pass that, given the camera and per-instance DAG roots, walks the DAG and emits a per-instance selected-cluster-id list into an append buffer; dispatch is one thread-group per instance (or per coarse screen region for very large instances), bounded by a recipe `MaxSelectedClustersPerInstance`. Rationale: a compute pass keeps LOD selection on the GPU (the non-goals forbid CPU-side selection), one-group-per-instance is the simplest correct dispatch that parallelizes across the scene, and an explicit append-buffer cap makes the output size bounded and the overflow case (decision 10) well-defined.
5. **HZB integration.** Cluster bounds are HZB-culled per `GRAPHICS-038` *after* LOD selection (select the cut first, then cull the selected clusters' bounds), so culling operates on the cluster set actually chosen for this frame. Rationale: selecting first and culling second means the HZB only tests the clusters that survived LOD selection (far fewer than the full DAG), and it keeps the two passes independently testable; the ordering is recorded explicitly because the reverse (cull then select) would discard ancestors needed to evaluate the monotone cut.
6. **Bounded scope marker.** Explicitly recorded and load-bearing: **no software rasterization** for sub-pixel triangles, **no cluster-page streaming** (the whole DAG is resident; out-of-core streaming is out of scope), and **no two-pass HW/SW raster split**. This task delivers cluster-granularity continuous LOD only. Rationale: Nanite's remaining machinery (SW raster for sub-pixel triangles, page streaming, HW/SW split) is each a multi-year effort with its own residency and rasterizer contracts; recording the exclusion in the design decisions (not just the non-goals) prevents an implementation child from silently growing into a Nanite re-implementation, and cluster-granularity LOD already captures the bulk of the detail-scaling benefit.
7. **Coexistence with non-virtualized meshlets.** Geometries opt in per asset; a virtualized mesh dispatches through the same `MeshletViaCompute` or `MeshletViaMeshShader` lanes selected by `GRAPHICS-053`, with the LOD selector feeding the selected-cluster list into the existing meshlet dispatch instead of the full meshlet set. Rationale: routing virtualized meshes through the existing meshlet dispatch lanes means no new draw path or pipeline is introduced — virtualization is purely an upstream cluster-selection stage — so the basic meshlet and legacy indexed-triangle paths remain the untouched default and a virtualized asset is just a meshlet asset with a DAG attached.
8. **`GpuGeometryRecord` extension.** Add `ClusterDagBDA` (device address of the cluster DAG SSBO), `RootClusterId` (entry cluster, `kInvalidClusterId` ⇒ non-virtualized), and `LodErrorScale` (per-asset error-metric scale). Backward compatible: an existing record with `RootClusterId == kInvalidClusterId` and zero BDA is exactly the current non-virtualized meshlet/triangle record. Rationale: three additive fields with an invalid-sentinel default make the extension backward compatible by construction (every existing record reads as non-virtualized), and storing the DAG by BDA keeps the record fixed-size while the variable-length DAG lives in its own buffer.
9. **RT BVH interaction.** Virtualized meshes build their BLAS over the *underlying triangle representation at a single authoring-time-chosen LOD* (not the runtime-selected continuous LOD), recorded per asset. Rationale: ray tracing needs a stable acceleration structure that cannot be rebuilt per frame as the raster LOD cut changes, so pinning the BLAS to one baked LOD is the standard trade-off — rays may hit slightly coarser/finer geometry than the raster path, which is acceptable for shadows/AO/GI and far cheaper than per-frame BVH refit; recording the rule makes the discrepancy a known, documented limitation rather than a surprise.
10. **Diagnostics.** `VirtualizedClustersSelectedHistogram[Lod]` (per-LOD-level selected-cluster counts) and `LodSelectorOverflowCount` (instances that hit `MaxSelectedClustersPerInstance`) as atomic counters. Rationale: the per-LOD histogram is the single signal that shows whether continuous LOD is actually scaling detail with distance (the whole point of the feature), and the overflow counter surfaces the one bounded-buffer failure mode from decision 4 so a too-small cap is observable rather than silently clamping geometry.
11. **Test split.** `unit` for the authoring-time DAG builder roundtrip and error-metric monotonicity (parent error ≥ max child error); `contract;graphics` for the `GpuGeometryRecord` extension layout and the LOD-selector dispatch shape under null RHI; opt-in `gpu;vulkan` smoke for visual continuous-LOD correctness. Rationale: the builder and error monotonicity are pure CPU logic best covered by fast `unit` tests, the record layout and dispatch shape are null-RHI-checkable contract surfaces, and only the visual LOD-transition correctness needs a real device — so the default gate stays green and only the genuinely device-dependent proof opts into `gpu;vulkan`.
12. **Layering.** No live ECS. `assets/` owns the DAG builder (CPU-only authoring), `graphics/assets` owns DAG upload, and `graphics/` consumes baked DAGs. Rationale: keeping the builder in the CPU-only `assets/` layer and the consumer in `graphics/` preserves AGENTS.md §2 — graphics never runs the authoring-time simplification and never imports live ECS — so the virtualized-mesh contract adds no new cross-layer edge.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales, including the bounded-scope marker that explicitly excludes Nanite parity (no software rasterization, no cluster-page streaming).
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-056-Impl-A** — `GpuGeometryRecord` extension + `contract;graphics` shape tests.
- **GRAPHICS-056-Impl-B** — Authoring-time DAG builder under `tools/` + `unit` tests.
- **GRAPHICS-056-Impl-C** — LOD selector compute pass + null-RHI dispatch tests.
- **GRAPHICS-056-Impl-D** — HZB cull integration (gated by `GRAPHICS-038`) + dispatch via `MeshletViaCompute` / `MeshletViaMeshShader`.
- **GRAPHICS-056-Impl-E** — Opt-in `gpu;vulkan` continuous-LOD smoke.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The geometry-pipeline section of `docs/architecture/graphics.md` (with the bounded-scope marker), the `GpuGeometryRecord` section of `src/graphics/rhi/README.md`, and the LOD section of `src/graphics/renderer/README.md` are deferred to the implementation children (`GRAPHICS-056-Impl-A..D`); per AGENTS.md §9 those docs describe current state, and this planning slice adds no current-state behavior. The recorded decisions above — including the explicit bounded-scope marker — plus the `GRAPHICS-035` roadmap pointer are this slice's docs surface.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Bounded-scope marker is explicit and cited (decision 6 + the roadmap pointer; the architecture-doc citation lands with the implementation children per AGENTS.md §9).
- [x] Implementation child slices are identified but not opened.
- [x] Legacy indexed-triangle and basic meshlet paths remain the unconditional default.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` bounded-scope slice. All twelve cluster-LOD decisions are recorded with explicit answers and trade-off rationales: the flat fixed-capacity `ClusterDagRecord` (parent + 8 children + error + bounds), the object-space error projected to screen pixels with the monotone hole-free cut rule, the offline `tools/mesh-virtualize/` greedy-cluster + meshoptimizer builder, the one-group-per-instance LOD selector compute pass with a bounded append buffer, the select-then-HZB-cull ordering, the load-bearing bounded-scope marker (no SW raster, no cluster-page streaming, no two-pass HW/SW split), per-asset opt-in routed through the existing `MeshletViaCompute`/`MeshletViaMeshShader` lanes, the three additive backward-compatible `GpuGeometryRecord` fields with the invalid-sentinel default, the authoring-time-pinned BLAS LOD for RT, the per-LOD histogram + overflow diagnostics, the unit/contract/opt-in-gpu test split, and the assets/graphics layering with no live ECS. Implementation children `GRAPHICS-056-Impl-A..E` are identified but not opened; legacy indexed-triangle and basic meshlet paths stay the unconditional default and no shader bodies / DAG builder land. Per AGENTS.md §9 the architecture-doc/README updates (including the bounded-scope marker citation) are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No software rasterization for sub-pixel triangles.
- No cluster-page streaming.
- No Nanite parity goal scope creep.
- No removal of legacy indexed-triangle path.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
