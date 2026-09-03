---
id: METHOD-040
theme: I
depends_on: [METHOD-039, REVIEW-004]
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "This task materially changes a triangle-surface method and its same-topology face/edge result contract, but deliberately stops before runtime/config/UI/property adoption. geometry.parameterization-optimization does not apply because no UV, chart, seam, cut materialization, or topology change is in scope. geometry.support-radius-policy does not apply because this remains a mesh method with a method-local dimensionless surface scale, not a compact-support point-set method."
maturity_target: CPUContracted
---
# METHOD-040 — Global multicut curvature-patch CPU reference

## Goal
- Replace METHOD-039's seed-sensitive greedy region-merging decision with the
  smallest task-local global-partition formulation that preserves its hard
  feature constraints and continuous signed-curvature intent, then accept a
  CPU reference only if it passes the already frozen oracle, topology, and
  seed/diagonal/scale/noise stability gates.

## Non-goals
- No novelty or publishability claim. This is a practical synthesis of
  established correlation-clustering/multicut and surface-segmentation work.
- No parameterization, UV charts, seam selection, triangle cutting, vertex
  duplication, remeshing, or other topology-changing result.
- No retuning of METHOD-039's detector, inherited analytic fixtures, metric
  thresholds, hard-feature facts, signed-curvature inputs, or v1 reference
  after reading the failed seed case.
- No generic graph-optimization framework, public multicut package, solver
  registry/service/backend hierarchy, or new third-party dependency without a
  separately reviewed two-consumer or measured-need decision.
- No runtime/config/UI/property adoption, optimized CPU/GPU backend, or speedup
  claim. A positive CPU-contract verdict must open a separate adoption task.

## Context
- METHOD-040 remains paused behind `REVIEW-004`. The explicit user direction
  that completed METHOD-039 on 2026-09-03 was scoped to that bounded negative-
  result retirement and does not authorize this next research slice.
- METHOD-039's standalone feature detector and local patch reference are
  independently executable. The local solver passes the frozen supplied-
  oracle catalog, the mandatory hard-fold and computed smooth-transition/
  ridge/valley controls, homogeneous plane/cylinder controls, deterministic
  growth, boundary-role, scale, alternate-diagonal, noise, and orientation
  checks.
- The preregistered one-dual-step perturbation of every automatic seed exceeds
  the area-weighted variation-of-information limit of `0.01`; the local
  solver terminates at an exact energy local optimum with unsupported closure
  fragments. `LocalRagOneStepSeedPerturbationRefutesFrozenStabilityGate` is the
  executable negative oracle. METHOD-039 therefore forbids positive adoption
  or threshold tuning and retires its local solver as a diagnostic comparator.
- Begin with the original Zhuang et al. feature-aligned segmentation paper and
  primary correlation-clustering/multicut sources plus later deterministic
  surface-graph improvements. Record stable citations and distinguish an
  exact global objective from a heuristic that merely performs wider moves.
- The central formulation question is explicit: METHOD-039's regional GMM
  likelihood and geodesic-turning terms are not automatically pairwise-
  additive multicut costs. Before implementation, either derive a documented
  equivalent finite graph objective or freeze a new pairwise objective and
  report the old complete energy only as an external diagnostic. Do not call a
  pairwise surrogate the same energy.

## Control surfaces
- Config: deferred. Seeds may remain a diagnostic incumbent/proposal input but
  may not become a correctness-bearing config parameter.
- UI: deferred. No selector appears until a later adoption task has a positive
  CPU-contract verdict and frozen result diagnostics.
- Agent/CLI: deferred with config/UI; no private invocation path is added.

## Backends
- Backend axis: one serial deterministic CPU reference candidate only. The
  METHOD-037 `cpu_reference_v1` remains the production reference and the
  METHOD-039 local patch solver remains an unexposed negative comparator.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | A full owning oriented triangle surface with finite positions, live triangle adjacency, slot-aligned ordered signed principal curvatures, and slot-aligned hard/soft feature evidence. Surface faces and their embedding remain semantic inputs. |
| Compatible entity sources | Mesh geometry entities only; point sets and abstract graphs do not satisfy the embedded surface contract. |
| RuntimeModule | Deferred to a separately scoped adoption task after a positive CPU-contract verdict; do not modify the existing runtime operation here. |
| Config/agent | Deferred with runtime adoption. No public token or schema migration is allowed in this task. |
| UI | Deferred with runtime adoption. The existing v1 panel remains unchanged. |
| Publication | Return slot-aligned face regions and edge boundary/role diagnostics without mutating the mesh. Canonical property publication remains unchanged and is deferred. |
| End-to-end tests | Not applicable before adoption. This task owns geometry CPU correctness, exact-small-graph, stability, and fail-closed controls only. |

## Slice plan
- **Slice A — Primary-source intake and objective freeze.** Determine whether
  the regional/turning objective has an exact multicut representation; freeze
  graph atoms, signed costs, hard constraints, solver identity, tie-breaking,
  failure behavior, fixtures, metrics, and killing thresholds before reading a
  new integrated result.
- **Slice B — Exact bounded oracle.** Implement a task-local exhaustive or
  branch-and-bound solver for tiny RAGs and use it to validate objective
  assembly, hard constraints, label-permutation invariance, and every accepted
  heuristic move. This is a correctness oracle, not the production-scale path.
- **Slice C — Deterministic wider-move reference.** Implement the smallest
  deterministic multicut/correlation-clustering solve justified by Slice A,
  using sparse surface-graph storage. Seed/growth partitions may initialize an
  incumbent but may not change the graph objective or accepted optimum.
