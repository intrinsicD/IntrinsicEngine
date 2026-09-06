---
id: METHOD-040
theme: I
depends_on: [METHOD-039]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-boundary"
branch: "codex/method-040-boundary-partition"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-05T23:57:08Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "This task materially changes a triangle-surface method and its same-topology face/edge result contract, but deliberately stops before runtime/config/UI/property adoption. geometry.parameterization-optimization does not apply because no UV, chart, seam, cut materialization, or topology change is in scope. geometry.support-radius-policy does not apply because this remains a mesh method with a method-local dimensionless surface scale, not a compact-support point-set method."
maturity_target: CPUContracted
---
# METHOD-040 — Global multicut curvature-patch CPU reference

## Goal
- Compute meaningful connected geometric patches by jointly selecting salient
  feature segments and their completing boundaries. Replace METHOD-039's
  seed-sensitive local partitioning with the smallest task-local multicut
  candidate that preserves hard features and uses surface-curvature evidence;
  statistical curvature-class coherence is not itself the patch definition.
  Accept a CPU reference only if it passes the already frozen oracle, topology,
  and seed/diagonal/scale/noise stability gates plus the declared quality cohort.

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
- On 2026-09-06 the operator explicitly authorized overnight implementation,
  collaboration with Claude through MCP, diverse local dataset testing, and
  iterative fixes through the next morning. This scoped direction releases
  METHOD-040 from REVIEW-004 without unpausing other research work. The working
  deadline is 08:00 Europe/Berlin on 2026-09-06. Preserve model capacity for
  verification and final review; Codex's starting weekly used allowance is 82%.
- This run uses high-risk engineering custody and non-claim-eligible exploratory
  comparisons. It is not a publication-bound experiment; the former claim-grade
  planning profile is replaced explicitly, while independent source review,
  frozen candidate definitions, retained negative evidence, and existing
  correctness gates remain required. No performance or quality adoption claim
  follows from completing the overnight engineering slice.
- The [review](../../methods/geometry/curvature_segmentation/feature_boundary_review.md)
  explains the clarified geometric target and earlier frog/sculpt observations.
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
  `BUG-163` later exposed that comparator through an explicitly diagnostic
  config/runtime/UI token for bounded sculpt inspection; this is not the
  positive production adoption that this task still requires before any
  METHOD-040 engine integration.
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
- Engine agent/CLI: deferred with config/UI. An opt-in benchmark executable
  exercises the public CPU method API; it is not an engine operation or selector.

## Backends
- Backend axis: one serial deterministic CPU reference candidate only. The
  METHOD-037 `cpu_reference_v1` remains the production reference and the
  METHOD-039 local patch solver remains an explicitly labeled negative
  comparator rather than an accepted backend.

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

## Overnight candidate freeze
- The boundary-only candidate remains a comparator after varied mesh inspection
  exposed fragmentation and a large background region. The separately frozen
  [regional experiment](../../methods/geometry/curvature_segmentation/boundary_partition_experiment.md)
  adds bounded curvature fitting, a scale-coupled region cost, and attenuation
  of redundant soft evidence near hard creases. It is explicitly a new
  non-pairwise objective; exact tiny-graph tests cover its complete energy.
  This extension follows the user's authorization to improve the implementation
  where testing reveals a need. Existing oracles and their thresholds remain
  unchanged, and any candidate conflict remains a negative adoption result.
- Candidate v1 was rejected by the unchanged same-curvature false-boundary
  oracle (F=0.55). Its exact source and test log are retained under
  `tasks/evidence/METHOD-040/experiments/`. Before any dataset result, v2 fixes
  alpha=1.5: moderate F=0.55 has positive cut cost while the inherited salient
  F>=0.8 contours have negative cost. No fixture or metric gate changes.
- Candidate `boundary_multicut_isotropic_v1`: full face-dual graph, source-edge
  length divided by mesh diagonal, cost `ell * (1 - 4 * F)`, exact must-cut
  hard constraints, no GMM unary, region-count penalty, or raw turning energy.
  Canonical face-slot ordering; deterministic ties use the lowest node IDs.
