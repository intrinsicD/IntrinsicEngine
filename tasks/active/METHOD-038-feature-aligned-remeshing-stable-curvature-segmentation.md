---
id: METHOD-038
theme: I
depends_on: [METHOD-037, GEOM-071]
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-10T16:42:34Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "This task materially changes a mesh method, its CPU backend and diagnostics, its canonical face/edge publication, and its runtime/config/UI controls. geometry.parameterization-optimization does not apply because UV cuts and parameterization remain deferred to GEOM-076; geometry.support-radius-policy is point-set/LOP-specific, while this task freezes a separate surface-geodesic feature scale."
maturity_target: ParityProven
---
# METHOD-038 — Feature-aligned, remeshing-stable curvature segmentation

## Status

- Active as of 2026-08-10. Slice A is evidence-only: paper intake,
  deterministic baseline profiling, remeshing fixtures, candidate killing
  experiments, and a frozen protocol. The production selector remains
  METHOD-037 `cpu_reference_v1` until the preregistered gates select a v2
  formulation.
- Checkpoint 1 adds observational stage/per-candidate timing, a deterministic
  supplied-curvature profile runner with declared 10k/100k/1M Fixed and
  Automatic cohorts, an exact two-component `population_count` gate, a
  deterministic minimum-region cleanup regression, and the primary-source
  equation/scale/metric intake. It does not alter method output or runtime
  defaults.
- Frozen scratch run `scratch-002` reproduced the earlier probe from checkpoint
  `874d09c3`: Automatic selected four components and failed the exact-two gate
  while its `0.01` label error passed; the matched Fixed control selected two
  with `0.005` error. Independent audit
  `codex-method038-scratch-auditor-20260810-r2` recomputed the same gates and
  rejected claim authorization as intended; bounded negative claim C40 records
  the result.
- Build triage: the initial broad incremental build and its ccache-disabled
  retry produced shifting Clang 23 frontend bus errors in unrelated runtime and
  test translation units. Preset reconfiguration followed by
  `CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests --parallel 2`
  completed without either crash; no speculative METHOD-038 source workaround
  was introduced.
- Checkpoint 2 extends only the opt-in private profiler: a 10k analytic-sphere
  diagonal pair now exercises cold `ComputeAndSegment` and reusable
  precomputed-curvature lanes, while the existing planar transition pair now
  reports an exact-reference continuous-boundary upper bound. Both Fixed and
  Automatic modes retain their declared population gates; no selector,
  runtime/config/UI path, public module, or production default changed.
- The canonical non-claim fixture replay is `scratch-003`. It preserves the
  Fixed control and the Automatic over-selection as separate outcomes instead
  of tuning the selector from the fixture. This checkpoint is a bounded
  fixture contract, not the full analytic corpus, remeshing convergence, or a
  candidate-v2 selection.
- Checkpoint 3 freezes the disjoint cheapest A-D screen/confirmation parameters
  and adds an opt-in fold-control lane over paired `30/45/60`-degree isometric
  meshes. It checks the shared strict `angle > 45 degrees` feature facts,
  flat/fold edge-length correspondence, and the constant-curvature v1 negative
  control without executing or selecting a candidate. Frozen `scratch-004`
  preserved a schema-only rejection of an unregistered composite backend
  label; the scientific gates were not changed in response.
- Accepted `scratch-005` then recorded exact paired-diagonal feature counts
  `0/0/24` at `30/45/60` degrees, zero feature-mask error, normalized maximum
  flat/fold edge-length delta `0.000000038`, and identical one-component,
  one-region, zero-boundary v1 payloads for all three folds. Independent audit
  accepted the derivation with `claim_authorized: false`; bounded claim C42
  records fixture/oracle integrity only. Final task-bound replay `scratch-006`
  is preserved under
  `superseded/20260811-fold-screening-final-task-bound/` before checkpoint 4
  takes over the live custody path.
