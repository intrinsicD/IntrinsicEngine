---
id: METHOD-039
theme: I
depends_on: [METHOD-038]
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-11T09:56:56Z"
evidence_skip_reason: "Interactive user-directed completion; the reviewed diff, repository gates, ARA ledger, and sealed non-claim benchmark rows are the evidence."
template: micro
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "This task materially changes a mesh method and its public CPU result and diagnostics, while its failed stability gate deliberately prevents canonical face/edge property publication and runtime/config/UI adoption. geometry.parameterization-optimization does not apply: the result is a same-topology diagnostic partition and no UV, chart, seam, cut-materialization, or parameterization operation is in scope. geometry.support-radius-policy does not apply because that contract is specific to compact-support point-set methods; this task owns a method-local dimensionless surface feature scale."
maturity_target: CPUContracted
---
# METHOD-039 — Feature-network-constrained curvature patch decomposition

## Goal
- Deliver a deterministic, topology-preserving CPU method that partitions an oriented triangle surface into connected, curvature-coherent patches by detecting hard and soft feature lines, forming a conservative seeded oversegmentation, and merging adjacent regions under an explicit curvature-and-boundary energy so every retained boundary is either feature-supported or a diagnosed curvature-change closure boundary.

## Status
- Completed on 2026-09-03 at `CPUContracted` by explicit user direction while
  `REVIEW-004` remains open. This records the operator-approved Theme I
  exception; it does not weaken or satisfy the product-convergence gate for
  unrelated work.
- Implementation and clean-source profile commit: `889126d0`.
- Retirement evidence is indexed by ARA claim C54 and observation O165.
- Slices A and B are complete and reviewed. Slice C now implements an unadopted
  deterministic grow/merge/refine CPU reference and passes the supplied-oracle,
  detected-feature, hard-fold, homogeneous, diagonal, scale, noise, and
  orientation controls. Its one-dual-step seed perturbation exceeds the frozen
  `0.01` variation-of-information gate despite exact full-energy checks after
  every accepted move. ARA claim C45 records this bounded refutation.
- Per the preregistered stop rule, the local formulation was not adopted: no v2
  selector, config/runtime/UI path, or canonical property publication was
  added. The completion slice added only opt-in generated feature,
  quality, refutation, and 100k-face health profiles before retiring the
  negative result. `METHOD-040` is the separately scoped task-local global
  multicut follow-up.

## Non-goals
- No UV generation, atlas/chart construction, seam selection, mesh cutting, vertex duplication, face splitting, or other parameterization work.
- No novelty or publishability claim. This is an engineering synthesis of established feature detection, watershed/region-growing, region-merging, and feature-aligned segmentation ideas. Quantitative evidence supports only the implemented method contract.
- No semantic or object-part segmentation. A patch means a connected region with a statistically coherent signed-curvature descriptor, not a learned object category.
- No requirement that every detected soft ridge, valley, or transition become a patch boundary. Soft features attract or support boundaries; only validated hard creases are mandatory barriers.
- No plane/cylinder/sphere/saddle enum as the segmentation decision rule. Shape index, curvedness, and primitive-like names may be reported as diagnostics, but the decision uses continuous signed-curvature distributions.
- No user-authored seed set as a correctness requirement. Supplied seeds may be a test/debug input, but production seeds are deterministic and the accepted final partition must be stable to bounded seed perturbations.
- No generic segmentation framework, generic graph optimizer, public multicut library, new service/registry/backend hierarchy, or replacement Gaussian-mixture implementation.
- No optimized CPU or GPU backend and no acceleration claim. Performance work begins only if profiling of an accepted reference method demonstrates a concrete need.
- No reinterpretation, retuning, or deletion of METHOD-038's sealed fixture/oracle evidence.

## Context
- Owner/layer: descriptor construction, feature evidence, seeded growth, region statistics, adjacency merging, final boundary roles, and reference diagnostics live in the existing `Geometry.HalfedgeMesh.CurvatureSegmentation` implementation (`geometry -> core`). Runtime continues to own canonical-property binding, validated config, stale-source checks, atomic publication, undo, and visualization wiring. App remains a thin consumer of the existing runtime path.
- METHOD-038 established the controls that motivate this task: validated hard-feature facts distinguish a sufficiently sharp fold even when the supplied curvature field is constant; a smooth cylinder must remain feature- and boundary-free; a smooth signed-curvature transition can define a boundary without a hard crease; and the current Automatic GMM selection can over-partition a simple supplied-curvature fixture. Those observations are bounded by existing ARA claims C40-C43 and are not broadened here.
- The implementation follow-up begins only after METHOD-038 retires its current evidence slice. METHOD-039 consumes the preserved controls and resolves the broad candidate list to the smallest practical feature-first formulation; it does not rewrite the earlier custody records.
- Two networks remain distinct throughout the API and diagnostics:
  - `F`, the detected feature-evidence network: hard crease facts plus optional soft ridge/valley/transition confidence;
  - `Gamma`, the final separating patch-boundary network.
