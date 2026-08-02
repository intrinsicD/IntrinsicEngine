---
id: UI-041
theme: I
depends_on: [RUNTIME-209]
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
# UI-041 — Point-set outlier multi-domain panel

## Goal

- Replace the PointCloud-only “Remove Outliers” control with a shared
  mesh/graph/point-cloud analysis panel: Detect publishes an outlier mask on
  every vertex source, while explicit Remove is offered only for point clouds.

## Non-goals

- No estimator/runtime/config implementation or topology-aware mesh/graph
  deletion.
- No automatic deletion after detection and no geometry conversion.

## Context

- `RUNTIME-209` separates method analysis from ownership-changing publication.
  UI language must preserve the same distinction and the limitations found in
  its SOR/ROR literature review.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Vertex positions. |
| Compatible entity sources | Mesh, graph, and point cloud for Detect; point cloud only for Remove. |
| RuntimeModule | Consume `RUNTIME-209` config/readiness/analyze/remove/result paths. |
| Config/agent | Edit and apply the shared validated outlier config. |
| UI | Register the analysis panel under all three Processing domains with capability-specific actions. |
| Publication | Visualize same-count mask/score; label point-cloud compaction as a separate destructive history command. |
| End-to-end tests | Three-domain discovery/detection, visualization, point-cloud removal, disabled reasons, and undo/redo. |

## Required changes

- [ ] Register one feature panel under Mesh, Graph, and PointCloud Processing
      with shared method/parameter state.
- [ ] Rename the primary action to Detect Outliers and display mask/score and
      diagnostics through existing property visualization controls.
- [ ] Show Remove Marked Points only when runtime readiness allows
      topology-free point-cloud compaction; require a separate explicit action
      and show its count/history consequences.
- [ ] Use runtime config, readiness, submit, and result records exclusively.

## Tests

- [ ] Assert all three menu registrations and Detect routing with shared config.
- [ ] Cover mask visualization, missing/invalid source diagnostics, explicit
      point-cloud Remove, and absence/disabled state for mesh/graph Remove.
- [ ] Verify no detection action mutates cardinality and undo/redo remains
      runtime-owned.

## Docs

- [ ] Update Sandbox outlier workflow docs and menu inventory.

## Acceptance criteria

- [ ] Every compatible vertex source can discover and run outlier detection.
- [ ] Only point clouds expose explicit removal; mesh/graph topology is safe.
- [ ] UI and agent callers share config, readiness, and operations.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'Outlier' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No detect-and-delete default, mesh/graph compaction, converter, UI-owned
  property publication, or independent per-domain configs.