- CPU path: exact feasible partition enumeration up to eight graph nodes;
  sparse greedy additive edge contraction, then bounded Kernighan-Lin-style
  pair refinement and splits for larger graphs. Accept only strictly improving
  feasible prefixes, canonicalize connected regions, and recheck energy/hard
  constraints. Return no partition on invalid inputs or exhausted work budget.
- Initial limits: 8 outer sweeps, 2000000 attempted moves, 1e-12 relative
  energy tolerance. Report actual convergence/work-limit disposition separately
  from elapsed timing. Isotropic metric first; anisotropy is a separately frozen
  candidate only if the first comparison identifies a need.
- Calibration/known examples: sculpt and frog. Additional cohort chosen by
  filename and size before candidate outputs: fandisk, bunny10k, dolphin,
  elephant, bumpy_torus, genus3, office_chair, sphere, cube, saddle2. Inputs stay
  under the local Dropbox dataset root; record hashes, counts, load rejections,
  parameters, timings, boundary support/closure, and region areas. Never silently
  discard rejected faces to produce a favorable segmentation.
- One repository writer (Codex). Claude is reserved for bounded mathematical
  planning and independent final review; code/data access is pending the
  explicit payload permission requested after automatic review rejected it.
  The current MCP server lists no available agent types; continue local work
  while resolving this collaboration capability.
- Smallest design: one method-local solver implementation with plain data,
  one mesh API, a private graph test seam, and one opt-in runner. No generic
  graph package, service, registry, backend factory, or new dependency.

## Slice plan
- **Slice A — Primary-source intake and objective freeze.** Use the dated
  feature-boundary review as the proposed direction: a new signed boundary-cost
  objective, with regional GMM/turning energy reported only as diagnostics.
  Freeze graph atoms, isotropic then separately specified anisotropic costs,
  hard constraints, solver identity, tie-breaking, failure behavior, fixtures,
  quality cohort, metrics, and killing thresholds before integrated execution.
  State explicitly how face-dual/source-edge boundaries differ from Zhuang's
  vertex-graph/within-triangle formulation; do not claim equivalence.
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
- [x] Review and cite the original feature-aligned correlation-clustering work
      plus relevant primary multicut sources; select one exact objective and
      state which METHOD-039 terms are preserved, transformed, or diagnostic-
      only.
- [x] Freeze initialization-independent graph atoms. Hard-feature transitions
      must be fixed cuts; no atomic node may straddle a hard feature.
- [x] Freeze signed join/cut costs, the presence or absence of a complexity
      prior, numerical tolerances,
      stable ordering, and infeasibility/non-finite statuses before integrated
      execution. Do not tune against the known perturbed-seed output.
- [x] Implement a tiny-graph exact oracle and enumerate all feasible
      partitions on generated bounded cases to validate objective assembly and
      the claimed optimum.
- [x] Implement one deterministic sparse CPU reference using private plain
      structs/free functions in the existing curvature-segmentation package.
      Do not extract a reusable optimizer without a present second consumer.
- [ ] Treat seeds only as optional proposal/incumbent data. Different legal
      seed initializations must converge to an equivalent accepted partition
      or fail the task's unchanged adoption gate.
- [x] Preserve slot-aligned diagnostics, connected final patches, distinct
      hard/soft/closure boundary roles, exact hard barriers, and fail-closed
      validation. Report objective lower/upper bounds or optimality gaps only
      when they are mathematically justified.
- [ ] Record sparse graph size, iteration/move counts, deterministic payloads,
      stage timings, and peak workspace estimates. No dense face-pair matrix.
- [x] Keep the candidate private and open a separate config/runtime/UI/
      publication task only after all CPU acceptance gates and the declared
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
- [x] Results are deterministic under repeated runs and graph insertion-order
      permutations; every final patch is connected and every boundary has a
      valid hard/soft/closure role.
- [x] Empty, non-triangle, degenerate, non-manifold, non-finite, slot-mismatch,
      invalid-cost, and solver-limit inputs fail closed without partial output.
