---
id: UI-038
theme: I
depends_on: [RUNTIME-208]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "Codex-UI038"
branch: "ui/ui-038-progressive-poisson-panel"
worktree: "/tmp/intrinsic-ui038.h8R1Tp"
claimed_at: "2026-08-03T07:00:37Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# UI-038 — Progressive Poisson multi-domain panel

## Status

- In progress on 2026-08-03. The slice reuses the `RUNTIME-208` typed
  operation/readiness model and existing domain-panel registration; it adds no
  app-owned method state, backend seam, or geometry conversion.

## Goal

- Present the corrected non-destructive Progressive Poisson operation under
  Mesh, Graph, and PointCloud processing, using one runtime config/readiness
  model and visualizing source-cardinality result channels.

## Non-goals

- No method/runtime/config implementation; `RUNTIME-208` owns those contracts.
- No converter, surface-sampling workflow, destructive confirmation modal, or
  UI-owned geometry/history state.
- No backend control not already implemented and exposed by runtime.

## Context

- This task supersedes the former conversion-confirmation plan. A mesh already
  exposes the `Vertices` point source required by the method; converting it to a
  point cloud was the defect, not a workflow to secure.
- Re-check the Progressive Poisson working draft and relevant progressive/GPU
  Poisson sampling literature used by `RUNTIME-208` before finalizing labels so
  the panel describes input ordering rather than surface sample generation.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Finite vertex positions. |
| Compatible entity sources | Mesh vertices, graph nodes, and point-cloud points. |
| RuntimeModule | Consume only the typed operation/readiness delivered by `RUNTIME-208`. |
| Config/agent | Edit/apply the same validated runtime config used outside the UI. |
| UI | Register one feature-owned panel model under all three geometric domains. |
| Publication | Display and bind same-cardinality rank/level/radius properties; never request conversion. |
| End-to-end tests | App integration covers all menu paths, shared config, readiness diagnostics, run, publication, and visualization. |

## Required changes

- [ ] Register Progressive Poisson under Mesh, Graph, and PointCloud Processing
      using the established domain-panel pattern and one shared typed state.
- [ ] Remove surface-sample count/seed/triangle-area/normal interpolation and
      conversion language; render only fields accepted by `RUNTIME-208`.
- [ ] Drive enabled state and disabled-reason tooltips from copied runtime
      readiness, including invalid/missing vertex properties and backend state.
- [ ] Submit the same validated config/apply/run request used by agents and show
      method diagnostics plus published channel semantics.
- [ ] Reuse existing scalar/property visualization controls for rank, level,
      acceptance, and radius without app-owned publication or mutation.

## Tests

- [ ] Extend Sandbox registration tests to assert all three stable menu paths
      and no destructive-conversion/surface-sampling controls.
- [ ] Exercise the panel with mesh, graph, and point-cloud selections and prove
      identical config/run routing plus source-correct status.
- [ ] Cover disabled reasons and result-channel visualization bindings without
      topology/cardinality changes.

## Docs

- [ ] Update Sandbox docs/screenshots and remove the obsolete conversion-safety
      description from UI/runtime backlog indexes.

## Acceptance criteria

- [ ] A loaded mesh, graph, or point cloud can discover and run Progressive
      Poisson from its appropriate domain menu.
- [ ] The panel contains no converter path and cannot discard topology.
- [ ] UI/config/agent callers use one validated runtime operation.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No mesh-to-point-cloud conversion, modal confirmation, app-local config,
  direct ECS mutation, or duplicated per-domain method implementation.
