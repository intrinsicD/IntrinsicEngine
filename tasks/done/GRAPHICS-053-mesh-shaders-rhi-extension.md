# GRAPHICS-053 — Mesh shaders RHI extension (IMeshShaderDevice) (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until the meshlet representation (`GRAPHICS-044`) and operational gate (`GRAPHICS-033`) feed an implementation slice.

## Goal
Lock down the contract for an optional `IMeshShaderDevice` capability extension on the RHI exposing the modern task→mesh pipeline (replacing VS/HS/DS/GS), task-shader culling at meshlet group granularity, mesh-shader emit of vertex/primitive output, with explicit "not supported" handling and a fallback that keeps the legacy indexed-triangle and meshlet-via-compute paths working. Planning only — no Vulkan extension enables in this slice.

## Non-goals
- No work-graph mesh-node integration (covered by `GRAPHICS-054`).
- No Nanite-style cluster DAG (`GRAPHICS-056`).
- No removal of legacy indexed-triangle and meshlet-via-compute paths.
- No mobile mesh-shader optimization.
- No `VK_EXT_mesh_shader` enables in `src/graphics/vulkan/` in this planning slice.

## Context
- Owner layer: `graphics/rhi` (`IMeshShaderDevice` capability), `graphics/vulkan` (extension wiring + `vkCmdDrawMeshTasksIndirectEXT`), `graphics/renderer` (consumer pipelines).
- `VK_EXT_mesh_shader` (Khronos, 2022; widely supported on RTX 20-series and later, Radeon RX 6000+, Apple M-series, Adreno gradually) is the modern replacement for the geometry-shader pipeline. Task shaders perform per-meshlet-group culling; mesh shaders emit final vertex/primitive output.
- Cross-links: `GRAPHICS-044` (meshlet representation), `GRAPHICS-038` (HZB cull pairs naturally with task-shader cull), `GRAPHICS-054` (work-graph mesh nodes), `GRAPHICS-056` (cluster DAG / continuous LOD consumes mesh shaders).