- [x] A bounded health cohort confirms sparse storage and reports work/timing
      diagnostics without making a performance claim.
- [ ] Add the review's equal-curvature/strong-contour, broken-contour completion,
      unrelated-fragment, and zigzag representation controls. Freeze a quality
      cohort containing sculpt, the known frog counterexample, and additional
      held-out meshes; assess feature/closure length, area distribution,
      fragmentation, and matching-view overlays rather than region count alone.

## Docs
- [x] Record citations, the exact chosen objective, units, solver contract,
      tie-breaking, diagnostics, complexity, and the relationship to the
      rejected METHOD-039 local energy in the method package.
- [x] Add a schema-valid quality/stability manifest and result protocol before
      integrated execution; preserve every positive and negative candidate
      outcome with source-bound engineering evidence.
- [x] Update the method README/manifest and ARA claim ledger with a bounded
      positive or refuted verdict. Regenerate the module inventory only if a
      public module surface changes.
- [x] Name the separately scoped engine-adoption task only after a positive
      CPU verdict; otherwise document that no adoption follow-up is owed.

## Acceptance criteria
- [x] The exact-small-graph oracle validates objective assembly and all hard
      constraints; production-scale results do not claim global optimality
      beyond any proven bound.
- [ ] The candidate passes every inherited oracle, hard-fold, homogeneous,
      smooth-transition, ridge/valley, topology, determinism, and fail-closed
      control without changing frozen inputs or thresholds.
- [ ] The formerly refuting one-step seed perturbation and every other frozen
      stability variant pass VI `<= 0.01` and boundary distance `<= 0.02 D`.
- [x] The implementation remains task-local, sparse, and serial CPU; no
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
  all-pairs face storage, a new third-party solver, or a METHOD-040 production
  selector before a positive frozen verdict and separate review.
- Materializing cuts, changing topology, adding parameterization behavior, or
  claiming novelty, GPU parity, or a speedup.

## Maturity
- Target: `CPUContracted` for one accepted deterministic global-partition CPU
  reference. Runtime/config/UI/property adoption is deliberately not part of
  this task.
- A negative result is a valid terminal state without claiming
  `CPUContracted`; both v1 production behavior and the METHOD-039 local
  diagnostic comparator remain unchanged and no adoption follow-up is owed.

## Overnight implementation record — 2026-09-06

- Implemented a task-local sparse signed-boundary solver, tiny exact oracle,
  optional area-weighted regional fitting, and explicit area agglomeration.
  The latter can increase the optimized energy; both values and merge counts
  are exposed separately, and changed results lose the exact-optimum flag.
- Fixed independently reviewed stale regional merge proposals and weighted
  variance overflow/underflow. Added direct regression cases, bounded stale
  heap storage, removed constant terminal-flow costs, and added a valid
  split-rejection bound. Exact hard barriers remain independent Boolean facts.
- The immutable unmarked-curvature closure oracle rejects the weak regional
  candidate at VI=log(2), above 0.01. Boundary-only candidates also cannot
  satisfy that curvature-only oracle. The original oracle and threshold remain
  unchanged; the rejection is retained as an executable negative control.
  No positive METHOD-040 CPU-contract or production-adoption verdict follows.
- The separately frozen contrast/area/curve-coverage profiles investigate the
  operator's geometric-feature target. The
  [experiment contract](../../methods/geometry/curvature_segmentation/boundary_partition_experiment.md)
  records every objective and parameter change explicitly. Twelve local inputs
  are retained in the comparison, including parser/topology rejections; meshes
  are never silently repaired, trimmed, or used as CI fixtures.
- Added exact loaded-geometry exports and a standalone local comparison viewer.
  Synthetic viewer regressions verify native vertex reordering and input/output
  identity, hashes, face labels, and boundary agreement. Browser checks exercise
  synchronized cameras and overlays without external dependencies.
- Claude collaboration through MCP produced two bounded public mathematical
  reviews. Automatic approval rejected sharing repository/data context, and
  that payload permission remains unanswered. A local independent reviewer
  inspected source and exact input conditions. A separate automatic rejection
  prevented transferring private derived viewer screenshots; they remain local.