- Checkpoint 4 preregisters that extension as a separate, opt-in
  `surface_controls` lane. It fixes an open `R=1`, `L=2`, `32 x 64` cylinder
  phase pair and a `24 x 48` paired-diagonal graph of
  `q(x)=0.5(1+tanh(x/0.08))`, including analytic curvature, hard-feature,
  v1 negative/comparison, and continuous `x=0` boundary oracles. No candidate
  A-D, held-out confirmation row, selector, or production integration is in
  this checkpoint.
- The private checkpoint-4 runner and manifest now implement those frozen
  rows at scientific source `251f2dab`. Accepted pre-status run `scratch-007`
  passed all 17 gates: both cylinder phases had zero hard features and exact
  v1 payload parity; both smooth phases recovered the exact 24-edge reference
  mask with zero label/mask error, two endpoints, and no junction. Its
  independent audit retained `claim_authorized: false`, and bounded claim C43
  records fixture/oracle integrity only. Because this status correction
  advances the task hash, that immutable run is preserved under
  `superseded/20260811-surface-screening-task-status-advance/`.
- Final checkpoint-4 disposition is reserved to task-bound `scratch-008` and
  its independent audit. That identity must replay the unchanged scientific
  protocol without retuning any fixture, metric, or gate. Production remains
  METHOD-037 `cpu_reference_v1` throughout.
- Next scientific boundary: preregister candidate A's cheapest killing screen
  against the validated controls, then complete the remaining screening corpus
  before opening held-out confirmation. This checkpoint does not begin that
  candidate implementation.

## Goal
- Replace METHOD-037's slow, edge-local boundary inference with an evidence-selected CPU formulation that remains statistically based on the existing Gaussian mixture, aligns region boundaries with persistent surface feature curves, converges across retriangulations of the same embedded surface, and meets a preregistered useful-acceleration gate without weakening correctness, diagnostics, or the Fixed/Automatic UI contract.

## Non-goals
- No claim of exact equality between face labels, edge IDs, or edge masks on different triangulations. The target is agreement after projection to a common continuous reference surface and convergence under refinement.
- No intrinsic-only feature detector. A high-dihedral crease is extrinsic and cannot be distinguished from an isometric fold using only the surface metric; the selected method may use dihedral angle, normal variation, and robust extrinsic curvature descriptors.
- No normal-curvature term in the boundary-fairness energy. Boundary regularity may penalize length and geodesic curvature `k_g`; it must not penalize the curve's normal-curvature component `k_n`, which would bias the path merely because the supporting surface bends.
- No new Gaussian-mixture implementation, generic clustering framework, neural segmentation model, service/registry/backend framework, or silent substitution of a different statistical model. Reuse `Geometry.GaussianMixture` for the region data model.
- No UV cuts, chart construction, topology edits, vertex duplication, parameterization, or atlas adoption. `GEOM-076` remains the only consumer and stays evidence-gated on this task's accepted result.
- No GPU backend before the selected CPU reference, quality gates, optimized-CPU parity, and performance baseline are frozen and accepted.

