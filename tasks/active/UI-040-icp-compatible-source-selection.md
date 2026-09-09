---
id: UI-040
theme: I
depends_on: [RUNTIME-207]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: codex-interactive
branch: main
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-08T14:06:03.847241+00:00"
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

## Spatial acceleration consideration

Use RUNTIME-207's shared readiness/config path for any future cached
correspondence backend. The selected target property, domain and metric must reach
runtime unchanged; UI must not build a private index or imply LBVH acceleration
before the registration consumer is wired.

See the [shared spatial-index consumer inventory](../../docs/architecture/spatial-index-consumers.md).

## Required changes

- [x] Register stable Mesh, Graph, and PointCloud Processing entries that open
      one shared ICP window; retain a View alias only if existing compatibility
      tests/users require it.
- [x] Populate both entity/property selectors from runtime catalogs and display
      provenance, element-domain, value-count, normal readiness, transform
      readiness, and exact disabled reasons.
- [x] Route parameter edits through the shared runtime config preview/apply path
      and submit only the typed registration command.
- [x] Preserve trajectory scrubbing and source-transform undo/redo without
      app-owned copies of geometry or method state.

## Tests

- [x] Assert all domain menu entries, every provenance pairing, and
      representative vertex/edge/halfedge/face property selector options.
- [x] Cover point-to-point success for mixed domains, point-to-plane normal
      readiness, same-entity rejection, and stale/missing entity diagnostics.
- [x] Verify config/agent/UI parity and source-transform-only undo/redo.

## Docs

- [x] Update Sandbox registration documentation with compatible sources,
      variant requirements, and transform-only publication.

## Acceptance criteria

- [x] Any compatible typed property on mesh, graph, or point-cloud entities is
      selectable as either ICP operand.
- [x] Panel availability and diagnostics exactly match runtime readiness.
- [x] No converter or UI-private mutation path exists.

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

## Interactive implementation note — 2026-09-08

The operator accepted the combined canonical ICP workflow and shared-index
integration proposal. This explicitly adds cached CPU LBVH and framed Vulkan
correspondence queries to the original binding/UI scope. The CPU KD-tree default
and existing CPU solve remain. No GICP, Trimmed ICP overlap estimator, Anderson
acceleration, kNN or triangle query method is included. Implementation acceptance
is verified in the shared working tree. These notes remain active pending
review/commit. The design is documented in
[registration](../../docs/architecture/registration.md).

## Verification record — 2026-09-08

- CPU cohort: 4,349 selected; 4,348 passed with host display access, no
  failures, and one expected unsanitized LeakSanitizer-control skip.
- Two Vulkan/ASan/UBSan readback tests passed, including framed point-to-point
  and point-to-plane registration with same-variant CPU references.
- CPU and runtime GPU comparison output is schema-v2 validated, dirty-source,
  and non-claim-eligible. The default remains CPU KD-tree.
- Canonical 8-by-8 domain pairs, live-row mapping, normal preflight, stale
  binding/deletion rejection, source-only undo/redo, config preview/apply/run,
  and one shared window behind the domain menu entries are covered.
- [Review and limitations](../../docs/reviews/2026-09-08-icp-spatial-integration.md).
