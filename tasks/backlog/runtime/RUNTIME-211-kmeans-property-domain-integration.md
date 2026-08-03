---
id: RUNTIME-211
theme: I
depends_on: [RUNTIME-196, HARDEN-087]
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
# RUNTIME-211 — K-Means property-domain integration

## Goal

- Make the canonical CPU/Vulkan K-Means operation consume any caller-selected
  finite `vec3` property on a resolved geometry element domain and publish
  labels/colors/scalars back to that same domain.

## Non-goals

- No clustering objective, initialization, backend, or numerical change.
- No geometry conversion, count change, or parallel clustering service.
- No ImGui work; `UI-043` owns property-aware discovery.

## Context

- `ClusteringModule` already carries `GeometryPropertyRef` values, but
  `IsExecutionDomain`, property resolution, publication, and diagnostics admit
  only mesh vertices, graph nodes, and point-cloud points.
- Lloyd's least-squares quantization (DOI `10.1109/TIT.1982.1056489`), Arthur
  and Vassilvitskii's k-means++ seeding, and Bahmani et al.'s scalable
  k-means++ formulate the objective over vectors, not mesh provenance or
  vertex handles. Re-read those sources and record selection/exclusion before
  implementation; do not change the landed initializer/backend policy here.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | One finite, non-empty `Property<vec3>` plus K-Means parameters. |
| Compatible entity sources | Every mesh vertex/edge/halfedge/face, graph node/edge/halfedge, or point-cloud point property satisfying the typed contract. |
| RuntimeModule | Extend the sole `ClusteringModule`/`ClusteringService`; no second path. |
| Config/agent | Preserve the validated clustering config/command path and property identities. |
| UI | `UI-043` adds catalog-backed input/output selection under appropriate provenance menus. |
| Publication | Same-cardinality named label/color/scalar properties on the originating element domain; topology and unrelated properties remain unchanged. |
| End-to-end tests | CPU and Vulkan/fallback source matrix, stale validation, property publication/history, config/direct/UI parity. |

## Required changes

- [ ] Replace the three-domain switches with canonical property-set resolution
      for all eight logical element domains; carry the selected refs through
      CPU/GPU job scopes, diagnostics, staleness, and completion.
- [ ] Publish only the named, count-matched output cohort on the originating
      domain and preserve unknown/custom properties plus mesh/graph topology.
- [ ] Reuse one property-aware availability result for direct, config/agent,
      and later UI callers.
- [ ] Preserve backend identity, GPU fallback, history, and exact CPU/Vulkan
      parity behavior.

## Tests

- [ ] Parameterize CPU execution over every physical property-domain family,
      including mesh face centers, and compare labels for identical values.
- [ ] Cover Vulkan/fallback requests without changing domain eligibility.
- [ ] Prove topology/custom-property preservation, stale rejection, and exact
      apply/undo/redo for named outputs.

## Docs

- [ ] Update clustering runtime and method-integration docs with the property
      source/publication matrix and reviewed literature.

## Acceptance criteria

- [ ] K-Means eligibility depends only on its typed input/output property
      contract, never provenance or `VertexProperty`.
- [ ] CPU, GPU, config, agent, and UI callers converge on one operation.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'Clustering|KMeans' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No converter, provenance rewrite, handle-specific eligibility, duplicate
  clustering service, or backend/numerical substitution.
