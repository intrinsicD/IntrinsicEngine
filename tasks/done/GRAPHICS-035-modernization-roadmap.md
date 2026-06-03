# GRAPHICS-035 — Rendering modernization roadmap (umbrella planning index)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Leaf planning slices proceed independently; implementation children stay unopened until Theme A is complete.

## Goal
Record the agreed phased roadmap that takes the promoted rendering stack from its current 2025-era foundation (explicit RHI, render graph, snapshot extraction, GPU-driven culling, bindless materials) to a 2026+ feature set (modern frame structure, Slang-based shading, visibility-buffer/meshlet path, hardware ray tracing + hybrid GI, virtual shadow maps, reconstruction/upscaling seam, research-grade differentiators). This task is the index that orders and cross-links the leaf planning slices `GRAPHICS-036` through `GRAPHICS-058`; it does **not** open any implementation child slices.

## Non-goals
- No implementation. No CMake option additions. No new RHI/runtime/graphics behavior in this slice.
- No new shaders, materials, passes, or pipelines.
- No relaxation of fail-closed Vulkan behavior; `GRAPHICS-033` continues to gate operational bring-up.
- No collapsing of the 8-bucket draw-lane contract or the snapshot-extraction boundary; both remain invariants.
- No commitment to ship Nanite-style virtualized geometry as a goal — `GRAPHICS-056` records the bounded scope explicitly.
- No legacy code copying into promoted layers.

## Context
- Owner layer: cross-cutting. Each leaf task records its precise owning layer per `AGENTS.md` §2 and §4. No new dependency edges are introduced in this slice.
- Engine baseline (verified by the 2026-05-09 rendering analysis):
  - RHI exposes dynamic rendering, sync2-style barriers, descriptor indexing (PARTIALLY_BOUND + UPDATE_AFTER_BIND), buffer device address, timeline semaphores, VMA. Vulkan device remains fail-closed (`IsOperational() == false`) until `GRAPHICS-033` is satisfied.
  - Frame graph supplies a typed pass+resource DAG with transient aliasing, automatic Sync2 barrier synthesis, and pass culling. Multi-queue / async compute is **not** wired.
  - `CullingPass` performs frustum culling into 8 indirect-draw lanes (`SurfaceOpaque`, `SurfaceAlphaMask`, `Lines`, `Points`, `ShadowOpaque`, three Selection lanes). HZB + two-phase occlusion are **not** present.
  - Forward path is integrated; classic deferred G-buffer + composition is defined but operationally gated.
  - Material/light/shadow systems exist; no clustered binning, no IBL, no multi-scatter compensation.
  - Post chain is FXAA / SMAA only; no TAA, no upscaler seam, no temporal history.
  - GPU asset cache is non-evicting with hot-reload retire-deadline pattern; transfer queue is hardened with full-chain mip uploads.
  - Snapshot extraction (`AGENTS.md` §4: "graphics has no live ECS knowledge") is the architectural backbone and remains untouched.
- The `docs/agent/blueprint/ground-up-redesign-blueprint-2026.md` three-graph fabric (CPU task graph + GPU frame graph + async streaming graph) is the long-horizon target this roadmap progressively realizes.
- Cross-links: `GRAPHICS-001` (parity inventory), `GRAPHICS-018` family (Vulkan integration), `GRAPHICS-022` (rendergraph diagnostics), `GRAPHICS-023` family (hot reload), `GRAPHICS-025` (hybrid path), `GRAPHICS-028..034` (residency, sandbox, operational gate).

## Roadmap structure
The leaf planning slices are grouped into five phases. Each phase is independently testable (CPU-only where possible) and progressively unblocks the next.

### Phase 1 — Modern frame structure
Foundational architectural shifts that are expensive to retrofit later. Must precede feature additions.
- `GRAPHICS-036` — Pipelined frames and double-buffered render world (sim N / render N-1).
- `GRAPHICS-037` — Async compute and multi-queue scheduling in the frame graph.
- `GRAPHICS-038` — HZB and two-phase occlusion culling extension to `CullingPass`.
- `GRAPHICS-039` — Clustered light binning.
- `GRAPHICS-040` — TAA pass and reconstructor/upscaler interface seam.

