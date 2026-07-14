# GRAPHICS-054 — Work graphs RHI extension (IWorkGraphDevice) (planning, long-horizon)

- Status: completed (2026-06-03; planning-only; `Scaffolded`; long-horizon).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Per the long-horizon flag, implementation children stay unopened until both backend support (Vulkan work-graph equivalent or a D3D12 backend) and at least one consumer recipe exist.

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

## Recorded decisions
1. **Capability surface.** `IWorkGraphDevice` is fetched via `IDevice::QueryInterface<IWorkGraphDevice>()`, returning `nullptr` until both backend support and engine integration are present. Rationale: reusing the capability-query seam keeps work graphs purely additive (mirroring `GRAPHICS-045`/`GRAPHICS-053`), and returning `nullptr` until both backend and integration exist matches the long-horizon posture — the surface can be designed now without any backend pretending to support it.
2. **Node kinds.** Lock the supported node kinds: `BroadcastingLaunchNode`, `CoalescingLaunchNode`, `ThreadLaunchNode`, and `MeshNode` (gated on `GRAPHICS-053`), with all node kinds optional and absence silently handled. Rationale: these are the canonical D3D12 work-graph node kinds, so adopting them verbatim avoids a translation layer when a backend lands; gating `MeshNode` on mesh-shader support and treating every kind as optional keeps the surface usable on partial implementations.
3. **Node payload shape.** Each node declares an input record type and emits typed payloads to downstream nodes, with a recorded payload size cap and typing rule. Rationale: typed records are the work-graph data-flow contract; pinning a size cap and typing rule up front lets the API and its null-RHI mock be defined deterministically before any backend exists, and keeps payloads bounded for backing-memory sizing.
4. **Backing storage.** Work graphs require a backing memory allocation (D3D12: `WorkGraphBackingMemory`), with the sizing query and lifecycle recorded. Rationale: GPU-scheduled graphs need scratch memory whose size the backend reports, so exposing a sizing query + explicit lifecycle (rather than a hidden allocation) keeps the cost visible and the allocation under the same ownership discipline as other GPU resources.
5. **Scheduler determinism.** Forbid relying on inter-node ordering beyond the explicit data dependencies, with the rule recorded. Rationale: GPU schedulers reorder freely, so any code assuming node execution order beyond declared data edges would be non-portable and racy; forbidding it up front keeps consumer recipes correct across backends and scheduler revisions.
6. **Diagnostics.** `WorkGraphNodesDispatchedHistogram[NodeId]` and `WorkGraphBackingMemoryBytes` are atomic counters. Rationale: the per-node dispatch histogram surfaces graph behavior (which nodes fire, how often) and the backing-memory counter surfaces the scratch cost — the two signals needed to understand a GPU-scheduled workload, without strings.
7. **Recipe slots.** Reserve a `RecipeKind::WorkGraphScheduler` opt-in; no production recipes use it until a consumer opens. Rationale: reserving the recipe slot now lets the selection plumbing be shaped without activating it, so a future consumer (e.g. continuous-LOD) drops in without renumbering or reworking recipe selection.
8. **Operational-gate addition.** Append "Work graph capabilities probed and recorded" to `GRAPHICS-033`'s reason enum without rewriting earlier gates; the gate stays `NotRequested` until a consumer opens. Rationale: the gate enum is append-only by contract, so work graphs join as a new optional gate that stays inert until requested, preserving every prior gate's meaning and the long-horizon "no commitment" stance.
9. **Test split.** `contract;graphics` for the capability surface and recipe-slot reservation under null-RHI mocks; opt-in `gpu;<backend>` smoke when backend support is present (none today). Rationale: the surface and reservation are fully checkable under null RHI even with no backend, so the planning contract is testable now, and the `gpu` smoke is reserved (not written) until a backend exists — keeping the default gate green.
10. **Layering.** No live ECS, no vendor SDK, and backend-specific binding lives under `src/graphics/<backend>/`. Rationale: preserves AGENTS.md §2 — the RHI capability stays backend-agnostic, backend wiring is confined to the backend directory, and no middleware enters promoted layers.
11. **Long-horizon flag.** Explicitly record this task as long-horizon; no implementation child slices are opened until backend support and at least one consumer recipe exist. Rationale: D3D12 work graphs are production but the engine has no D3D12 backend and the Vulkan equivalent is still in flight, so committing implementation timing now would be speculative — recording the flag captures the API shape while gating implementation on real prerequisites.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice. Long-horizon flag applies: implementation children remain unopened until backend support and at least one consumer recipe exist.

## Implementation child slices (named, not opened)
- **GRAPHICS-054-Impl-A** — `IWorkGraphDevice` interface + null-RHI mock + capability tests.
- **GRAPHICS-054-Impl-B** — Recipe slot reservation + null-RHI shape tests.
- **GRAPHICS-054-Impl-C** — Backend wiring (Vulkan equivalent or D3D12; opens when backend support lands).
- **GRAPHICS-054-Impl-D** — Operational-gate extension in `GRAPHICS-033` reason enum + diagnostic wiring.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The RHI capability (long-horizon-flagged) section of `docs/architecture/graphics.md` is deferred to the implementation children (`GRAPHICS-054-Impl-A/B`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The capability-surface section of `src/graphics/rhi/README.md` is deferred to the same implementation children for the same reason.

## Acceptance criteria
- [x] Eleven decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Long-horizon flag is recorded; no implementation timing is committed.
- [x] Engine compiles and runs without work-graph-capable backends.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` long-horizon slice. All eleven work-graph RHI decisions are recorded with explicit answers and trade-off rationales: the `QueryInterface`-fetched optional `IWorkGraphDevice` returning `nullptr` until backend + integration exist, the four D3D12-canonical node kinds with `MeshNode` gated on `GRAPHICS-053` and all kinds optional, the typed-record payload contract with a size cap, the backing-memory sizing query + lifecycle, the determinism rule forbidding reliance on inter-node ordering beyond data edges, the per-node dispatch histogram + backing-memory counter, the reserved `RecipeKind::WorkGraphScheduler` opt-in, the append-only `GRAPHICS-033` gate addition staying `NotRequested`, the null-RHI-contract + reserved `gpu;<backend>` test split, the backend-confined no-vendor-SDK layering, and the explicit long-horizon flag. Implementation children `GRAPHICS-054-Impl-A..D` are identified but not opened; per the long-horizon flag no implementation timing is committed, no Vulkan/D3D12 enables land, and the engine compiles/runs without work-graph backends. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No premature implementation before backend support and a consumer recipe exist.
- No D3D12 backend stand-up here.
- No vendor SDK imports.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
