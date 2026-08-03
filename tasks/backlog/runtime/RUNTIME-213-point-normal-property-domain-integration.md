---
id: RUNTIME-213
theme: I
depends_on: [GEOM-026, HARDEN-087]
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
# RUNTIME-213 — Point-set normal property-domain integration

## Goal

- Expose the geometry point-set normal estimator for any selected finite
  `vec3` property on any resolved element domain, while retaining the distinct
  mesh- and graph-topology normal methods where topology is genuinely required.

## Non-goals

- No replacement of mesh face-weighted or graph connectivity-aware normal
  kernels, learned estimator, or numerical-policy change.
- No converter or requirement that a point-set sample be a vertex.
- No UI implementation; `UI-045` owns method/property selection.

## Context

- The current editor advertises normals only for mesh vertices, graph nodes,
  and point-cloud points. `Geometry.PointCloud.Normals::Estimate` already has a
  span contract, so face centers and other typed sample properties are valid
  point-set inputs even though topology-aware alternatives remain distinct.
- Re-read Hoppe et al.'s PCA-plane normal/orientation construction (DOI
  `10.1145/133994.134011`) and Mitra–Nguyen's noise/curvature/density-aware
  neighborhood analysis before implementation. Record later robust/learned
  estimators as excluded variants rather than changing the landed oracle.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Point-set variant: finite `Property<vec3>`; mesh/graph variants: their explicit topology plus named properties. |
| Compatible entity sources | Point-set estimator on every element domain; topology-aware mesh/graph methods only where their named adjacency exists. |
| RuntimeModule | Extend the existing normal command/job/history path and availability model. |
| Config/agent | One validated method selection and parameter path, including property refs. |
| UI | `UI-045` distinguishes point-set from topology-aware methods and lists compatible properties. |
| Publication | Named same-cardinality normal property on the originating domain; no topology or input mutation. |
| End-to-end tests | Property-domain point estimator, topology-aware method gating, staleness/history/config/UI parity. |

## Required changes

- [ ] Model normal-method requirements explicitly instead of deriving them
      from entity provenance or a `VertexProperty` type.
- [ ] Resolve point-set input/output refs on every element domain and reuse the
      canonical availability/property catalog.
- [ ] Keep mesh/graph topology-aware paths and their diagnostics distinct while
      routing all publication through one named-property transaction shape.

## Tests

- [ ] Run the point-set estimator over each physical property-domain family,
      including face centers, and compare identical inputs.
- [ ] Prove topology-aware variants require only their documented adjacency,
      preserve unrelated/custom properties, and publish/undo/redo exactly.
- [ ] Cover config parity and stale input/output property rejection.

## Docs

- [ ] Update normal method/runtime docs with the explicit method-to-input
      matrix and reviewed original/extensions literature.

## Acceptance criteria

- [ ] Point-set normal estimation never requires point-cloud provenance or a
      vertex property.
- [ ] Stronger mesh/graph algorithms remain available only by their real
      topology contract, not by menu identity.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'Normal' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No method conflation, converter, provenance-only gate, handle-specific point
  estimator, or unreviewed numerical substitution.
