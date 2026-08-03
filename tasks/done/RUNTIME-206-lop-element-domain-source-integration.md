---
id: RUNTIME-206
theme: I
depends_on: [RUNTIME-175, HARDEN-087]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-runtime206"
branch: "runtime/runtime-206-lop-domain-sources"
worktree: "/tmp/intrinsic-runtime206.ZvbjE9"
claimed_at: "2026-08-03T18:51:47Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-206 — LOP family element-domain source integration

## Status

- Completed and retired on 2026-08-03 at `Operational`. The real asynchronous
  runtime path now resolves named finite `vec3` properties across all eight
  mesh/graph/point-cloud element domains, executes the existing CPU-reference
  LOP-family worker, and publishes same-cardinality named outputs to the
  originating `PropertySet` without topology conversion. Mesh/graph count
  changes fail before submission; canonical point-cloud replacement retains
  exact undo/redo. Focused coverage passed 13/13, the full CPU selector selected
  4,076 tests and passed 4,075 with its policy-defined GLFW/LSan skip, ASan
  passed 2,670/2,670, and UBSan passed 2,669 with the LSan-only case skipped.
  Strict architecture, task, documentation, method-manifest, inventory, ARA,
  and independent fixed-surface review evidence close the runtime correction.
  `UI-039` owns property-aware Sandbox discovery; no GPU backend or performance
  claim is made.
- Commit: pending the implementation/evidence checkpoint; the accepted review
  under `tasks/evidence/RUNTIME-206/` provides the exact source identity.

## Goal

- Refactor the landed LOP/WLOP/CLOP/EAR runtime operation to consume the
  caller-selected finite `vec3` position property on any ECS geometry element
  domain and to publish named outputs back to that same domain without
  converting or discarding richer topology. A mesh-face center property is a
  valid point-set input even though it is not a `VertexProperty`.

## Non-goals

- No new consolidation strategy, numerical change, optimized/GPU backend, or
  mesh/graph-to-point-cloud converter.
- No ImGui implementation; `UI-039` owns multi-domain discovery after this
  runtime contract is proven.
- No count-changing mutation of mesh or graph element domains.

## Context

- `RUNTIME-175`/`UI-035` initially integrate the family around exact
  point-cloud provenance. The public consolidation kernels operate on point
  positions, not vertex handles: any element-domain `Property<glm::vec3>` can
  therefore satisfy the same input contract. Vertex properties on meshes,
  graphs, and point clouds are only common defaults, not the eligibility
  boundary.
- Re-read Lipman et al. (LOP, DOI `10.1145/1275808.1276405`), Huang et al.
  (WLOP, DOI `10.1145/1618452.1618522`), Preiner et al. (CLOP, DOI
  `10.1145/2601097.2601172`), and Huang et al. (EAR, DOI
  `10.1145/2421636.2421645`) before editing. Record which
  variants permit changed output cardinality; do not infer an ECS conversion
  requirement from their point-set terminology.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | A caller-selected finite `Property<glm::vec3>` on one element-domain `PropertySet`; optional same-domain, same-count normal property for anisotropic variants. No `VertexProperty` requirement. |
| Compatible entity sources | Any resolved mesh vertex/edge/halfedge/face, graph node/edge/halfedge, or point-cloud point property satisfying the typed input contract. |
| RuntimeModule | Extend `PointCloudConsolidationModule`/typed geometry operation; do not add a parallel service. |
| Config/agent | Preserve the validated consolidation config lane; reject topology-bearing count changes with one shared diagnostic. |
| UI | `UI-039` registers the same panel model under Mesh, Graph, and PointCloud. |
| Publication | Same-cardinality named position/normal outputs update only their originating element-domain `PropertySet`; count-changing output is allowed only for the topology-free point-cloud point domain and its canonical position output. |
| End-to-end tests | Runtime contracts cover every physical element-domain family, including a mesh-face center property, topology/property preservation, config parity, async staleness, and undo/redo; `UI-039` covers property-aware discovery. |

## Required changes

- [x] Replace exact point-cloud provenance and `Vertices` checks with canonical
      `GeometryPropertyRef` resolution. Carry the selected element domain and
      named input/output properties through job scope, stale-result validation,
      diagnostics, and history.
- [x] Reuse the existing consolidation service/config/operation; make source
      capability and publication policy explicit plain data rather than adding
      a converter, facade, or second runtime module.
- [x] Preserve every unrelated property plus mesh faces/edges/halfedges and
      graph edges/halfedges when output cardinality matches the selected
      property, updating only the named output properties and dirty state.
- [x] Fail closed before submission when any topology-bearing element-domain
      request would change cardinality; report the same reason through UI,
      config/agent, and direct runtime callers. Point-cloud cardinality changes
      retain exact undo/redo and require canonical position publication.
- [x] Keep async completion generation-bound to the original element source and
      discard stale results without partial publication or history entries.
- [x] Export/reuse one property-aware availability preflight so later UI work
      can discover the operation from the property catalog without changing
      provenance metadata or rebuilding validation switches.

## Tests

- [x] Add parameterized runtime contracts that run a same-cardinality strategy
      from mesh vertex, graph node, and point-cloud point defaults plus at least
      one non-vertex domain property (mesh face centers), and verify identical
      kernel inputs plus source-correct publication.
- [x] Prove mesh/graph topology and non-target properties survive apply, undo,
      and redo byte-for-byte, including unrelated custom properties whose value
      kinds are not part of the runtime primitive catalog.
- [x] Prove topology-bearing count changes fail before work is queued while an
      equivalent point-cloud request remains supported.
- [x] Cover stale async completion and config/agent/direct-command parity for
      all three source domains.

## Docs

- [x] Update runtime and Sandbox method-integration docs with the source and
      publication matrix; update method package notes if the literature audit
      changes a declared variant contract.

## Acceptance criteria

- [x] LOP-family eligibility depends on a usable typed property on a resolved
      element domain, not exact point-cloud provenance or `VertexProperty`.
- [x] Mesh and graph entities retain topology; no implicit conversion exists.
- [x] Every control surface uses one validated config/operation path and every
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
