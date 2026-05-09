# GRAPHICS-053 — Mesh shaders RHI extension (IMeshShaderDevice) (planning)

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

## Design decisions to record
1. **Capability surface.** `IMeshShaderDevice` is fetched via `IDevice::QueryInterface<IMeshShaderDevice>()`. Returns `nullptr` when unavailable.
2. **Pipeline kinds.** Mesh-shader pipelines extend the existing graphics-pipeline desc with `TaskShader` + `MeshShader` slots replacing VS. Record the desc shape.
3. **Indirect dispatch.** `DispatchMeshTasksIndirect` consumes the same indirect-draw buffer shape as the existing 8-bucket lanes; the bucket emitting it is selected by recipe. Record the rule.
4. **Per-task workgroup contract.** Task shader receives meshlet-group-id; outputs visible meshlet count + per-meshlet payload (uint indices). Mesh shader consumes payload, emits vertices + primitives. Record the payload shape limit (per Vulkan: 16 KB workgroup memory).
5. **Output limits.** Per mesh-shader workgroup ≤ 256 vertices and ≤ 256 primitives by spec; engine convention matches `GRAPHICS-044` meshlet limits (≤ 64 vertices, ≤ 124 primitives). Record the rule.
6. **HZB integration.** Task shader invokes the HZB cull from `GRAPHICS-038` per meshlet. Record the inlined cull module reference.
7. **Recipe selection.** Recipes carry a `GeometryDispatchKind { LegacyIndexed, MeshletViaCompute, MeshletViaMeshShader }`. Default: `LegacyIndexed`. `MeshletViaMeshShader` falls back to `MeshletViaCompute` when capability missing.
8. **Diagnostics.** `MeshShaderTasksDispatchedCount`, `MeshShaderMeshletsEmittedCount`, `MeshShaderFallbackCount`. Atomic counters.
9. **Operational-gate addition.** Append "Mesh shader capabilities probed and recorded" as a gate in `GRAPHICS-033`'s reason enum without rewriting earlier gates.
10. **Test split.** `contract;graphics` for capability surface and recipe selection under null-RHI mocks; opt-in `gpu;vulkan` smoke for one-meshlet correctness.
11. **Layering.** No live ECS. No vendor SDK.

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales.
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-053-Impl-A** — `IMeshShaderDevice` interface + null-RHI mock + capability tests.
- **GRAPHICS-053-Impl-B** — Mesh-shader pipeline desc + recipe selection + null-RHI shape tests.
- **GRAPHICS-053-Impl-C** — Task-shader HZB-cull integration (gated by `GRAPHICS-038` and `GRAPHICS-044`).
- **GRAPHICS-053-Impl-D** — Vulkan recording bodies + opt-in `gpu;vulkan` smoke (gated by `GRAPHICS-033`).
- **GRAPHICS-053-Impl-E** — Operational-gate extension in `GRAPHICS-033` reason enum + diagnostic wiring.

## Tests
- Planning slice: validators only.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/graphics.md` RHI capability section.
- Update `src/graphics/rhi/README.md` capability surface.
- Update `src/graphics/vulkan/README.md` operational-gate section.

## Acceptance criteria
- Eleven decisions are recorded with explicit answers and trade-off rationales.
- Implementation child slices are identified but not opened.
- Engine compiles and runs without mesh-shader-capable hardware.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No removal of legacy indexed-triangle path.
- No silent extension enablement.
- No vendor SDK imports.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