- Validated interior hard-feature transitions are a subset of `Gamma`; a source-boundary hard mark remains feature evidence but cannot separate two incident patches. A soft feature may remain inside a patch when adjacent curvature models are compatible. Conversely, the part of `Gamma` not supported by `F` may contain a closure boundary where different curvature regimes must be separated even though no classical ridge or valley closes the partition.
- Seed growth is an oversegmentation mechanism, not the scientific definition of a patch. Provisional seed fronts are removed unless the final region energy supports them.

### Established method lineage

| Work | Mechanism used here | Disposition |
| --- | --- | --- |
| [Lavoue, Dupont, and Baskurt 2005](https://doi.org/10.1016/j.cad.2004.09.001) | Curvature-pattern classification, region growth, a region-adjacency graph, and merging of similar patches | Primary lineage for the grow-then-merge structure; hard curvature classes are replaced by continuous signed-curvature likelihoods. |
| [Mangan and Whitaker 1999](https://doi.org/10.1109/2945.817348) | Watershed-style surface oversegmentation | Baseline for conservative catchment construction and seed-front diagnostics. |
| [Vieira and Shimada 2005](https://doi.org/10.1016/j.cagd.2005.03.006) | Deterministic surface-mesh region growing | Baseline for seeded face growth and connected-region handling. |
| [Pottmann et al. 2007](https://doi.org/10.1016/j.cagd.2007.07.004) and [Lai et al. 2007](https://doi.org/10.1109/TVCG.2007.19) | Physical-scale integral-invariant curvature/feature responses, persistence, and hysteresis | Preferred smooth-feature evidence family because its scale is explicit and it is designed to suppress tessellation noise. |
| [Hildebrandt, Polthier, and Wardetzky 2005](https://doi.org/10.2312/SGP/SGP05/085-090) | Smoothed ridge/valley extremality lines | Bounded smooth-feature comparator; not a second production path unless it wins the frozen analytic/noise controls. |
| [Zhuang et al. 2017](https://doi.org/10.1007/s41095-016-0071-3) | Feature fragments, anisotropic watershed oversegmentation, and correlation-clustering closure paths | Closest full-pipeline precedent. This task first implements deterministic local RAG merging; global correlation clustering is the named escalation condition, not speculative infrastructure. |
| [Cohen-Steiner, Alliez, and Desbrun 2004](https://doi.org/10.1145/1186562.1015817) | Seed, grow, refit, and relocate region proxies | Initialization/refit precedent; geometric proxies are not adopted as the curvature-region model. |
| [Bonneel et al. 2018](https://doi.org/10.1111/cgf.13549) | Surface Mumford-Shah edge-set optimization | Quality/reference alternative if local merging fails; out of the first implementation because it is a materially larger solver. |

## Selected formulation

### 1. Inputs and dimensionless curvature descriptor

- Consume a full owning oriented triangle mesh, slot-aligned signed principal curvatures `(k1, k2)`, and either supplied feature evidence or evidence computed from the mesh. Faces and surface adjacency are semantic inputs, so this remains a mesh method.
- At physical surface scale `r`, use the dimensionless face descriptor

  ```text
  d_f(r) = (r * k1_f, r * k2_f).
  ```

  Face statistics are area-weighted. Internal accumulation uses `double`; published vector/color properties retain the repository float-storage policy.
- Reuse the existing deterministic Gaussian mixture to obtain component likelihoods `p_k(d_f)`. Fixed and Automatic selection remain inspectable inputs. Final patch merging does not equate "different fitted component ID" with "different patch"; it evaluates the increase in regional model cost.

### 2. Feature evidence before patch completion

- Hard evidence `H_e in {0,1}` comes from `Geometry.HalfedgeMesh.Features` with its existing strict `dihedral > threshold` contract and fail-closed invalid-topology/normal handling. An interior dual transition across `H_e = 1` is non-traversable and non-mergeable.
- Soft evidence `F_e in [0,1]` is an inspectable confidence assembled from scale-persistent signed-curvature change, surface-type transition, and coherent ridge/valley responses. The primary formulation uses the METHOD-038 physical-scale intake; non-maximum suppression and hysteresis produce connected line fragments, and the objective length-weights their contribution. Every contributing scale and response remains available in diagnostics. Pottmann equation (27) remains a reviewed ball--solid volume comparator and is not misnamed as the selected open-surface quadrature.
- Oracle/supplied `H_e` and `F_e` inputs are a required test seam. They let patch completion be verified independently of feature-detection error and avoid diagnosing two coupled algorithms from one failed output.
- Hard and soft evidence are never collapsed into one boolean. Hard evidence constrains topology of the partition; soft evidence changes boundary energy and may be rejected by the region model.

### 3. Deterministic conservative oversegmentation

- Remove hard-barrier transitions from the face-dual traversal graph.
- Within each remaining connected cell, select seeds by deterministic farthest-point sampling under a traversal cost combining surface distance, descriptor change, and soft-feature crossing cost. Seed spacing is derived from the physical feature scale; equal candidates break ties by stable face slot. A uniform cell always has at least one seed.
- Grow all seeds simultaneously with a stable priority queue. A face is claimed by the lowest accumulated cost, with stable seed/face tie-breaking. Fronts meeting other fronts create provisional boundaries; no provisional boundary is yet authoritative.
- Record seed count, seed faces, growth cost, front length, and the reason each dual transition was blocked or penalized. Tests may replace or perturb the seeds while keeping every later stage unchanged.

### 4. Curvature-aware region model and merging

- For a connected provisional region `R`, define the area-weighted regional cost

  ```text
  C(R) = min_k sum_{f in R} (A_f/D^2) * [-log(max(p_k(d_f), epsilon))]
         + beta_patch.
  ```

  `beta_patch` is the explicit model-complexity cost per retained patch. The winning component, runner-up margin, descriptor mean/covariance, normalized residual, and area are diagnostics, not hidden state.
- For adjacent regions `Ri` and `Rj`, define the cost of retaining their shared boundary

  ```text
  B_ij(P) = B(Gamma(P)) - B(Gamma(P after Ri union Rj)),
  B(Gamma) = lambda_length * L(Gamma)
           + lambda_turn * G(Gamma)
           - lambda_feature * S(Gamma),
  ```

  where `L` is bbox-normalized surface boundary length, `G` is the discrete intrinsic geodesic-turning quadrature, and `S` is bbox-normalized length-weighted soft-feature support. Defining `B_ij` as the exact before/after boundary credit includes turning changes at affected endpoints and junctions. No normal-curvature penalty is permitted.
- Merge when the total energy decreases:

  ```text
  delta_merge = C(Ri union Rj) - C(Ri) - C(Rj) - B_ij(P) < 0,
  ```

  unless the adjacency contains a hard barrier. Recompute only neighboring region statistics after each merge. Select the most negative admissible delta with stable region-ID tie-breaking and stop when none remains.
- The reference result is therefore a deterministic local optimum of one stated energy. A surviving non-feature boundary is classified as `closure` only when its regional model-cost increase justifies retaining it. A provisional seed front with neither feature support nor curvature evidence must disappear.

### 5. Final boundary network and roles

- Derive `Gamma` from adjacent final face-region IDs. Its discrete topology is automatically a valid partition boundary: interior arcs close, meet at junctions, or terminate at the source mesh boundary.
- Classify every final boundary transition as `hard_feature`, `soft_feature_supported`, or `curvature_closure`; publish the role separately from the final boundary mask so detected features and separating cuts cannot be confused.
- Run only bounded, topology-preserving narrow-band label refinement using the same energy. Do not split triangles or move source vertices. If edge-constrained paths fail the frozen projected-distance gate, stop and author a separate continuous barycentric-curve task; do not hide triangle cutting in this method.

### 6. Global-optimization escalation gate

- The local RAG merge is accepted only if bounded changes to seed placement, seed density, face traversal order, and alternate diagonals stay within the frozen partition/boundary tolerances.
- If that gate fails, retain the negative result and stop positive adoption. A follow-up may implement task-local correlation clustering/multicut over the same superpatch RAG, using Zhuang et al. as the baseline. This task must not grow a generic graph-optimization framework in response to a failed fixture.

### Right-sizing decision

- Element: a public feature-network abstraction, generic watershed/RAG package, or multicut framework would each have one production consumer in this task and therefore fail the present-consumer test.
- Simpler alternative: keep feature samples, provisional regions, sufficient statistics, and adjacency records as private plain structs/free functions in the existing curvature-segmentation implementation; expose only the supplied spans/plain result fields needed by callers and tests.
- Blast radius: one geometry method module, its existing runtime/config/UI consumer path, focused method tests, and the existing profile runner. No new layer or dependency edge is planned; strict layering remains the authority.
- Reintroduction trigger: extract a reusable feature-network or graph-optimization module only when a second production method needs the same stable contract, or when the seed-stability gate has first refuted local merging and a separately reviewed global-solver task is opened.

## Control surfaces
- Config: not entered. The rejected candidate has no token or tunable state;
  `sandbox.curvature_segmentation` remains the unchanged v1 control surface.
- UI: not entered. The existing Curvature Segmentation panel remains v1-only;
  no private diagnostic execution path was added.
- Agent/CLI: not entered. A future accepted candidate must use the same schema-
  versioned preview/validate/apply path as the editor.

## Backends
- Backend axis: METHOD-037's `cpu_reference_v1` remains the sole production
  reference. METHOD-039 added one deterministic CPU diagnostic comparator but
  no selectable v2, optimized, or GPU token and no backend abstraction.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | A full owning oriented triangle surface with finite positions, live triangle-face adjacency, slot-aligned signed principal curvatures or the existing compute path, and optional slot-aligned hard/soft edge evidence. Surface faces and their embedding are semantic inputs. |
| Compatible entity sources | Mesh geometry entities only. Point sets and abstract graphs do not satisfy the embedded triangle-surface contract. |
| RuntimeModule | Unchanged. The failed candidate never enters the existing curvature-segmentation operation; a future accepted method requires a separately scoped adoption task. |
| Config/agent | Unchanged v1 schema and validated path; no rejected-candidate token exists. |
| UI | Unchanged v1 Curvature Segmentation panel; no graph or point-cloud placement is applicable because faces are semantic inputs. |
| Publication | Diagnostic result only. No new canonical property is published; the existing v1 face/edge properties and topology remain unchanged. |
| End-to-end tests | Not entered because adoption failed. Existing v1 runtime/publication/coherence coverage remains authoritative. |

## Slice plan
- **Slice A — Freeze the practical contract and oracle fixtures (complete).** Bind the inherited evidence boundary to METHOD-038's retired immutable records, record the exact integral-invariant and ridge/valley equations used, define the supplied feature-evidence seam, freeze generated analytic fixtures/metrics, and add no production selector.
- **Slice B — Feature evidence CPU reference (complete).** Implement hard-feature consumption plus one primary multi-scale soft-feature detector, with the Hildebrandt-style extremality detector limited to a fixture comparator. Validate feature confidence and line topology independently of patch construction.
- **Slice C — Oracle-driven grow/merge patch reference (complete; local formulation refuted).** The unadopted deterministic CPU reference passes its oracle and computed-evidence controls, but the frozen one-step seed-location gate rejects positive adoption. The failure remains executable rather than being tuned away.
- **Slice D — Negative-result profiling and retirement (complete).** A dedicated
  opt-in profiler records generated feature/quality/refutation smoke results
  and a 100k-face sparse-storage health cohort without changing production
  behavior.
- **Engine adoption (withdrawn).** The rejected local formulation does not
  enter config/runtime/UI/property adoption. A positive METHOD-040 verdict must
  open a separately scoped adoption task; `cpu_reference_v1` remains unchanged.

## Required changes
- [x] After the declared dependency is satisfied, bind intake and fixture lineage to METHOD-038's immutable retired evidence paths without changing its protocols, raw results, bundles, audits, or claims.
- [x] Expand `methods/geometry/curvature_segmentation/feature_aligned_intake.md` with the exact selected feature-response, non-maximum-suppression, hysteresis, seed-cost, region-cost, merge, boundary-role, and stopping equations; mark the formulation as an established-method synthesis rather than a novelty claim.
- [x] Add generated analytic fixtures with oracle hard/soft feature evidence and exact/reference patch boundaries for a plane, cylinder, sphere, strict-threshold folds, smooth signed-curvature transition, ridge, valley, nearby feature pair, junction, open boundary, disconnected surfaces, and a same-curvature false-boundary control.
- [x] Add the smallest supplied-feature test seam using spans/plain records; do not introduce an interface or public feature-network framework. Reject slot-count mismatches, non-finite confidences/descriptors, unsupported submesh views, and invalid topology with explicit diagnostics.
- [x] Implement the primary physical-scale smooth-feature evidence path and consume `Geometry.HalfedgeMesh.Features` for hard facts. Report per-signal contributions, persistence scales, suppression/hysteresis decisions, endpoints, junctions, and rejected fragments.
- [x] Implement deterministic seed selection and simultaneous face growth behind hard barriers, including stable tie-breaking and complete provisional-front diagnostics.
- [x] Implement area-weighted regional sufficient statistics, the frozen `C(R)` and `B_ij` terms, deterministic adjacency updates, energy-decreasing merges, and final local-optimum checks without adding a generic clustering or graph-optimization layer.
- [x] Classify and return final boundary roles independently from feature evidence in the companion result. Preserve topology and the sealed v1 component/region/boundary outputs.
- [x] Add bounded topology-preserving boundary refinement with surface length and discrete geodesic turning. Do not use normal curvature, triangle splitting, or vertex relocation.
- [x] Execute the frozen seed-density/location, diagonal, scale, noise, and orientation checks. The one-step seed-location perturbation rejects the local formulation, so `METHOD-040` owns the separately scoped global-correlation-clustering escalation.
- [x] Keep config migration and canonical property publication behind an accepted reference gate. This positive-only work was intentionally not entered because the Slice C local formulation was rejected.
- [x] Add a dedicated opt-in curvature-patch profile runner and manifests with generated quality/stability/smoke cohorts. Keep METHOD-038's custody-bound runner byte-stable; record quality, topology, determinism, stage work/timings, and peak working-set diagnostics without stating or gating a speedup.
- [x] Keep major-stage storage at sparse surface-graph scale (`O(F + E)` slot arrays/adjacency and no dense all-pairs face matrix). No adoption or performance claim is made.

## Tests
- [x] Feature controls: plane, sphere, and cylinder produce no spurious interior hard feature; the shared strict-threshold fold contract remains exact; smooth transition, ridge, and valley fixtures recover their reference line within the frozen tolerance; orientation reversal changes only signed ridge/valley naming, not feature support. Final-partition orientation parity also passes.
- [x] Oracle isolation: supplied exact feature evidence drives the expected patch result when computed evidence is bypassed, and deliberately corrupted soft evidence changes only the integrated lane.
- [x] Growth controls: stable tie-breaking makes repeated runs identical; hard barriers are never crossed; every live face receives one provisional region; disconnected components and open boundaries are handled independently. A distinct storage-order permutation was not pursued after the earlier frozen seed gate rejected adoption.
- [x] Merge controls: multiple seeds on a constant-curvature control merge to one patch; same-curvature false boundaries disappear; genuinely different regimes remain separate; exact recomputation agrees with every accepted delta and no negative admissible delta remains at termination.
- [x] Boundary legitimacy: every final boundary transition is hard, soft-supported, or a curvature closure with nonnegative merge delta. Unsupported seed fronts disappear on the accepted oracle controls.
- [x] Partition validity: final patches are non-empty and face-connected, final boundary arcs close, meet at diagnosed junctions, or terminate at source boundaries, and output slot counts match source topology.
- [x] Stability gate executed: seed density, alternate diagonals, scale, bounded noise, and orientation pass, while the one-dual-step seed-location case exceeds VI `0.01` and refutes the local formulation. Plane/cylinder remain one patch with zero interior boundary.
- [x] Smooth-transition quality: the existing tanh transition produces two connected patches and a final boundary within `0.02 D` of the exact `x=0` curve without hard-feature input.
- [x] Robustness: production tests cover empty input, slot mismatches, invalid params/seeds, and non-finite/invalidly ordered curvature; the broader Slice A malformed mesh/evidence preflight remains executable. Further production expansion stopped with the rejected formulation.
- [x] Publication/coherence was intentionally not entered: no new properties or writeback path exist, while the unchanged v1 publication/coherence contract remains covered by its existing tests.
- [x] Control-surface parity was intentionally not entered: no rejected-candidate token or private execution path was added, and the existing v1 config/runtime/UI lane remains unchanged.
- [x] Complexity/health: generated smoke and 100k-face cohorts complete without a dense `O(F^2)` allocation, report nonnegative stage timings and bounded working-set diagnostics, and preserve deterministic payloads. These are health checks, not performance claims.

## Docs
- [x] Update `methods/geometry/curvature_segmentation/{paper.md,feature_aligned_intake.md,README.md,method.yaml}` with the selected known formulation, input/output units, `F` versus `Gamma`, parameters, statuses, diagnostics, complexity, and limitations.
- [x] Document the interpretation of Fixed/Automatic GMM selection: components are curvature hypotheses, while final patches are connected energy-selected regions and may merge neighboring over-fitted hypotheses.
- [x] Document seed behavior and the stability gate prominently: seeds initialize an oversegmentation and do not define final boundaries.
- [x] Document hard/soft/closure boundary roles and the fact that soft feature lines may remain internal while curvature closures may occur away from a classical line.
- [x] Add benchmark manifests and replay instructions for generated feature, patch-quality, seed-stability, and health cohorts. Any quantitative current-state result entering method docs must first have a matching ARA claim/evidence binding.
- [x] Update method current-state docs and the generated module inventory for the unadopted companion surface. Runtime/Sandbox docs remain unchanged because adoption did not occur.

## Acceptance criteria
- [x] The selected equations and parameters are fixed before the integrated quality run, with stable citations and explicit failure behavior; no novelty claim is made.
- [x] With oracle feature evidence, every analytic fixture produces the expected connected patch count and boundary topology, demonstrating that closure cuts can be tested independently of detector quality.
- [x] With computed evidence, mandatory hard folds are never crossed, plane/cylinder controls remain unsplit, and the smooth transition/ridge/valley controls pass their frozen line and patch tolerances.
- [x] Extra seeds on homogeneous regions disappear during merging, while curvature-incompatible neighboring regions remain separated; every surviving non-feature boundary reports its curvature-model merge delta.
- [x] Execute the adoption stability gate. The bounded seed-location case fails, so positive adoption is blocked and `METHOD-040` records the separately scoped global-optimization decision without threshold tuning.
- [x] Results remain deterministic, patches connected, topology/cardinality unchanged, diagnostics complete, and malformed inputs fail closed across the bounded implemented controls.
- [x] The rejected candidate was not exposed through config/agent/runtime/UI; no new service/framework was added and the v1 reference remains the unchanged production path.
- [x] Clean-source sealed non-claim benchmark rows and ARA claim C54 preserve
  only the implemented correctness controls, stability refutation, and bounded
  health result. No novelty, parameterization, GPU, or speedup statement is
  published.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke IntrinsicCurvaturePatchProfile
ctest --test-dir build/ci --output-on-failure -R 'CurvatureSegmentation|CurvaturePatch|HalfedgeMeshFeatures|SandboxCurvatureSegmentation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1

cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1

python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
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
- Treating every detected feature as a mandatory cut, treating every GMM component as a patch, or retaining a boundary merely because two seed fronts met.
- Crossing a validated hard feature, silently weakening the strict dihedral-threshold contract, or merging disconnected surface components.
- Using raw region IDs, face slots, or edge-mask overlap across remeshings as a stability metric; compare projected partitions and boundaries with label-permutation-invariant metrics.
- Tuning thresholds after reading the frozen integrated-quality results, deleting failed fixtures, or converting a failed local-merge stability gate into an unreviewed global solver.
- Adding a generic feature-network, watershed, RAG, multicut, service, registry, scheduler, or backend abstraction without a present second consumer and a separately reviewed task.
- Publishing a topology-changing result, materializing cuts, or introducing parameterization/UV behavior.
- Adding optimized CPU/GPU work or claiming a performance improvement before a separate measured task establishes reference parity and a baseline.

## Maturity
- Reached: `CPUContracted` for the standalone feature stage and preserved local
  diagnostic comparator. The seed-sensitive local candidate did not reach
  `Operational`; the v1 production path remains canonical.
- No optimized CPU or GPU follow-up is owed. A performance backend may open
  only after an accepted reference method has parity evidence and a separate
  task freezes its baseline contract.
- `METHOD-040` owns the next CPU-reference attempt. If it produces a positive
  global-partition verdict, a separately scoped task must own selector,
  config/runtime/UI, and canonical-property adoption.