## Recorded decisions
1. **Capability surface.** `IMeshShaderDevice` is fetched via `IDevice::QueryInterface<IMeshShaderDevice>()`, returning `nullptr` when unavailable. Rationale: reusing the capability-query seam (mirroring `GRAPHICS-045`'s `IRayTracingDevice`) keeps mesh shaders purely additive and lets the engine compile/run unchanged on hardware without `VK_EXT_mesh_shader`, with the renderer guarding every use on the query.
2. **Pipeline kinds.** Mesh-shader pipelines extend the existing graphics-pipeline desc with `TaskShader` + `MeshShader` slots replacing VS, with the desc shape recorded. Rationale: extending the existing pipeline desc (rather than a parallel one) reuses the established pipeline-cache/recompile machinery, and replacing only the VS slot keeps the rest of the fixed-function/state desc identical so a mesh-shader pipeline drops into the same recipe plumbing.
3. **Indirect dispatch.** `DispatchMeshTasksIndirect` consumes the same indirect-draw buffer shape as the existing 8-bucket lanes, with the emitting bucket selected by recipe. Rationale: reusing the indirect-draw buffer layout means the existing GPU-driven cull output feeds mesh-task dispatch without a translation step, and recipe-level bucket selection keeps the choice of mesh-shader-vs-classic-draw a recipe decision rather than a global mode.
4. **Per-task workgroup contract.** The task shader receives a meshlet-group-id and outputs a visible meshlet count + per-meshlet payload (uint indices); the mesh shader consumes the payload and emits vertices + primitives, with the payload shape bounded by the Vulkan 16 KB workgroup-memory limit. Rationale: the task→mesh payload handoff is the core mesh-shader contract, and pinning the payload to the spec's 16 KB workgroup-memory ceiling guarantees portability across vendors that all guarantee at least that much.
5. **Output limits.** Per mesh-shader workgroup ≤ 256 vertices and ≤ 256 primitives by spec, with the engine convention matching `GRAPHICS-044` meshlet limits (≤ 64 vertices, ≤ 124 primitives). Rationale: targeting the `GRAPHICS-044` meshlet caps (well under the spec maximums) means the same baked meshlet table feeds mesh shaders unchanged and stays within every vendor's emit limits, avoiding a mesh-shader-specific re-meshletization.
6. **HZB integration.** The task shader invokes the HZB cull from `GRAPHICS-038` per meshlet, with the inlined cull module reference recorded. Rationale: task shaders are the natural place to run per-meshlet occlusion cull before emitting work, and reusing the `GRAPHICS-038` HZB cull as an inlined module keeps a single cull implementation shared between the compute and mesh-shader paths.
7. **Recipe selection.** Recipes carry a `GeometryDispatchKind { LegacyIndexed, MeshletViaCompute, MeshletViaMeshShader }` defaulting to `LegacyIndexed`; `MeshletViaMeshShader` falls back to `MeshletViaCompute` when the capability is missing. Rationale: a three-state dispatch enum lets geometry delivery be selected per recipe with a graceful capability-driven downgrade, and defaulting to `LegacyIndexed` keeps the proven path unconditional so no scene regresses before mesh-shader recording is smoke-tested.
8. **Diagnostics.** `MeshShaderTasksDispatchedCount`, `MeshShaderMeshletsEmittedCount`, and `MeshShaderFallbackCount` are atomic counters. Rationale: dispatch/emit counts surface mesh-shader workload and cull effectiveness (tasks dispatched vs meshlets emitted), and the fallback counter makes the capability-driven downgrade observable — all without strings.
9. **Operational-gate addition.** Append "Mesh shader capabilities probed and recorded" to `GRAPHICS-033`'s reason enum without rewriting earlier gates. Rationale: the `GRAPHICS-033` gate enum is append-only by contract, so mesh shaders join as a new optional gate that stays `NotRequested` until a recipe opts in, preserving every prior gate's meaning and the first-failing-gate ordering.
10. **Test split.** `contract;graphics` for the capability surface and recipe selection under null-RHI mocks; opt-in `gpu;vulkan` smoke for one-meshlet correctness. Rationale: capability surface and recipe selection are device-independent and stay on the default CPU gate; only the actual mesh-shader emit needs a device, keeping the default gate green on hardware without mesh shaders.
11. **Layering.** No live ECS and no vendor SDK. Rationale: preserves AGENTS.md §2 — the mesh-shader path consumes the meshlet table and snapshot, owns only GPU pipeline state, and never touches live ECS or middleware.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-053-Impl-A** — `IMeshShaderDevice` interface + null-RHI mock + capability tests.
- **GRAPHICS-053-Impl-B** — Mesh-shader pipeline desc + recipe selection + null-RHI shape tests.
- **GRAPHICS-053-Impl-C** — Task-shader HZB-cull integration (gated by `GRAPHICS-038` and `GRAPHICS-044`).
- **GRAPHICS-053-Impl-D** — Vulkan recording bodies + opt-in `gpu;vulkan` smoke (gated by `GRAPHICS-033`).
- **GRAPHICS-053-Impl-E** — Operational-gate extension in `GRAPHICS-033` reason enum + diagnostic wiring.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The RHI capability section of `docs/architecture/graphics.md` is deferred to the implementation children (`GRAPHICS-053-Impl-A/B`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The capability-surface section of `src/graphics/rhi/README.md` is deferred to the same implementation children for the same reason.
- [x] The operational-gate section of `src/graphics/vulkan/README.md` is deferred to `GRAPHICS-053-Impl-D/E` for the same reason.

## Acceptance criteria
- [x] Eleven decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Engine compiles and runs without mesh-shader-capable hardware.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All eleven mesh-shader RHI decisions are recorded with explicit answers and trade-off rationales: the `QueryInterface`-fetched optional `IMeshShaderDevice` returning `nullptr` when unavailable, the VS-slot-replacing `TaskShader`+`MeshShader` pipeline desc extension, `DispatchMeshTasksIndirect` reusing the 8-bucket indirect-draw buffer, the 16 KB-bounded task→mesh payload contract, output limits matching the `GRAPHICS-044` meshlet caps, the inlined `GRAPHICS-038` HZB task-shader cull, the `GeometryDispatchKind` selector defaulting to `LegacyIndexed` with a capability-driven downgrade to `MeshletViaCompute`, the three atomic mesh-shader counters, the append-only `GRAPHICS-033` gate addition, the null-RHI-contract + opt-in one-meshlet `gpu;vulkan` test split, and the no-live-ECS / no-vendor-SDK layering audit. Implementation children `GRAPHICS-053-Impl-A..E` are identified but not opened; the legacy indexed-triangle and meshlet-via-compute paths stay supported, no `VK_EXT_mesh_shader` enables land, and the engine compiles/runs without mesh-shader hardware. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No removal of legacy indexed-triangle path.
- No silent extension enablement.
- No vendor SDK imports.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