- **Slice D — Frozen verdict.** Replay METHOD-039's full oracle and stability
  gates, add bounded health diagnostics, and retire positively or negatively.
  A positive verdict opens a separate engine-adoption task; a negative verdict
  retains both candidate solvers unexposed.

## Required changes
- [ ] Review and cite the original feature-aligned correlation-clustering work
      plus relevant primary multicut sources; select one exact objective and
      state which METHOD-039 terms are preserved, transformed, or diagnostic-
      only.
- [ ] Freeze initialization-independent graph atoms. Hard-feature transitions
      must be fixed cuts; no atomic node may straddle a hard feature.
- [ ] Freeze signed join/cut costs, complexity prior, numerical tolerances,
      stable ordering, and infeasibility/non-finite statuses before integrated
      execution. Do not tune against the known perturbed-seed output.
- [ ] Implement a tiny-graph exact oracle and enumerate all feasible
      partitions on generated bounded cases to validate objective assembly and
      the claimed optimum.
- [ ] Implement one deterministic sparse CPU reference using private plain
      structs/free functions in the existing curvature-segmentation package.
      Do not extract a reusable optimizer without a present second consumer.
- [ ] Treat seeds only as optional proposal/incumbent data. Different legal
      seed initializations must converge to an equivalent accepted partition
      or fail the task's unchanged adoption gate.
- [ ] Preserve slot-aligned diagnostics, connected final patches, distinct
      hard/soft/closure boundary roles, exact hard barriers, and fail-closed
      validation. Report objective lower/upper bounds or optimality gaps only
      when they are mathematically justified.
- [ ] Record sparse graph size, iteration/move counts, deterministic payloads,
      stage timings, and peak workspace estimates. No dense face-pair matrix.
- [ ] Keep the candidate private and open a separate config/runtime/UI/
      publication task only after all CPU acceptance gates and claim-grade
      evidence pass.

## Tests
- [ ] Exact-oracle tests compare the candidate objective and partition against
      exhaustive feasible partitions on tiny hard/soft/closure graphs,
      including ties, disconnected components, junctions, and infeasibility.
- [ ] Replay all supplied-oracle and computed-evidence METHOD-039 controls;
      mandatory hard folds remain cut and plane/cylinder controls remain one
      patch with no interior boundary.
- [ ] The original automatic seeds, one-dual-step perturbed seeds, density
      multipliers `1.5/2/3`, traversal/storage permutations, alternate
      diagonals, scale, bounded noise, and orientation reversal stay within
      area-weighted VI `0.01` and projected boundary distance `0.02 D`.
- [ ] Results are deterministic under repeated runs and graph insertion-order
      permutations; every final patch is connected and every boundary has a
      valid hard/soft/closure role.
- [ ] Empty, non-triangle, degenerate, non-manifold, non-finite, slot-mismatch,
      invalid-cost, and solver-limit inputs fail closed without partial output.
- [ ] A bounded health cohort confirms sparse storage and reports work/timing
      diagnostics without making a performance claim.

## Docs
- [ ] Record citations, the exact chosen objective, units, solver contract,
      tie-breaking, diagnostics, complexity, and the relationship to the
      rejected METHOD-039 local energy in the method package.
- [ ] Add a schema-valid quality/stability manifest and result protocol before
      integrated execution; preserve every positive and negative candidate
      outcome under claim-grade custody.
- [ ] Update the method README/manifest and ARA claim ledger with a bounded
      positive or refuted verdict. Regenerate the module inventory only if a
      public module surface changes.
- [ ] Name the separately scoped engine-adoption task only after a positive
      CPU verdict; otherwise document that no adoption follow-up is owed.

## Acceptance criteria
- [ ] The exact-small-graph oracle validates objective assembly and all hard
      constraints; production-scale results do not claim global optimality
      beyond any proven bound.
- [ ] The candidate passes every inherited oracle, hard-fold, homogeneous,
      smooth-transition, ridge/valley, topology, determinism, and fail-closed
      control without changing frozen inputs or thresholds.
- [ ] The formerly refuting one-step seed perturbation and every other frozen
      stability variant pass VI `<= 0.01` and boundary distance `<= 0.02 D`.
- [ ] The implementation remains task-local, sparse, and serial CPU; no
      production selector, runtime/config/UI path, property publication, GPU
      backend, performance claim, parameterization behavior, or novelty claim
      lands in this task.
- [ ] If any objective, exactness, hard-constraint, or frozen stability gate
      fails, retire the candidate as negative evidence with no adoption and no
      threshold tuning.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'CurvaturePatchContract|CurvatureSegmentation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/experiment_custody.py validate --root .
python3 tools/agents/check_ara_claims.py --root . --strict
```

## Forbidden changes
- Retuning or deleting the METHOD-039 refutation fixture, seed perturbation,
  objective controls, metric thresholds, or inherited METHOD-038 evidence.
- Calling a heuristic or pairwise surrogate a global optimum or the unchanged
  METHOD-039 energy without a proof.
- Crossing hard features, merging disconnected components, retaining a cut
  merely because seed fronts met, or making seed IDs semantic output.
- Introducing a generic optimizer/service/registry/backend framework, dense
  all-pairs face storage, a new third-party solver, or a public selector before
  a positive frozen verdict and separate review.
- Materializing cuts, changing topology, adding parameterization behavior, or
  claiming novelty, GPU parity, or a speedup.

## Maturity
- Target: `CPUContracted` for one accepted deterministic global-partition CPU
  reference. Runtime/config/UI/property adoption is deliberately not part of
  this task.
- A negative result is a valid terminal state without claiming
  `CPUContracted`; both v1 production behavior and the METHOD-039 local
  diagnostic comparator remain unchanged and no adoption follow-up is owed.
