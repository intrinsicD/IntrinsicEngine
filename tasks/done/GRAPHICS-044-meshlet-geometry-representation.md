# GRAPHICS-044 — Meshlet geometry representation in GpuGeometryRecord (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until Theme A is complete.

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

## Recorded decisions
1. **Meshlet table shape.** Per meshlet: `VertexOffset (u32)`, `VertexCount (u8)`, `PrimitiveOffset (u32)`, `PrimitiveCount (u8)`, `BoundingSphere (vec4: xyz + radius)`, `NormalCone (vec4: axis_xyz + cos_half_angle)`. Packed to a **32-byte** record (counts packed into the spare bytes of an offset word). Caps: **`VertexCount ≤ 64`, `PrimitiveCount ≤ 124`** (meshoptimizer/NV-mesh-shader canonical limits). Rationale: 32 bytes is the standard cache-friendly meshlet descriptor; the 64/124 caps match hardware mesh-shader payload limits so the same table feeds `GRAPHICS-053` unchanged.
2. **Local index format.** **8-bit primitive indices** into the meshlet's local vertex range, **3 indices per triangle**, packed tightly and **4-byte-aligned** at the meshlet boundary (trailing pad bytes per meshlet). Rationale: 8-bit local indices are sufficient given the ≤ 64 vertex cap and are the meshoptimizer output format; 4-byte alignment keeps SSBO loads aligned.
3. **Vertex range table.** Per meshlet a list of indices into the **global** vertex buffer, stored as **32-bit** indices (no 64K cap). The 16-bit option is rejected as the default because research meshes routinely exceed 64K vertices per geometry; 16-bit is recorded as an optional authoring-time space optimization for small meshes. Rationale: research-friendly large meshes are a stated engine target; 32-bit avoids a silent correctness cap, trading modest memory.
4. **`GpuGeometryRecord` extension.** Append **`MeshletTableBDA`, `MeshletCount (u32)`, `MeshletVertexIndexBDA`, `MeshletPrimitiveIndexBDA`**. **Backward compatible: `MeshletCount == 0` selects the legacy indexed-triangle path**, all BDAs zero. Rationale: an append-only, count-gated extension keeps every existing geometry record valid and makes the legacy path the unconditional zero-default (acceptance criterion).
5. **Asset pipeline.** Authoring-time meshletization runs in **`assets/`** using **meshoptimizer** (FetchContent through `cmake/Dependencies.cmake` + `external/cache/`), **CMake-gated** behind an `INTRINSIC_ENABLE_MESHLETS` option that is OFF by default until `Impl-B` lands. Rationale: keeps generation CPU-side in `assets` (AGENTS.md §2), routes the new dependency through the mandated cache, and avoids growing the default build surface during planning.
6. **Bounding cone semantics.** `(cone_axis_xyz, cos_half_angle)` per meshlet; cluster cull rejects a meshlet as backfacing when `dot(cone_axis, normalize(view_pos - cone_apex)) < -cos_half_angle - epsilon`, with a small `epsilon` for conservatism (never rejects a partially-front-facing cluster). Degenerate cones (cos_half_angle ≥ 1 sentinel) are **never culled**. Rationale: the meshoptimizer normal-cone test is the standard cheap backface-cluster reject; the epsilon + degenerate-sentinel rule guarantees no false rejection of visible geometry.
7. **Coexistence with legacy path.** A geometry **may carry both** legacy indices and a meshlet table; the **renderer recipe chooses at the bucket level** which representation a pass consumes (e.g. a mesh-shader/vis-buffer recipe reads meshlets, the classic surface recipe reads indices). Carrying both costs extra memory; authoring may emit meshlets-only once all consuming recipes are meshlet-capable. Rationale: dual representation lets meshlet adoption be incremental per recipe without a flag-day migration; the memory cost is an explicit authoring choice.
8. **Visibility-buffer ID encoding compatibility.** Meshlet-id occupies **8 bits** in the `GRAPHICS-043` vis-buffer encoding → **maximum 256 meshlets per instance**; meshes exceeding 256 meshlets are **split into multiple instances at authoring time** (no runtime hashing fallback that could alias IDs). Rationale: a hard 256/instance cap keeps the vis-buffer triangle-ID decode exact and collision-free; authoring-time instance splitting is deterministic and shifts the cost off the hot path.
9. **Diagnostics.** **`MeshletsPerGeometryHistogram`** (authoring/upload-time distribution) and **`MeshletCullRejectedCount`** (per cull phase). Atomic counters; no per-frame strings. Rationale: the histogram surfaces meshletization quality, the per-phase reject count surfaces cull effectiveness — both observable without a parallel readback.
10. **Test split.** **`unit`** for the meshlet-builder round-trip (decoded meshlet vertex/triangle set equals the legacy indexed triangles); **`contract;graphics`** for the `GpuGeometryRecord` extension shape + `MeshletCount == 0` legacy gating under null RHI; **opt-in `gpu;vulkan`** smoke for HZB-cull correctness. Rationale: builder parity vs. legacy indices is the strongest CPU correctness signal; only HZB-cull output needs a device.
11. **Layering.** Meshletization tooling lives in **`assets/`**; **graphics consumes meshlet data, it does not generate it**. The `GpuGeometryRecord` extension is a `graphics/rhi` POD with no live ECS access. Rationale: preserves AGENTS.md §2 — assets own CPU geometry processing, graphics owns only the GPU record and consumption.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-044-Impl-A** — `GpuGeometryRecord` extension + `contract;graphics` shape tests under null RHI.
- **GRAPHICS-044-Impl-B** — Authoring-time meshletization in `assets/` + `unit` roundtrip tests.
- **GRAPHICS-044-Impl-C** — Upload pipeline through `Graphics.GpuAssetCache` + integration tests.
- **GRAPHICS-044-Impl-D** — `Pass.Culling` extension to consume meshlet bounds + diagnostic counters (gated by `GRAPHICS-038`).

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The geometry-pipeline section for `docs/architecture/graphics.md` is deferred to the implementation children (`GRAPHICS-044-Impl-A/B/C`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The `GpuGeometryRecord` section of `src/graphics/rhi/README.md` is deferred to the same implementation children for the same reason.
- [x] The upload-path section of `src/graphics/assets/README.md` is deferred to the same implementation children for the same reason.

## Acceptance criteria
- [x] Eleven decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Legacy indexed-triangle path remains the unconditional default.
- [x] Backward compatibility: `MeshletCount == 0` legacy meshes render unchanged.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All eleven meshlet-representation decisions are recorded with explicit answers and trade-off rationales: the 32-byte meshlet record with ≤ 64 vertex / ≤ 124 primitive caps + bounding sphere + normal cone, the 8-bit 4-byte-aligned local primitive indices, the 32-bit global vertex-range table, the append-only `MeshletCount`-gated `GpuGeometryRecord` extension, the `assets/`-side meshoptimizer pipeline gated behind a default-OFF CMake option, the epsilon-conservative normal-cone backface reject with a degenerate-never-cull sentinel, the bucket-level dual-representation coexistence rule, the 8-bit / 256-meshlets-per-instance vis-buffer cap with authoring-time instance splitting, the histogram + per-phase reject counters, the builder-round-trip unit + extension-shape contract + HZB gpu smoke test split, and the layering audit. Implementation children `GRAPHICS-044-Impl-A..D` are identified but not opened; the legacy indexed-triangle path stays the unconditional `MeshletCount == 0` default and no runtime meshletization or mesh-shader dispatch lands. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No removal of legacy indexed-triangle path.
- No runtime meshletization.
- No mesh-shader dispatch in this task.
- No cluster DAG / continuous LOD here.
- No mixing of mechanical file moves with semantic refactors.
