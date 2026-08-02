---
id: RUNTIME-206
theme: I
depends_on: [RUNTIME-175, HARDEN-087]
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
# RUNTIME-206 — LOP family element-domain source integration

## Goal

- Refactor the landed LOP/WLOP/CLOP/EAR runtime operation to consume the
  canonical `Vertices` source of mesh, graph, and point-cloud entities and to
  publish results without converting or discarding richer topology.

## Non-goals

- No new consolidation strategy, numerical change, optimized/GPU backend, or
  mesh/graph-to-point-cloud converter.
- No ImGui implementation; `UI-039` owns multi-domain discovery after this
  runtime contract is proven.
- No count-changing mutation of graph or mesh vertices.

## Context

- `RUNTIME-175`/`UI-035` initially integrate the family around exact
  point-cloud provenance. The public consolidation kernels operate on point
  positions; mesh vertices and graph nodes therefore satisfy the same input
  contract.
- Re-read Lipman et al. (LOP, DOI `10.1145/1275808.1276405`), Huang et al.
  (WLOP), Preiner et al. (CLOP, DOI `10.1145/2601097.2601172`), and Huang et
  al. (EAR, DOI `10.1145/2421636.2421645`) before editing. Record which
  variants permit changed output cardinality; do not infer an ECS conversion
  requirement from their point-set terminology.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Finite `Vertices` positions; optional same-count normals for anisotropic variants. |
| Compatible entity sources | Mesh vertices, graph nodes, and point-cloud points. |
| RuntimeModule | Extend `PointCloudConsolidationModule`/typed geometry operation; do not add a parallel service. |
| Config/agent | Preserve the validated consolidation config lane; reject topology-bearing count changes with one shared diagnostic. |
| UI | `UI-039` registers the same panel model under Mesh, Graph, and PointCloud. |
| Publication | Same-cardinality position/normal properties update their originating `Vertices`; count-changing output is allowed only for topology-free point clouds. |
| End-to-end tests | Runtime contracts cover all three sources, topology preservation, config parity, async staleness, and undo/redo; `UI-039` covers discovery. |

## Required changes

- [ ] Replace exact point-cloud provenance checks with canonical `Vertices`
      availability/extraction and carry the originating source domain through
      job scope, stale-result validation, diagnostics, and history.
- [ ] Reuse the existing consolidation service/config/operation; make source
      capability and publication policy explicit plain data rather than adding
      a converter, facade, or second runtime module.
- [ ] Preserve mesh faces/edges/halfedges and graph edges/halfedges when output
      cardinality matches the source, updating only count-matched vertex
      properties and dirty/generation state.
- [ ] Fail closed before submission when a mesh/graph request would change
      vertex cardinality; report the same reason through UI, config/agent, and
      direct runtime callers. Point-cloud cardinality changes retain exact
      undo/redo.
- [ ] Keep async completion generation-bound to the original element source and
      discard stale results without partial publication or history entries.
- [ ] Update availability snapshots so all compatible sources can discover the
      operation without changing provenance metadata.

## Tests

- [ ] Add parameterized runtime contracts that run a same-cardinality strategy
      from mesh, graph, and point-cloud `Vertices` and verify identical kernel
      inputs plus source-correct publication.
- [ ] Prove mesh/graph topology and non-target properties survive apply,
      undo, and redo byte-for-byte.
- [ ] Prove topology-bearing count changes fail before work is queued while an
      equivalent point-cloud request remains supported.
- [ ] Cover stale async completion and config/agent/direct-command parity for
      all three source domains.

## Docs

- [ ] Update runtime and Sandbox method-integration docs with the source and
      publication matrix; update method package notes if the literature audit
      changes a declared variant contract.

## Acceptance criteria

- [ ] LOP-family eligibility depends on a usable `Vertices` source, not exact
      point-cloud provenance.
- [ ] Mesh and graph entities retain topology; no implicit conversion exists.
- [ ] Every control surface uses one validated config/operation path and every
      compatible source has executable runtime proof.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'PointCloudConsolidation|GeometryAvailability' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No mesh/graph-to-point-cloud conversion, topology deletion, provenance
  rewriting, silent vertex reindexing, or UI-private execution path.
- No count-changing graph/mesh publication without a separately reviewed
  topology-editing contract.
