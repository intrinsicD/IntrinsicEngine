---
id: RUNTIME-209
theme: I
depends_on: [HARDEN-087]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-209 — Point-set outlier analysis and publication split

## Goal

- Separate statistical/radius outlier detection from destructive removal so
  every caller-selected finite `vec3` property on a resolved element domain
  can publish a same-cardinality outlier mask while explicit cardinality
  reduction remains restricted to topology-free point clouds.

## Non-goals

- No new outlier estimator, topology-aware mesh/graph deletion, conversion, or
  general geometry filtering framework.
- No ImGui implementation; `UI-041` owns the three-domain panel behavior.

## Context

- The current command rejects mesh/graph provenance and immediately compacts a
  point cloud. Its statistical and radius kernels first compute rejected input
  indices, which are valid analysis results for any typed sample property,
  including mesh face centers.
- Review Rusu et al.'s statistical neighborhood formulation (DOI
  `10.1016/j.robot.2008.08.005`) and the official PCL behavior references for
  StatisticalOutlierRemoval
  (`https://pointclouds.org/documentation/classpcl_1_1_statistical_outlier_removal.html`)
  and RadiusOutlierRemoval
  (`https://pointclouds.org/documentation/classpcl_1_1_radius_outlier_removal.html`)
  before editing. Search density-aware successors during the task's literature
  intake and record stable identities plus selection/exclusion rationale, but
  do not silently change the two existing estimators. Preserve detection vs
  deletion as an explicit engine distinction.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | A caller-selected finite `Property<vec3>` plus neighborhood parameters. |
| Compatible entity sources | Every resolved mesh/graph/point-cloud element domain for detection; point-cloud points only for destructive compaction. |
| RuntimeModule | Extend the existing geometry-processing outlier command/job path with explicit Analyze and Remove modes. |
| Config/agent | Add one validated point-set-outlier config used by UI, agents, and direct commands. |
| UI | `UI-041` provides Detect on all domains and explicit Remove only for point clouds. |
| Publication | Analyze writes named same-domain mask and score/diagnostic properties; Remove compacts only point-cloud `Vertices` through history. |
| End-to-end tests | All property-domain families including face centers, point-cloud-only removal, config parity, async staleness, and UI readiness tests. |

## Required changes

- [ ] Introduce an explicit operation mode and property-aware capability
      record; Analyze resolves a canonical `GeometryPropertyRef` on any element
      domain, while Remove additionally requires topology-free point-cloud
      provenance and canonical point properties.
- [ ] Publish deterministic same-cardinality mask and available score data to
      the originating property domain without changing topology, element
      order, or unrelated properties.
- [ ] Preserve point-cloud removal as a separate undoable mutation consuming
      the analysis result/current generation; fail closed for graph/mesh Remove
      with an actionable shared diagnostic.
- [ ] Add one serializable preview/validate/apply config section for method and
      parameters, reused by direct, agent, and UI requests.
- [ ] Keep queued completion generation-bound and use existing geometry
      operation/history abstractions rather than a new service.

## Tests

- [ ] Parameterize statistical and radius Analyze over every physical
      property-domain family, including mesh face centers, and compare
      masks/diagnostics for identical values.
- [ ] Verify Analyze preserves all topology/cardinality and undo/redo restores
      target properties exactly.
- [ ] Verify Remove succeeds and round-trips only for point clouds; graph/mesh
      fail before queueing with no dirty/history mutation.
- [ ] Add config round-trip, control-surface parity, and stale async completion
      coverage.

## Docs

- [ ] Update runtime/config and method notes with detection/removal semantics,
      literature references, property names, and domain matrix.

## Acceptance criteria

- [ ] Outlier detection is available for every valid typed sample property.
- [ ] Destructive removal is explicit and cannot damage graph/mesh topology.
- [ ] All callers share the same config, readiness, diagnostics, and history
      behavior.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'Outlier' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No mesh/graph compaction, hidden conversion, combined detect-and-delete
  default, UI-only mode selection, or silent property loss.