- Formal completion-evidence generation is independently blocked by
  [BUG-171](../backlog/bugs/BUG-171-required-command-receipt-supersession.md): two
  failed development builds were mistakenly marked required, and the evidence
  tool cannot reconcile them with later passing checks. Both original receipts
  remain intact. Do not delete them, weaken the gate, or mark this task retired
  while that custody issue remains unresolved.

- Final local comparison: 48 retained runs across four fixed profiles and twelve
  inputs; 36 completed with exact hard constraints and full assignment, twelve
  rejected invalid/unsupported inputs. The cohort exited 1, as those failures
  remain part of the record. Thirty-six schema-v2 observations validate with
  claim eligibility false. The report is
  [boundary_partition_report.md](../../methods/geometry/curvature_segmentation/boundary_partition_report.md).
- Final source review resolved stale regional gains, weighted-variance numeric
  extremes, positive hard-curve test coverage, unbound viewer sidecars, and
  float32 export restoration. Twenty-eight boundary tests and ten synthetic
  viewer tests pass. The nine-mesh/four-profile local viewer passes browser
  behavior checks; no final visual-quality acceptance is claimed.
- The first full CPU run failed one existing authored-normal import test.
  One hundred isolated repetitions passed. Independent read-only review found
  plausible pre-existing completion races, recorded without proven attribution
  in [BUG-172](../backlog/bugs/BUG-172-synchronous-cpu-load-completion-race.md).
  The final CPU rerun passes; the original intermittent failure remains open.

- Required-gate corrections are tracked separately: BUG-173 fixes two runtime
  fixture-ordering assumptions (20 CPU and 20 ASan repetitions each), and BUG-175
  restores explicit manual classification for the earlier atlas smoke. Their
  implementation commits are `8c3200f1d` and `81b855283`; both notes are retired.
- Two ASan geometry wrapper timeouts led to reuse of identical detector setup
  and a dedicated required feature/partition test process. No case, assertion,
  label, or timeout was removed or relaxed. Actual grouped/discovered registration
  parity covers 4,293 GoogleTest cases; two manual records give 4,295 selected
  CPU entries. Concurrent local CTest discovery briefly duplicated generated
  registrations; serial regeneration corrected those files, with BUG-176 owning
  the future coordination guard. BUG-174 diagnosed inherited debuginfod lookup
  stalling llvm-symbolizer and clears that variable only for the harness child
  environment. Twenty complete ASan leak/lifetime repetitions pass without
  changing the suppression set, report predicates, or ten-second limit. Its
  implementation commit is `2d22f6f01`; the note is retired.
- The final canonical CPU gate passes 4,295 entries with its expected unsanitized
  GLFW/LSan skip. UBSan passes 2,753 physical entries with its expected leak-
  control skip; those entries cover the same 4,295 logical records. The final
  ASan rerun after the isolated BUG-174 repair passes all 2,753 entries with no
  skips. CPU/UBSan leak-control skips are expected without ASan instrumentation.
  The three geometry groups pass within the unchanged 120-second budget.

- The reviewed algorithm and test-producer source hashes are retained separately.
  The live work graph review binding became stale after final fixes and isolated
  prerequisite commits. The run was closed as aborted, with its events retained,
  because BUG-171 prevents formal completion. The task lease was released; the
  active negative experiment awaits custody reconciliation. Source-review
  acceptance does not constitute a fixed-surface retirement verdict.

- End-of-turn research record: C59 records the refuted frozen adoption hypothesis;
  C60 records only the bound local cohort observations. No generalized quality
  or performance claim is asserted.

## Subsequent diagnostic access
- The operator subsequently requested direct engine/UI testing with Claude
  collaboration. [UI-052](UI-052-method040-diagnostic-inspection.md) owns that
  separately scoped explicit diagnostic access, analogous to BUG-163. This
  does not change the overnight negative oracle, production default, formal
  retirement blocker, or the positive-adoption conditions above. Its versioned
  method token selects the fixed curves profile, not an accepted v2 backend.