## Context
- Owner/layer: scientific descriptors, feature likelihoods, boundary objective, reference and optimized CPU kernels, and benchmark diagnostics live in `src/geometry` (`geometry -> core`). Runtime owns canonical-property binding, input-revision/cache policy, validated config, publication, undo/stale checks, and backend selection. App owns only the existing Sandbox controls and visualization.
- METHOD-037 currently averages signed vertex `(k1,k2)` onto faces, fits the existing GMM once per automatic candidate, and uses a feature-weighted Potts/ICM objective on the input face-dual graph. A high dihedral merely lowers the cost of cutting an edge; it does not require or geometrically snap a boundary there. Raw face adjacency and per-edge normal differences also make the final edge mask sensitive to tessellation.
- `GEOM-071` owns the shared, validated high-dihedral/boundary feature classification and canonical `e:feature` materialization. This task consumes that seam instead of duplicating a private threshold test, then augments it with persistent smooth-feature evidence where a single dihedral threshold is insufficient.
- The phrase "triangulation independent" is frozen here as **remeshing stability**: given several valid meshes sampling the same oriented embedded surface and known closest-point/barycentric correspondence to one analytic or high-resolution reference, compare labels by area after projection and compare boundary curves in surface distance. Exact combinatorial identity is neither required nor meaningful.
- The user's curvature restriction is interpreted as a boundary-energy restriction: extrinsic curvature/normal evidence remains admissible for detecting creases, ridges, valleys, and surface-type transitions, while curve fairness uses `k_g` and never `k_n`. If the intended restriction is instead "no extrinsic curvature anywhere," intake must stop and record that this conflicts with mandatory high-dihedral detection before implementation proceeds.

### Published lineage and closest prior art

