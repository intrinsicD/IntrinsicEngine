---
id: UI-040
theme: I
depends_on: [RUNTIME-207]
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
# UI-040 — ICP compatible-source selection and discovery

## Goal

- Let users discover ICP from Mesh, Graph, and PointCloud contexts and select
  any compatible source/target entity plus typed property pair using the
  validated `RUNTIME-207` config, readiness, and transform-only command path.

## Non-goals

- No ICP kernel/runtime/config implementation, geometry conversion, or UI-owned
  transform/history mutation.
- No source/target restriction to matching provenance.

## Context

- The current top-level ICP window filters both operands to point-cloud
  entities. ICP literature specifies point/normal inputs, so menu context and
  ECS provenance must not narrow compatible vertex sources.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Two finite selected `vec3` properties; a same-domain target-normal property for normal-dependent variants. |
| Compatible entity sources | Every pair of resolved mesh/graph/point-cloud element-domain properties. |
| RuntimeModule | Consume `RUNTIME-207` entity options, readiness, config, submit, trajectory, and transform history. |
| Config/agent | Panel edits/applies the same validated ICP config. |
| UI | Add appropriate domain menu entries opening one shared cross-domain registration window. |
| Publication | Show source-transform-only consequences; never mutate geometry or target transform. |
| End-to-end tests | Domain menu discovery, cross-provenance and non-vertex property selection/readiness, config parity, run/trajectory, undo/redo. |

## Required changes

- [ ] Register stable Mesh, Graph, and PointCloud Processing entries that open
      one shared ICP window; retain a View alias only if existing compatibility
      tests/users require it.
- [ ] Populate both entity/property selectors from runtime catalogs and display
      provenance, element-domain, value-count, normal readiness, transform
      readiness, and exact disabled reasons.
- [ ] Route parameter edits through the shared runtime config preview/apply path
      and submit only the typed registration command.
- [ ] Preserve trajectory scrubbing and source-transform undo/redo without
      app-owned copies of geometry or method state.

## Tests

- [ ] Assert all domain menu entries, every provenance pairing, and
      representative vertex/edge/halfedge/face property selector options.
- [ ] Cover point-to-point success for mixed domains, point-to-plane normal
      readiness, same-entity rejection, and stale/missing entity diagnostics.
- [ ] Verify config/agent/UI parity and source-transform-only undo/redo.

## Docs

- [ ] Update Sandbox registration documentation with compatible sources,
      variant requirements, and transform-only publication.

## Acceptance criteria

- [ ] Any compatible typed property on mesh, graph, or point-cloud entities is
      selectable as either ICP operand.
- [ ] Panel availability and diagnostics exactly match runtime readiness.
- [ ] No converter or UI-private mutation path exists.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'ICP|Registration' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No exact-provenance filtering, mesh/graph conversion, target mutation,
  duplicated domain windows, or app-owned config/history truth.
