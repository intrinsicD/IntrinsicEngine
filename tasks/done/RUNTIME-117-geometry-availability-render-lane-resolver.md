---
id: RUNTIME-117
theme: F
depends_on: [HARDEN-083, RUNTIME-106, RUNTIME-111]
maturity_target: CPUContracted
---
# RUNTIME-117 — Geometry availability and render-lane resolver

## Completion
- Retired on 2026-06-19 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: runtime now has `Extrinsic.Runtime.GeometryAvailability`, a
  standard resolver for CPU source capabilities, provenance, property-domain
  support, and `Surface`/`Edges`/`Points` render-lane readiness from
  `GeometrySources` plus promoted render components.
- Evidence: `cmake --build --preset ci --target IntrinsicRuntimeContractTests
  IntrinsicECSTests` succeeded, and focused CPU/null coverage for
  `GeometrySources`, geometry availability, packers, extraction, progressive
  data, and `SandboxEditorUi` passed 205/205 tests.

## Goal
- Add the runtime-owned standard resolver that combines ECS CPU source availability with render component presence, so algorithms, progressive data, extraction, and UI can ask for the data or render lane they need without using `ActiveDomain` as a capability gate.

## Non-goals
- No GPU residency snapshot or buffer-handle reporting; `RUNTIME-119` owns that.
- No Sandbox editor UI migration; `UI-021` owns editor consumption.
- No graphics-layer ECS reads.
- No new geometry algorithm backend or async execution queue.
- No scene serialization format changes unless a migrated component already requires them.

## Context
- Owning subsystem/layer: `runtime` owns live ECS composition, render extraction preflight, editor command routing, and algorithm/source-provider validation.
- `HARDEN-083` defines the ECS CPU source-availability contract. This task consumes it with graphics render components (`RenderSurface`, `RenderEdges`, `RenderPoints`) that are data-only ECS components.
- `RUNTIME-106` retired render component domain composition, but several consumers still use `GeometrySources::ActiveDomain` for availability decisions.
- `Runtime.ProgressiveRenderData` has useful vocabulary (`ProgressiveGeometryDomain`, `ProgressiveRenderLane`, property binding helpers), but those helpers currently treat active domain as capability.
- This resolver is the standard runtime seam for "does this selected entity provide the CPU data I need?" and "is this render lane requested and supportable?".

## Required changes
- [x] Audit runtime call sites that inspect `GeometrySources::ConstSourceView::ActiveDomain`, `VertexSource`, `EdgeSource`, `HalfedgeSource`, `FaceSource`, or `NodeSource`, and classify each as provenance, CPU capability, render-lane availability, or exact-domain validation.
- [x] Add or promote a neutral runtime availability model that consumes the `HARDEN-083` ECS source-availability contract and reports CPU needs such as point positions, edge endpoints, halfedge topology, faces, and property domains.
- [x] Add render-lane availability resolution for `Surface`, `Edges`, and `Points` from `RenderSurface`, `RenderEdges`, `RenderPoints`, and the CPU source capabilities required by each lane.
- [x] Emit deterministic diagnostics for unsupported or missing-data cases: no render component, missing point source, missing edge endpoints, missing halfedges, missing faces, unsupported surface request, and unsupported property domain.
- [x] Provide a small public runtime API suitable for UI, runtime extraction, and algorithm command preflight; avoid UI-specific names and avoid exposing raw mutable ECS state.
- [x] Keep provenance visible in resolver results so callers can still distinguish "mesh vertices rendered as points" from "standalone point cloud points".
- [x] Document which checks remain exact-domain validation, for example algorithms that truly require a full halfedge mesh.

## Tests
- [x] Add `contract;runtime` coverage for mesh entities with independent `RenderSurface`, `RenderEdges`, and `RenderPoints` lane requests.
- [x] Add `contract;runtime` coverage for graph entities with edge and point lane requests, including missing node/edge sources.
- [x] Add `contract;runtime` coverage for point-cloud entities proving point lane support and deterministic surface/edge rejection.
- [x] Add coverage for partial mesh-like entities proving edge/point consumers can proceed when their data exists while halfedge/face consumers fail closed.
- [x] Add property-domain resolver coverage proving property options are selected by CPU source capability rather than only exact active domain.

## Docs
- [x] Update `src/runtime/README.md` to name the resolver as the standard runtime availability seam.
- [x] Update `tasks/backlog/runtime/README.md` with this task and its follow-ups.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening this task.
- [x] Regenerate `docs/api/generated/module_inventory.md` if a runtime module surface changes.

## Acceptance criteria
- [x] Runtime has one documented resolver for CPU geometry availability and render-lane availability.
- [x] Callers can ask for the data they need without reimplementing `Vertices` / `Nodes` / `Edges` / `Halfedges` / `Faces` checks.
- [x] Render-lane support is determined from render component presence plus required CPU sources, not from exact `ActiveDomain` alone.
- [x] Resolver results preserve provenance so mesh-derived point/edge work remains distinguishable from graph or point-cloud work.
- [x] Missing data produces explicit diagnostics and fail-closed behavior.
- [x] No promoted graphics code imports live ECS or runtime-owned resolver state.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryAvailability|ProgressiveRenderData|RenderExtraction|SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Moving geometry algorithms, render resources, or GPU handles into ECS.
- Making UI the owner of availability policy.
- Letting graphics read live ECS state.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
- This task closes the runtime CPU/render-lane availability contract. Existing runtime consumers migrate under `RUNTIME-118`; GPU availability is owned by `RUNTIME-119`.