| Work | Relevant mechanism | Role in this task |
| --- | --- | --- |
| [Lavoué, Dupont, and Baskurt 2005](https://doi.org/10.1016/j.cad.2004.09.001) | Near-constant curvature regions followed by curvature-tensor-direction boundary rectification | Direct baseline for curvature patterns and explicit boundary cleanup |
| [Lai, Zhou, Hu, and Martin 2006](https://doi.org/10.1145/1128888.1128891) | Feature-sensitive remeshing hierarchy, integral invariants, hierarchical clustering, and feature-sensitive boundary smoothing | Strongest direct candidate for both input-triangulation insensitivity and large-mesh acceleration |
| [Cohen-Steiner and Morvan 2003](https://doi.org/10.1145/777792.777839) | Normal-cycle curvature tensor with an approximation bound on restricted Delaunay triangulations | Convergent extrinsic-curvature estimator candidate |
| [Pottmann et al. 2007](https://doi.org/10.1016/j.cagd.2007.07.004) and [Pottmann et al. 2009](https://doi.org/10.1016/j.cagd.2008.01.002) | Multi-scale integral-invariant curvature and robust local geometric descriptors | Preferred low-noise, physical-scale descriptor family |
| [Lai et al. 2007](https://doi.org/10.1109/TVCG.2007.19) | Feature-sensitive remeshing, multi-scale integral invariants, morphology, and feature classification | Candidate for persistence, hysteresis, line continuity, and ridge/valley/junction classification |
| [Hildebrandt, Polthier, and Wardetzky 2005](https://doi.org/10.2312/SGP/SGP05/085-090) | Smoothed higher-order extremality fields for coherent surface feature lines | Ridge/valley baseline; expected to be noise-sensitive and not the default without evidence |
| [Yan et al. 2012](https://doi.org/10.1016/j.cad.2012.04.005) | Variational quadric patch fitting with feature- and simplification-based acceleration | CAD-oriented alternative when constant-curvature clustering is the wrong region model |
| [Bonneel et al. 2018](https://doi.org/10.1111/cgf.13549) | Surface Mumford-Shah/Ambrosio-Tortorelli formulation using discrete exterior calculus | Continuous edge-set alternative and quality baseline, not presumed fast enough for adoption |
| [Golovinskiy and Funkhouser 2008](https://doi.org/10.1145/1409060.1409098) | Consensus boundary probability from randomized cuts, with tessellation-sensitivity experiments | Stability baseline or boundary prior; not the default due to cost |
| [Sun, Harik, and Baek 2018](https://doi.org/10.1080/16864360.2018.1441235) | Geodesic-curvature flow applied to mesh-segmentation similarity fields | Boundary regularization baseline; does not by itself identify the right feature network |
| [Hildebrandt, Polthier, and Wardetzky 2006](https://doi.org/10.1007/s10711-006-9109-5) | Conditions for convergence of metric and geometric properties of polyhedral surfaces | Defines the assumptions under which a remeshing-convergence claim is defensible |
| [Chen, Golovinskiy, and Funkhouser 2009](https://doi.org/10.1145/1531326.1531379) | Region and boundary metrics for mesh-segmentation evaluation | Metric lineage; this task adds paired-remeshing correspondence tests |

- Prior-art disposition: feature-sensitive, tessellation-robust segmentation is known, especially in Lai et al. The repository contribution is an adaptation and controlled comparison against METHOD-037's existing GMM/config/publication path, not a novelty claim. Any stronger scientific claim requires a separate adversarial prior-art audit and ARA record.

## Candidate formulations and decision gate

- **A — Feature-first GMM (smallest candidate).** Keep robust signed-curvature GMM unaries, consume `GEOM-071` high-dihedral edges as mandatory barriers, infer smooth feature likelihood from persistent multi-scale integral-normal/curvature changes, and optimize connected regions only within the resulting feature cells. Regularize any movable boundary by length plus geodesic curvature. This is the preferred first experiment because it preserves the current statistical interpretation and output properties.
- **B — Feature-sensitive remeshing hierarchy (strongest prior-art candidate).** Build a bounded coarse-to-fine, feature-sensitive sampling hierarchy using integral invariants, solve region hypotheses on the coarse surface, refine only ambiguous bands, and project the result back to the source mesh. This is the leading candidate if A cannot meet both speed and remeshing-stability gates.
- **C — Continuous Mumford-Shah edge set.** Jointly approximate the curvature descriptor field and an edge indicator using the Ambrosio-Tortorelli/DEC family, with feature confidence in the edge cost and geodesic-curvature-only boundary fairness. Retain as a quality baseline unless profiling shows an interactive path.
- **D — Quadric/constant-surface proxies.** Fit plane/sphere/cylinder/general-quadric proxies and use feature curves as admissible patch boundaries. Evaluate on CAD fixtures only; reject as the general default if it loses free-form curvature regimes or the existing Fixed/Automatic semantics.
- **E — Consensus/randomized cuts.** Use repeated perturbations/remeshings to estimate boundary persistence. Treat this primarily as an offline robustness oracle or evidence generator; adopt it in production only if a bounded deterministic approximation meets the latency gate.
- **Intrinsic-only negative control.** Compare a flat sheet and an isometrically folded sheet. An intrinsic metric/geodesic-only detector must give the same response and therefore miss the dihedral crease; this control prevents an impossible requirement from being hidden behind discretization.
- Freeze the selected formulation, rejected alternatives, parameters, physical feature scale, metrics, and killing gates before changing the production selector. A negative result is valid: keep METHOD-037 available and block `GEOM-076` adoption.

## Control surfaces
- Config: migrate `sandbox.curvature_segmentation` through a schema-versioned codec. Preserve Fixed/Automatic component selection and add only evidence-selected controls: feature policy, dihedral threshold, physical/geodesic feature scale, persistence/hysteresis, boundary geodesic-fairness weight, and backend identity. Defaults must remain reproducible through config files, agent/CLI, and UI.
- UI: keep both Fixed and Automatic modes. Add feature-network/final-boundary inspection, reference-versus-candidate diagnostics, stage timings, cache-hit/backend identity, and a clear warning when the requested remeshing-stability assumptions are unavailable. Do not hide expensive work behind an unresponsive implicit rerun.
- Agent/CLI: use the same validated config record and configured request as the editor; no UI-only thresholds or backend choice.

## Backends
- Backend axis: retain METHOD-037 as the named `cpu_reference_v1` comparison oracle. First implement the selected feature-aligned formulation as a deterministic `cpu_reference_v2`; only after its quality contract passes may an optimized CPU implementation be added and exposed. No GPU backend in this task.
- Optimized CPU results report backend identity, stage timings, cache/reuse diagnostics, peak working-set estimate, and permutation-invariant parity deltas against `cpu_reference_v2`. A faster algorithm with unreported label or boundary drift fails.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | An oriented embedded triangle surface with finite positions, live face adjacency, and source-to-reference correspondence for remeshing evidence; canonical `e:feature` may be supplied or computed through `GEOM-071`. Faces, embedding, and adjacency are semantic inputs. |
| Compatible entity sources | Mesh entities only. Point sets and abstract graphs lack the embedded surface/face neighborhoods needed by the contract. |
| RuntimeModule | Extend the existing curvature-segmentation operation. Reuse source revision/stale checks and cache only immutable descriptor/feature intermediates keyed by exact position, topology, normal-source, and config revisions; add no service or registry. |
| Config/agent | Schema-versioned migration through the existing preview/validate/apply path; Fixed and Automatic remain available from Editor, AgentCli, and Programmatic sources. |
| UI | Extend the existing Curvature panel with the same validated controls, stage diagnostics, feature-network overlay, final face regions, and boundary overlay. |
| Publication | Preserve topology and existing face component/region/color plus edge boundary/color properties. If edge-only output cannot pass the remeshing gate, add a reviewed continuous surface-curve result in triangle/barycentric coordinates and derive the edge mask only as a visualization approximation; do not materialize cuts. |
| End-to-end tests | Analytic/remeshed geometry fixtures, reference/optimized parity, canonical feature-property binding, config-source parity, runtime caching/stale rejection, UI selection/diagnostics, and simultaneous feature-network/region/boundary visualization. |

## Slice plan
- **Slice A — Intake, profiling, and frozen protocol.** Reproduce the slowdown on deterministic scale cohorts, add stage timings, construct paired remeshings with continuous ground truth, run the cheapest killing experiment for A–D, and freeze the exact quality/performance gates. No production default changes.
- **Slice B — Feature-aligned CPU reference v2.** Implement only the selected formulation, preserve `cpu_reference_v1` as a comparison lane, add analytic/remeshing correctness tests, and publish continuous boundary evidence if edge masks alone fail.
- **Slice C — Optimized CPU.** Optimize only measured dominant stages. Candidate work includes descriptor reuse by property revision, one-time dual/feature graph construction, contiguous buffers, parallel independent automatic-GMM candidates with deterministic result order, narrow-band refinement, and hierarchy/coarsening. Do not introduce a public backend token until exact v2 parity and the useful-acceleration gate pass.
- **Slice D — Runtime/config/UI cutover and evidence.** Bind the accepted backend and diagnostics through existing control surfaces, preserve v1 fallback for recorded failure cases, seal confirmation results, and re-gate `GEOM-076` from the accepted disposition.

## Required changes
- [ ] Expand paper intake with the sources above plus later improvements and record the exact selected equations, descriptor units, scale convention, feature taxonomy, boundary representation, convergence assumptions, and exclusions in `methods/geometry/curvature_segmentation/`.
- [ ] Profile the current fixed and automatic paths by stage: curvature estimation, face aggregation/normalization, each GMM candidate and EM iteration, unary construction, dual/feature graph construction, spatial solve, cleanup/connectivity, runtime extraction/publication, and optional visualization preparation. Treat bottlenecks as measured facts, not assumptions.
- [ ] Replace the tiny folded-strip-only performance signal with stable smoke and heavy manifests covering at least approximately 10k, 100k, and 1M live faces, both Fixed and Automatic selection, cold and reusable-descriptor runs, and uniform/anisotropic triangulations. Record warmup, machine identity, backend, stage timings, peak working-set estimate, and quality metrics.
- [ ] Build a screening corpus and disjoint confirmation corpus from analytic surfaces with exact continuous feature curves: plane, sphere, cylinder, cone/frustum, saddle, plane-plane folds at several dihedrals, smooth blends, tangent plane-cylinder and cylinder-sphere transitions, ridges/valleys, close parallel features, junctions, open boundaries, and disconnected components.
- [ ] Generate paired triangulations of every applicable surface by uniform refinement, random legal edge flips, anisotropic tessellation, nonuniform sampling, diagonal changes, and feature-aligned versus feature-crossing triangles while retaining exact projection/correspondence to the same reference surface.
- [ ] Define feature likelihood from recognizable, scale-persistent patterns rather than a single raw curvature difference: mandatory high-dihedral creases, persistent signed-curvature change points/surface-type transitions, coherent ridge/valley evidence where allowed, hysteresis across weak gaps, and minimum physical length/area. Expose the contribution of every signal in diagnostics.
- [ ] Define the boundary objective in continuous-surface terms: data fidelity, feature attraction/barriers, surface arc length, and optional integrated squared geodesic curvature. Prohibit a normal-curvature fairness term and test the distinction on isometric bend controls.
- [ ] Decide and document whether a source-edge mask can satisfy the frozen boundary-distance gate. If not, introduce the smallest continuous triangle-crossing/barycentric polyline result, map it deterministically to existing visualization properties, and leave topology unchanged.
- [ ] Implement deterministic `cpu_reference_v2` only after the formulation/protocol is frozen; keep Fixed and Automatic modes and reuse the existing GMM for statistical region hypotheses.
- [ ] Implement an optimized CPU backend only after v2 correctness passes. Optimize the measured dominant stage, emit reference parity deltas, and avoid speculative caching, scheduler, hierarchy, or abstraction work that does not survive the frozen acceleration gate.
- [ ] Version and migrate config, runtime, and UI through the existing validated paths; preserve undo/redo, no-op detection, source/property revision checks, GPU-dirty propagation, and simultaneous face/feature/boundary visualization.
- [ ] Re-run `GEOM-076` eligibility only after the confirmation bundle is independently audited. A failed quality, stability, or acceleration gate leaves atlas consumption blocked and records the negative result.

## Tests
- [ ] Prove every valid interior edge strictly above the frozen high-dihedral threshold is a feature barrier on aligned crease fixtures; threshold equality is non-feature under the shared `GEOM-071` contract. Cover sign/orientation reversal, boundaries, invalid normals, deleted/dangling slots, and `GEOM-071` property equivalence.
- [ ] Prove a planar quad remains one region under either diagonal, a smooth cylinder does not acquire ring boundaries from tessellation, and feature-crossing remeshings recover the same continuous fold/transition curves within the frozen surface-distance tolerance.
- [ ] Compare projected segmentations with label-permutation-invariant area-weighted Rand/variation-of-information metrics and compare boundary curves with symmetric geodesic Hausdorff, precision/recall, length, spur/junction, and integrated `k_g^2` metrics. Never compare raw region IDs across meshes.
- [ ] Include the flat-versus-isometrically-folded negative control to prove intrinsic-only signals cannot satisfy the crease requirement, and an isometric bending test proving boundary fairness is unchanged when `k_g` is unchanged even though `k_n` changes.
- [ ] Cover constant-curvature regions, smooth curvature transitions, ridges/valleys, nearby features, noise, scale, orientation, non-manifold/degenerate/non-finite rejection, determinism, connected regions, and bounded failure diagnostics.
- [ ] Prove optimized-v2 payload parity against reference-v2 under frozen label matching and boundary tolerances for every screening fixture and confirmation cohort; report rather than hide fallback.
- [ ] Prove descriptor/cache reuse is invalidated by positions, topology, normal source, curvature/feature scale, or relevant config revision and is retained across label-only visualization changes.
- [ ] Cover config migration/defaults/invalid combinations, Editor/AgentCli/Programmatic parity, both Fixed and Automatic UI modes, backend identity, stage diagnostics, and feature-network plus final-boundary overlays.
- [ ] Run stable baseline comparisons. The default useful-acceleration killing gate is at most `0.50x` the paired median METHOD-037 runtime on both 100k- and 1M-face confirmation cohorts, no confirmation fixture slower than `0.80x`, no quality gate regression, and no more than `1.25x` peak working memory. Freeze any justified replacement thresholds before optimized implementation, never after seeing confirmation results.

## Docs
- [ ] Update the method manifest/paper/README with selected prior art, v1/v2/backend identities, equations, feature taxonomy, geodesic-versus-normal boundary curvature distinction, triangulation-convergence assumptions, complexity, parameters, diagnostics, and known counterexamples.
- [ ] Add benchmark manifests, frozen protocol, raw result JSON, per-fixture remeshing/feature/performance report, supported or refuted ARA claims, and exact replay commands. Runtime-only numbers are insufficient.
- [ ] Update geometry, runtime/config, Sandbox, and parameterization-roadmap current-state docs only for behavior actually adopted; regenerate module/task/method/benchmark inventories as required.
- [ ] Update `GEOM-076` with the accepted property/continuous-curve contract or close its segmentation-input hypothesis if this task is negative.

## Acceptance criteria
- [ ] A frozen paper/formulation review resolves the extrinsic-feature versus intrinsic-boundary-fairness distinction and selects one candidate with explicit rejection reasons for the others.
- [ ] Mandatory analytic high-dihedral creases are recovered as boundaries, smooth curvature-pattern features meet the frozen continuous-curve precision/recall gates, and final boundaries remain on the accepted feature network except for explicitly diagnosed topological closure paths.
- [ ] Projected region and boundary metrics pass on every disjoint confirmation remeshing family and show convergence or stable bounded error under refinement; exact edge-ID equality is never claimed.
- [ ] The optimized CPU path passes the frozen useful-acceleration, memory, determinism, and reference-v2 parity gates. Otherwise no optimized selector/default is published and the negative result remains inspectable.
- [ ] Fixed and Automatic modes, config/agent/UI parity, canonical face/edge publication, visualization, undo/stale/coherence behavior, and topology preservation remain operational through the accepted path.
- [ ] `GEOM-076` remains blocked from production atlas adoption until this task's independently audited confirmation disposition is positive.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'CurvatureSegmentation|SharpFeature|SandboxCurvatureSegmentation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
INTRINSIC_CURVATURE_PROFILE_COHORT=fixtures \
  python3 tools/benchmark/run_and_seal.py \
    --executable build/ci/bin/IntrinsicCurvatureSegmentationProfile \
    --output <sealed-result-dir> --manifests-root benchmarks \
    --run-id method-038-fixtures --attempt-id attempt-001
python3 tools/benchmark/validate_benchmark_results.py --root <sealed-result-dir> --manifests-root benchmarks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/experiment_custody.py validate --root .
```

## Forbidden changes
- Declaring triangulation independence from one remeshing, visual similarity, raw edge-mask overlap, or unprojected face labels.
- Using an intrinsic-only signal while claiming to detect an isometric/high-dihedral fold, or using normal curvature as a boundary-fairness penalty after this contract is frozen.
- Treating every noisy curvature gradient as a feature, allowing a weak unary term to cross a mandatory high-dihedral barrier without an explicit diagnosed exception, or silently forcing every optional ridge candidate to split a region.
- Replacing the existing GMM, adding a generic feature/segmentation framework, or introducing a public optimized/GPU selector before reference-v2 correctness and frozen parity evidence.
- Tuning thresholds on the confirmation corpus, dropping slow or low-quality fixtures, reporting runtime without quality/memory/backend identity, or making a claim from dirty/unsealed results.
- Feeding boundaries into UV cutting or changing authoritative topology in this task.

## Maturity
- Target: `ParityProven` for the selected `cpu_optimized` implementation against `cpu_reference_v2`, with the complete runtime/config/UI path retained at `Operational`.
- A negative result is a valid closure: keep METHOD-037's current reference available, publish the failed candidate/gate evidence, and do not unblock curvature-guided UV-atlas adoption.
