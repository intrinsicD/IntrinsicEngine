# GRAPHICS-044 — Meshlet geometry representation in GpuGeometryRecord (planning)

## Goal
Lock down the contract for adding a meshlet table (vertex offset, triangle offset, vertex count ≤ 64, primitive count ≤ 124, bounding cone for backface cluster culling, bounding sphere for frustum/HZB culling) to `GpuGeometryRecord`, so that GPU-driven culling, mesh-shader dispatch, and visibility-buffer materialization can operate at meshlet granularity. Planning only — no upload pipeline changes and no shader bodies land here.

## Non-goals
- No mesh-shader dispatch (covered by `GRAPHICS-053`).
- No virtualized geometry / cluster DAG (`GRAPHICS-056`).
- No removal of the legacy indexed-triangle path; it remains supported alongside meshlets.
- No CPU-side meshlet building at runtime; meshlet data is baked into geometry assets at authoring time.
- No visibility-buffer integration *body* here; only the ID encoding compatibility (`GRAPHICS-043` consumer).

## Context
- Owner layer: `assets` (CPU meshlet asset shape), `graphics/assets` (GPU upload), `graphics/rhi` (`GpuGeometryRecord` extension), `graphics/renderer` (consumer).
- Today `GpuGeometryRecord` carries vertex/index buffer device addresses + counts. Modern GPU-driven rendering operates per *meshlet* (≤ 64 vertices, ≤ 124 triangles), enabling per-cluster culling, mesh-shader dispatch, and stable visibility-buffer triangle IDs.
- The meshoptimizer reference (Arseny Kapoulkine) is the canonical meshlet builder. It produces vertex remap, primitive list (8-bit local indices), bounding sphere, and normal cone.
- Cross-links: `GRAPHICS-038` (HZB cull at meshlet granularity), `GRAPHICS-043` (vis-buffer encodes meshlet-id), `GRAPHICS-053` (mesh-shader dispatch consumes meshlets), `GRAPHICS-056` (cluster DAG builds on meshlet representation).

## Design decisions to record
1. **Meshlet table shape.** Per meshlet: vertex offset, vertex count, primitive offset, primitive count, bounding sphere (xyz, radius), normal cone (xyz, cos-half-angle). Records: 32 bytes per meshlet baseline.
2. **Local index format.** 8-bit primitive indices into the meshlet's vertex range. Record packing rule (3 indices per triangle, 4-byte-aligned padding).
3. **Vertex range table.** Per meshlet a list of indices into the global vertex buffer. Decide between 16-bit (capped at 64K vertices per geometry) and 32-bit (no cap, more memory) — default 32-bit for research-friendly large meshes.
4. **`GpuGeometryRecord` extension.** Add `MeshletTableBDA`, `MeshletCount`, `MeshletVertexIndexBDA`, `MeshletPrimitiveIndexBDA`. Backward compatible: `MeshletCount == 0` means legacy indexed-triangle path.
5. **Asset pipeline.** Authoring-time meshletization runs in `assets/` using meshoptimizer (or equivalent). Record the dependency placement and CMake gating.
6. **Bounding cone semantics.** `(cone_axis_xyz, cos_half_angle)` per meshlet. Cluster cull rejects meshlets where `dot(cone_axis, view_to_center) < -cos_half_angle - epsilon` (backface). Record the conservatism rule.
7. **Coexistence with legacy path.** Geometries can carry both legacy indices and meshlet table; renderer recipes choose at the bucket level. Record the rule for memory cost vs. flexibility.
8. **Visibility-buffer ID encoding compatibility.** Meshlet-id is 8 bits in the vis-buffer encoding from `GRAPHICS-043`. Maximum 256 meshlets per instance — record the cap and the hashing fallback for larger meshes (split into multiple instances at authoring time).
9. **Diagnostics.** `MeshletsPerGeometryHistogram`, `MeshletCullRejectedCount` (per phase). Counters atomic.
10. **Test split.** `unit` for meshlet-builder roundtrip (vertex/triangle parity vs. legacy indices); `contract;graphics` for `GpuGeometryRecord` extension under null RHI; opt-in `gpu;vulkan` smoke for HZB-cull correctness.
11. **Layering.** Meshletization tooling lives in `assets/`. Graphics consumes meshlet data; it does not generate it.

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales.
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-044-Impl-A** — `GpuGeometryRecord` extension + `contract;graphics` shape tests under null RHI.
- **GRAPHICS-044-Impl-B** — Authoring-time meshletization in `assets/` + `unit` roundtrip tests.
- **GRAPHICS-044-Impl-C** — Upload pipeline through `Graphics.GpuAssetCache` + integration tests.
- **GRAPHICS-044-Impl-D** — `Pass.Culling` extension to consume meshlet bounds + diagnostic counters (gated by `GRAPHICS-038`).

## Tests
- Planning slice: validators only.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/graphics.md` geometry-pipeline section.
- Update `src/graphics/rhi/README.md` `GpuGeometryRecord` section.
- Update `src/graphics/assets/README.md` upload-path section.

## Acceptance criteria
- Eleven decisions are recorded with explicit answers and trade-off rationales.
- Implementation child slices are identified but not opened.
- Legacy indexed-triangle path remains the unconditional default.
- Backward compatibility: `MeshletCount == 0` legacy meshes render unchanged.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No removal of legacy indexed-triangle path.
- No runtime meshletization.
- No mesh-shader dispatch in this task.
- No cluster DAG / continuous LOD here.
- No mixing of mechanical file moves with semantic refactors.
