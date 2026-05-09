# GRAPHICS-054 — Work graphs RHI extension (IWorkGraphDevice) (planning)

## Goal
Lock down the contract for an optional `IWorkGraphDevice` capability extension on the RHI exposing GPU-side producer→consumer scheduling (work graphs): node graphs that dispatch downstream nodes from the GPU itself, including mesh nodes that feed the rasterizer directly. The extension is recorded as a *long-horizon* capability — D3D12 is in production (March 2024), the Vulkan equivalent is in flight, and this planning slice prepares the engine to adopt it cleanly without committing implementation timing. Planning only — no Vulkan extension enables here.

## Non-goals
- No commitment to ship an implementation in any specific phase.
- No D3D12 backend work (no D3D12 backend exists yet in the engine; this task is API-shape planning only).
- No removal of existing dispatch / draw-indirect paths.
- No vendor SDK imports.
- No CPU-side scheduler emulation.

## Context
- Owner layer: `graphics/rhi` (`IWorkGraphDevice` capability), `graphics/vulkan` (extension wiring; gated on Vulkan equivalent landing), `graphics/renderer` (consumer recipes for software-LOD or scheduler-driven workloads).
- D3D12 Work Graphs (Microsoft, March 2024 production; Mesh Nodes preview) provide GPU-side scheduling: nodes dispatch downstream nodes; mesh nodes feed the rasterizer; the scheduler replaces ExecuteIndirect+uber-shader patterns. AMD case studies show ~0.8–0.95 ms deferred shading on 4090 at 1080p; SIGGRAPH 2025 work-graphs course (AMD) is the canonical reference.
- The Vulkan equivalent is in flight as of 2025; the engine's plan is to gate this capability on its arrival rather than ship D3D12-only.
- Cross-links: `GRAPHICS-053` (mesh shaders are a prerequisite for mesh nodes), `GRAPHICS-056` (continuous-LOD scheduler is a natural consumer), `GRAPHICS-049` / `GRAPHICS-050` (neural shader dispatch is another natural consumer).

## Design decisions to record
1. **Capability surface.** `IWorkGraphDevice` is fetched via `IDevice::QueryInterface<IWorkGraphDevice>()`. Returns `nullptr` until both backend support and engine integration are present.
2. **Node kinds.** Lock the supported node kinds: `BroadcastingLaunchNode`, `CoalescingLaunchNode`, `ThreadLaunchNode`, `MeshNode` (gated on `GRAPHICS-053`). Record the rule that all node kinds are optional and absence is silently handled.
3. **Node payload shape.** Each node declares an input record type and emits typed payloads to downstream nodes. Record the payload size cap and the typing rule.
4. **Backing storage.** Work graphs require a backing memory allocation (D3D12: `WorkGraphBackingMemory`). Record the sizing query and the lifecycle.
5. **Scheduler determinism.** Forbid relying on inter-node ordering beyond the explicit data dependencies. Record the rule.
6. **Diagnostics.** `WorkGraphNodesDispatchedHistogram[NodeId]`, `WorkGraphBackingMemoryBytes`. Counters atomic.
7. **Recipe slots.** Reserve a `RecipeKind::WorkGraphScheduler` opt-in. No production recipes use this until a consumer opens.
8. **Operational-gate addition.** Append "Work graph capabilities probed and recorded" as a gate in `GRAPHICS-033`'s reason enum without rewriting earlier gates. The gate stays `NotRequested` until a consumer opens.
9. **Test split.** `contract;graphics` for capability surface and recipe-slot reservation under null-RHI mocks; opt-in `gpu;<backend>` smoke when backend support is present (none today).
10. **Layering.** No live ECS. No vendor SDK. Backend-specific binding lives under `src/graphics/<backend>/`.
11. **Long-horizon flag.** Explicitly record this task as long-horizon; no implementation child slices are opened until backend support and at least one consumer recipe exist.

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales.
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice. Long-horizon flag applies: implementation children remain unopened until backend support and at least one consumer recipe exist.

## Implementation child slices (named, not opened)
- **GRAPHICS-054-Impl-A** — `IWorkGraphDevice` interface + null-RHI mock + capability tests.
- **GRAPHICS-054-Impl-B** — Recipe slot reservation + null-RHI shape tests.
- **GRAPHICS-054-Impl-C** — Backend wiring (Vulkan equivalent or D3D12; opens when backend support lands).
- **GRAPHICS-054-Impl-D** — Operational-gate extension in `GRAPHICS-033` reason enum + diagnostic wiring.

## Tests
- Planning slice: validators only.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/graphics.md` RHI capability section with the long-horizon flag.
- Update `src/graphics/rhi/README.md` capability surface.

## Acceptance criteria
- Eleven decisions are recorded with explicit answers and trade-off rationales.
- Implementation child slices are identified but not opened.
- Long-horizon flag is recorded; no implementation timing is committed.
- Engine compiles and runs without work-graph-capable backends.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No premature implementation before backend support and a consumer recipe exist.
- No D3D12 backend stand-up here.
- No vendor SDK imports.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