### Phase 2 — Shader and material modernization
- `GRAPHICS-041` — Slang as canonical shading language with module compilation and hot reload.
- `GRAPHICS-042` — PBR feature completeness (multi-scatter GGX compensation, sheen, anisotropy, clear-coat, split-sum IBL).
- `GRAPHICS-043` — Visibility buffer recipe and compute-shader deferred materialization.
- `GRAPHICS-044` — Meshlet geometry representation in `GpuGeometryRecord`.

### Phase 3 — Ray tracing and global illumination
- `GRAPHICS-045` — `IRayTracingDevice` capability extension on the RHI (BLAS/TLAS, inline RT, ray pipelines, SBT).
- `GRAPHICS-046` — Hybrid GI (ReSTIR DI/GI HW path + software fallback).
- `GRAPHICS-047` — Virtual Shadow Maps to replace the cascade atlas.

### Phase 4 — Research differentiators
- `GRAPHICS-048` — 3D Gaussian Splatting rasterizer pass over the PointCloud primitive.
- `GRAPHICS-049` — Neural radiance cache slot in the GI path.
- `GRAPHICS-050` — Neural texture compression (random-access decode in material kernels).
- `GRAPHICS-051` — Differentiable rendering mode (gradient propagation through the frame graph).
- `GRAPHICS-052` — Deltaful GPU-resident scene (incremental snapshot deltas instead of full re-extraction).

### Phase 5 — Frontier
- `GRAPHICS-053` — `IMeshShaderDevice` capability extension on the RHI.
- `GRAPHICS-054` — `IWorkGraphDevice` capability extension on the RHI.
- `GRAPHICS-055` — Streaming virtual textures (SVT) with frame-graph-owned page table.
- `GRAPHICS-056` — Virtualized meshes (cluster DAG + continuous LOD) — bounded scope, not Nanite parity.
- `GRAPHICS-057` — DirectStorage-analog GPU decompression hookpoint on the transfer queue.
- `GRAPHICS-058` — Frame generation pass.

## Required changes
- [x] Land this roadmap file as the single ordered index of the modernization phases.
- [x] Open each leaf planning slice (`GRAPHICS-036..058`) as a separate planning-only task following the `GRAPHICS-029..034` template.
- [x] Update `tasks/backlog/rendering/README.md` DAG to list the new tasks in dependency order, with each entry citing its upstream gates.
- [x] Cross-link this roadmap from `docs/architecture/graphics.md` and `docs/architecture/rendering-three-pass.md`. Do **not** edit semantic content of those docs in this slice.

## Tests
- [x] Planning slice: validators only.
- [x] Each leaf task records its own implementation-child test split. The default verification gate stays:
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] `docs/architecture/graphics.md` — add a "Modernization roadmap" pointer to this task.
- [x] `docs/architecture/rendering-three-pass.md` — add a "Future work" pointer to this task without editing pass semantics.
- [x] `tasks/backlog/rendering/README.md` — DAG entry insertion (dependency order under existing `GRAPHICS-034`).

## Acceptance criteria
- [x] All twenty-three leaf planning slices exist as task files.
- [x] The DAG in `tasks/backlog/rendering/README.md` lists them with explicit upstream gates.
- [x] This roadmap is reachable from `docs/architecture/graphics.md` and `docs/architecture/rendering-three-pass.md`.
- [x] No implementation child slices are opened.
- [x] No semantic code or shader changes land.
- [x] Layering invariants in `AGENTS.md` §2 and §4 hold; this roadmap introduces no new dependency edges.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` umbrella index. All twenty-three leaf planning slices (`GRAPHICS-036..058`) exist as task files, the `tasks/backlog/rendering/README.md` DAG lists them in dependency order with explicit upstream gates, and this roadmap is now reachable from `docs/architecture/graphics.md` (`## Modernization roadmap`) and `docs/architecture/rendering-three-pass.md` (`## Where Active Work Lives`). No implementation child slices are opened and no semantic code or shader changes land; the leaves remain planning-only until Theme A's visible-geometry foundation is complete per `tasks/backlog/README.md`.

## Forbidden changes
- No implementation, no shaders, no pipelines, no CMake option growth.
- No relaxation of fail-closed Vulkan behavior.
- No collapsing of the snapshot-extraction boundary.
- No premature opening of implementation children for any leaf task.
- No mixing of mechanical file moves with semantic refactors.
- No legacy code copying into promoted graphics layers.
