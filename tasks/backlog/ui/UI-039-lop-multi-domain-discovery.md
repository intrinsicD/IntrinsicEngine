---
id: UI-039
theme: I
depends_on: [RUNTIME-206, UI-035]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# UI-039 — LOP family multi-domain discovery

## Goal

- Make the LOP/WLOP/CLOP/EAR panel discoverable and operational for compatible
  Mesh, Graph, and PointCloud vertex sources through the corrected
  `RUNTIME-206` operation.

## Non-goals

- No kernel, runtime service, config schema, converter, or topology-editing
  implementation.
- No duplicated per-domain config or panel logic.

## Context

- `UI-035` supplies the initial PointCloud-only panel. The LOP-family papers
  operate on point sets; `RUNTIME-206` separates that input contract from
  topology-safe result publication.
- Reuse the literature audit recorded by `RUNTIME-206` when naming strategies,
  normal requirements, and count-changing restrictions.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Vertex positions, with optional normals by strategy. |
| Compatible entity sources | Mesh vertices, graph nodes, and point-cloud points. |
| RuntimeModule | Consume `RUNTIME-206` availability/config/submit/result records. |
| Config/agent | Panel edits the shared validated consolidation config only. |
| UI | Register consistent Mesh, Graph, and PointCloud Processing entries backed by one state/model. |
| Publication | Explain same-cardinality in-place publication and disable count-changing mesh/graph requests from runtime readiness. |
| End-to-end tests | Menu registration, readiness, config/run routing, diagnostics, and visualization for all domains. |

## Required changes

- [ ] Refactor the existing consolidation panel registration to expose stable
      menu entries under all three compatible domains with shared state.
- [ ] Use runtime-provided source capability and publication readiness; for
      mesh/graph selections disable invalid target-count requests with the
      exact shared diagnostic rather than hiding the method.
- [ ] Preserve one validated config/apply/submit path and render strategy,
      convergence, normal, backend, history, and stale-result diagnostics.
- [ ] Keep visualization and selection on the originating entity/domain.

## Tests

- [ ] Assert all three registered menu paths open the consolidation panel and
      share config state.
- [ ] Exercise successful same-cardinality runs from mesh, graph, and point
      cloud plus disabled count-changing mesh/graph requests.
- [ ] Verify the app performs no direct ECS mutation or conversion and displays
      runtime failure/staleness diagnostics.

## Docs

- [ ] Update Sandbox/runtime method UI documentation with the three-domain
      discovery and publication matrix.

## Acceptance criteria

- [ ] Loading a mesh or graph exposes the LOP-family panel without conversion.
- [ ] Every domain drives the same runtime config and typed operation.
- [ ] UI cannot request topology-destructive publication.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'PointCloudConsolidation|MethodPanel' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No PointCloud-only menu gate, converter, app-owned readiness/config, direct
  vertex mutation, or separate panel implementation per domain.
