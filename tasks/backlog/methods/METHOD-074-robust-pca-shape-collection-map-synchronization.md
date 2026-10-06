---
id: METHOD-074
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: proposal filed from REVIEW-007 E7; implementation follows the method workflow (paper intake, CPU reference, analytic tests, benchmark) and owes research evidence before any claim.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation]
---
# METHOD-074 — RobustPCA synchronization of maps across a shape collection

## Goal
- N shapes with pairwise correspondences: stacking the pairwise maps (permutation/partial
  correspondence matrices over k landmark or sample points, or functional-map blocks) gives a block
  matrix that is **low-rank** when the maps are cycle-consistent; wrong matches are **sparse**;
  unmatched pairs are **missing**. Recover consistent maps and flag inconsistent correspondences.
- Origin: REVIEW-007 E7 (2026-10-06), GE15 — application of `Geometry::Linalg::RobustPCA`
  (Principal Component Pursuit, L + S by ADMM; stopping rule fixed in `74135dfff`). This is a
  **proposal**; any map-accuracy statement needs an `ara/logic/claims.md` row first (AGENTS.md §8b).

## Missing infrastructure (partial prerequisite)
- The engine has no map storage: `docs/architecture/parameterization-mapping-roadmap.md` Pack 6
  (surface-to-surface map records) is unimplemented and has no task. In scope without it: an operation
  that computes pairwise correspondences inside the run from existing registration (CPD soft
  correspondences or ICP closest points on a fixed sample set), synchronizes them and publishes
  per-sample results as properties. Persisting or composing maps needs Pack 6 filed and retired first;
  functional-map blocks additionally need a functional-map method task (the LBO eigenbasis module exists).

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | N sample sets (k samples each) and pairwise correspondence matrices, possibly partial. |
| Compatible entity sources | Any compatible `vec3` `GeometryPropertyRef` binding, independent of property name: mesh vertices/halfedges/edges/faces, graph nodes/halfedges/edges, point-cloud points; samples drawn from the bound property by existing samplers. |
| RuntimeModule | New `Runtime.MapSynchronizationOperations`; pairwise correspondences from existing `Runtime.RegistrationOperations` (CPD/ICP) over the selection, bounded N and k. |
| Config/agent | `sandbox.map_synchronization`: sample count, pairwise method, lambda, tolerance, reference shape; `preview_operation`/`run_operation`. |
| UI | Panel over the multi-selection: pair consistency table, inconsistent-correspondence counts. |
| Publication | Per shape: `uint` corresponding-sample index into the reference shape and scalar consistency score on each shape's source element domain (unsampled elements marked invalid); no persistent map object until Pack 6. |
| End-to-end tests | Every compatible domain (incl. faces, edges, halfedges) plus a synthetic collection (copies of one shape under rigid/nonrigid motion) with injected wrong matches → panel/agent → recovered correspondences above a stated accuracy. |

## Acceptance criteria
- [ ] Kernel extension in `Geometry.Linalg` (reuse if present): observation mask for partial maps;
      projection of recovered blocks back to (partial) permutations documented.
- [ ] Paper intake recorded; literature to verify: Huang–Guibas 2013 (consistent shape maps),
      Pachauri–Kondor–Singh 2013 (permutation synchronization), Zhou–Zhu–Daniilidis 2015 (MatchALS),
      Huang et al. 2014 (functional map networks).
- [ ] CPU reference on synthetic collections; collections with N < 3 or no overlap fail closed.
- [ ] Benchmark against pairwise-only correspondences on the same collection, sealed.
- [ ] New test suites `MapSynchronizationOperations`, `MapSynchronizationConfig` are added to `IntrinsicRuntimeContractTests`; they do not exist yet, and `--no-tests=error` cannot detect their absence while other selectors match.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(LinearAlgebra|MapSynchronizationOperations|MapSynchronizationConfig|AgentOperations|SandboxConfigSections|RuntimeEngineLayering|RuntimeEnginePrivateGlue)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```
