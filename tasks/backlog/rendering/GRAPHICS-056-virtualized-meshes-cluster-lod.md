# GRAPHICS-056 — Virtualized meshes with cluster DAG and continuous LOD (planning, bounded scope)

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

## Design decisions to record
1. **DAG structure.** Per cluster: parent cluster id (or sentinel root), child cluster ids (up to N), simplification error metric, bounding sphere. Record the canonical record layout.
2. **Error metric.** Locked: world-space surface deviation in object space, projected to screen-space pixels at LOD-selection time. Record the formula and the conservatism rule.
3. **DAG builder.** Authoring-time only, under `tools/mesh-virtualize/` (or chosen canonical location). Builds DAG bottom-up from `GRAPHICS-044` meshlets via greedy clustering + meshoptimizer simplification. Record the build-time invocation.
4. **LOD selector pass.** A compute pass, given the camera, walks the DAG per region of screen and selects the cluster level whose projected error fits a recipe-defined pixel budget. Outputs a per-instance cluster-id list. Record the dispatch shape.
5. **HZB integration.** Cluster bounds are HZB-culled per `GRAPHICS-038` after LOD selection. Record the order rule.
6. **Bounded scope marker.** Explicitly record: no software rasterization, no cluster-page streaming, no two-pass HW/SW split. Cite this in the design decisions to prevent scope creep into a Nanite re-implementation.
7. **Coexistence with non-virtualized meshlets.** Geometries opt in per asset; the renderer dispatches virtualized meshes via the same `MeshletViaCompute` or `MeshletViaMeshShader` lanes selected by `GRAPHICS-053`. Record the rule.
8. **`GpuGeometryRecord` extension.** Add `ClusterDagBDA`, `RootClusterId`, `LodErrorScale`. Backward compatible: `RootClusterId == invalid` means non-virtualized.
9. **RT BVH interaction.** Virtualized meshes still build BLAS over the *underlying triangle representation* at a fixed LOD chosen at authoring time, not at the runtime-selected LOD. Record the rule and the trade-off.
10. **Diagnostics.** `VirtualizedClustersSelectedHistogram[Lod]`, `LodSelectorOverflowCount`. Counters atomic.
11. **Test split.** `unit` for DAG builder roundtrip + error-metric monotonicity; `contract;graphics` for `GpuGeometryRecord` extension + LOD-selector dispatch shape under null RHI; opt-in `gpu;vulkan` smoke for visual continuous-LOD correctness.
12. **Layering.** No live ECS. `assets/` owns the builder; graphics consumes baked DAGs.

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales, including the bounded-scope marker that explicitly excludes Nanite parity (no software rasterization, no cluster-page streaming).
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-056-Impl-A** — `GpuGeometryRecord` extension + `contract;graphics` shape tests.
- **GRAPHICS-056-Impl-B** — Authoring-time DAG builder under `tools/` + `unit` tests.
- **GRAPHICS-056-Impl-C** — LOD selector compute pass + null-RHI dispatch tests.
- **GRAPHICS-056-Impl-D** — HZB cull integration (gated by `GRAPHICS-038`) + dispatch via `MeshletViaCompute` / `MeshletViaMeshShader`.
- **GRAPHICS-056-Impl-E** — Opt-in `gpu;vulkan` continuous-LOD smoke.

## Tests
- Planning slice: validators only.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/graphics.md` geometry-pipeline section with the bounded-scope marker.
- Update `src/graphics/rhi/README.md` `GpuGeometryRecord` section.
- Update `src/graphics/renderer/README.md` LOD section.

## Acceptance criteria
- Twelve decisions are recorded with explicit answers and trade-off rationales.
- Bounded-scope marker is explicit and cited in docs.
- Implementation child slices are identified but not opened.
- Legacy indexed-triangle and basic meshlet paths remain the unconditional default.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No software rasterization for sub-pixel triangles.
- No cluster-page streaming.
- No Nanite parity goal scope creep.
- No removal of legacy indexed-triangle path.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
