---
id: METHOD-039
theme: I
depends_on: [METHOD-038]
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "This task materially changes a mesh method, its public CPU result and diagnostics, canonical face/edge property publication, and the existing runtime/config/UI path. geometry.parameterization-optimization does not apply: the result is a same-topology surface partition and no UV, chart, seam, cut-materialization, or parameterization operation is in scope. geometry.support-radius-policy does not apply because that contract is specific to compact-support point-set methods; this task owns a method-local dimensionless surface feature scale."
maturity_target: Operational
---
# METHOD-039 — Feature-network-constrained curvature patch decomposition

## Goal
- Deliver a deterministic, topology-preserving CPU method that partitions an oriented triangle surface into connected, curvature-coherent patches by detecting hard and soft feature lines, forming a conservative seeded oversegmentation, and merging adjacent regions under an explicit curvature-and-boundary energy so every retained boundary is either feature-supported or a diagnosed curvature-change closure boundary.

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
- Validated hard features are a subset of `Gamma`. A soft feature may remain inside a patch when adjacent curvature models are compatible. Conversely, the part of `Gamma` not supported by `F` may contain a closure boundary where different curvature regimes must be separated even though no classical ridge or valley closes the partition.
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
- Soft evidence `F_e in [0,1]` is a length-weighted, inspectable confidence assembled from scale-persistent signed-curvature change, surface-type transition, and coherent ridge/valley responses. The primary formulation uses the METHOD-038 physical-scale integral-invariant intake; non-maximum suppression and hysteresis produce connected line fragments. Every contributing scale and response remains available in diagnostics.
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
  C(R) = min_k sum_{f in R} A_f * [-log(max(p_k(d_f), epsilon))]
         + beta_patch.
  ```

  `beta_patch` is the explicit model-complexity cost per retained patch. The winning component, runner-up margin, descriptor mean/covariance, normalized residual, and area are diagnostics, not hidden state.
- For adjacent regions `Ri` and `Rj`, define the cost of retaining their shared boundary

  ```text
  B_ij = lambda_length * L_ij
       + lambda_turn * G_ij
       - lambda_feature * S_ij,
  ```

  where `L_ij` is surface boundary length, `G_ij` is discrete geodesic-turning cost, and `S_ij` is length-weighted soft-feature support. No normal-curvature penalty is permitted.
- Merge when the total energy decreases:

  ```text
  delta_merge = C(Ri union Rj) - C(Ri) - C(Rj) - B_ij < 0,
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
- Config: version the existing `sandbox.curvature_segmentation` record only after the CPU reference passes method tests. Preserve Fixed/Automatic GMM selection and expose only sensitivity-tested controls: method mode, hard-feature policy/threshold, physical feature scale, patch-complexity cost, and feature-versus-length boundary balance. Seed locations remain debug/test data, not ordinary configuration.
- UI: extend the existing Curvature Segmentation panel rather than adding a panel or service. Show provisional seeds/superpatches on request, detected hard/soft feature evidence, final regions, boundary roles, and the regional merge-energy diagnostics. The legacy reference remains selectable until the new path passes all acceptance criteria.
- Agent/CLI: use exactly the same schema-versioned preview/validate/apply path and configured method request as the editor. No UI-only threshold, mode, or rerun path is permitted.

## Backends
- Backend axis: retain METHOD-037's current CPU reference as `cpu_reference_v1` for regression comparison. Add one deterministic feature-patch CPU reference only after the contract is frozen; do not add an optimized/GPU token or abstraction in this task.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | A full owning oriented triangle surface with finite positions, live triangle-face adjacency, slot-aligned signed principal curvatures or the existing compute path, and optional slot-aligned hard/soft edge evidence. Surface faces and their embedding are semantic inputs. |
| Compatible entity sources | Mesh geometry entities only. Point sets and abstract graphs do not satisfy the embedded triangle-surface contract. |
| RuntimeModule | Extend the existing curvature-segmentation operation and readiness path. Reuse its snapshot, source-revision, stale-result, undo, and atomic-publication behavior; add no module, service, registry, or job type. |
| Config/agent | Migrate the existing schema-versioned curvature-segmentation config through its shared preview/validate/apply path. Config files, Editor, AgentCli, and Programmatic sources produce the same validated request. |
| UI | Extend the existing mesh Curvature Segmentation panel with method selection and feature/superpatch/final-boundary diagnostics after the reference contract passes. No graph or point-cloud placement is applicable because faces are semantic inputs. |
| Publication | Preserve source topology and unrelated properties. Continue face component/region/color and edge boundary/color publication; add the smallest typed soft-feature score and boundary-role properties needed to distinguish `F` from `Gamma`. Publication is same-cardinality and undoable. |
| End-to-end tests | Cover a mesh entity from config/agent/editor request through snapshot execution, stale-result rejection, atomic property publication, undo/redo, property-revision visibility, and simultaneous feature/final-boundary visualization. |

## Slice plan
- **Slice A — Freeze the practical contract and oracle fixtures.** Bind the inherited evidence boundary to METHOD-038's retired immutable records, record the exact integral-invariant and ridge/valley equations used, define the supplied feature-evidence seam, freeze generated analytic fixtures/metrics, and add no production selector.
- **Slice B — Feature evidence CPU reference.** Implement hard-feature consumption plus one primary multi-scale soft-feature detector, with the Hildebrandt-style extremality detector limited to a fixture comparator. Validate feature confidence and line topology independently of patch construction.
- **Slice C — Oracle-driven grow/merge patch reference.** Implement deterministic seed selection, simultaneous growth, region statistics, RAG merging, boundary roles, and narrow-band refinement. First pass oracle feature evidence, then replay the same tests with detected evidence. Keep all work private to the existing method module until the result contract passes.
- **Slice D — Engine adoption and evidence.** Add the accepted mode to the existing config/runtime/UI path, publish the distinct feature and final-boundary properties, seal quality/stability/smoke results, and retain `cpu_reference_v1` as an explicit fallback/comparison lane.

## Required changes
- [ ] After the declared dependency is satisfied, bind intake and fixture lineage to METHOD-038's immutable retired evidence paths without changing its protocols, raw results, bundles, audits, or claims.
- [ ] Expand `methods/geometry/curvature_segmentation/feature_aligned_intake.md` with the exact selected feature-response, non-maximum-suppression, hysteresis, seed-cost, region-cost, merge, boundary-role, and stopping equations; mark the formulation as an established-method synthesis rather than a novelty claim.
- [ ] Add generated analytic fixtures with oracle hard/soft feature evidence and exact/reference patch boundaries for a plane, cylinder, sphere, strict-threshold folds, smooth signed-curvature transition, ridge, valley, nearby feature pair, junction, open boundary, disconnected surfaces, and a same-curvature false-boundary control.
- [ ] Add the smallest supplied-feature test seam using spans/plain records; do not introduce an interface or public feature-network framework. Reject slot-count mismatches, non-finite confidences/descriptors, unsupported submesh views, and invalid topology with explicit diagnostics.
- [ ] Implement the primary physical-scale smooth-feature evidence path and consume `Geometry.HalfedgeMesh.Features` for hard facts. Report per-signal contributions, persistence scales, suppression/hysteresis decisions, endpoints, junctions, and rejected fragments.
- [ ] Implement deterministic seed selection and simultaneous face growth behind hard barriers, including stable tie-breaking and complete provisional-front diagnostics.
- [ ] Implement area-weighted regional sufficient statistics, the frozen `C(R)` and `B_ij` terms, deterministic adjacency updates, energy-decreasing merges, and final local-optimum checks without adding a generic clustering or graph-optimization layer.
- [ ] Classify and publish final boundary roles independently from feature evidence. Preserve topology and existing component/region/boundary outputs; introduce only the minimum additional edge properties required for soft evidence and boundary role.
- [ ] Add bounded topology-preserving boundary refinement with surface length and discrete geodesic turning. Do not use normal curvature, triangle splitting, or vertex relocation.
- [ ] Perturb seeds, traversal order, seed density, mesh diagonal phase, scale, and noise. Reject the local-merge formulation if the final projected patches exceed the frozen stability tolerances; record the global-correlation-clustering escalation as a separate task only after such a failure.
- [ ] Version and migrate the existing config only after the reference gates pass, then route runtime, agent/CLI, UI, publication, undo/stale checks, and visualization through the same validated method request.
- [ ] Extend the existing curvature profile runner and manifests with generated quality/stability/smoke cohorts. Record quality, topology, determinism, stage work/timings, and peak working-set diagnostics; do not state or gate a speedup.
- [ ] Keep major-stage complexity at sparse surface-graph scale (`O(F + E)` storage and no dense all-pairs face matrix). Any observed superlinear stage must be diagnosed before adoption.

## Tests
- [ ] Feature controls: plane, sphere, and cylinder produce no spurious interior hard feature; the shared strict-threshold fold contract remains exact; smooth transition, ridge, and valley fixtures recover their reference line within the frozen tolerance; orientation reversal changes only signed ridge/valley naming, not feature support or final partition.
- [ ] Oracle isolation: supplied exact feature evidence drives the expected patch result even when the computed detector is disabled, and deliberately corrupted detector evidence changes only the detector-integrated lane. This distinguishes feature-detection failure from patch-completion failure.
- [ ] Growth controls: stable tie-breaking makes repeated runs and face-traversal permutations identical; hard barriers are never crossed; every live face receives exactly one provisional region; disconnected components and open boundaries are handled independently.
- [ ] Merge controls: multiple seeds on one constant-curvature plane/cylinder merge to one patch; same-curvature regions separated only by a soft feature merge when the data/boundary energy says so; genuinely different curvature regimes remain separate; no negative `delta_merge` remains at termination.
- [ ] Boundary legitimacy: every final boundary transition is a hard feature, a soft-feature-supported cut, or a curvature-closure cut whose removal would increase the frozen energy. No unsupported seed front survives.
- [ ] Partition validity: final patches are non-empty and face-connected, final boundary arcs close, meet at diagnosed junctions, or terminate at source boundaries, and output slot counts match the untouched source topology.
- [ ] Stability: on METHOD-038 alternate diagonals plus bounded seed-location/density perturbations, area-weighted variation of information is at most `0.01` and symmetric projected boundary distance is at most `0.02 D`; the plane/cylinder controls remain one patch with zero interior final boundary.
- [ ] Smooth-transition quality: the existing tanh transition produces two connected patches and a final boundary within `0.02 D` of the exact `x=0` curve without any hard-feature input.
- [ ] Robustness: cover empty input, unsupported views, non-triangle faces, zero-area/duplicate geometry, non-manifold or dangling adjacency, non-finite positions/curvatures/feature scores, unusable normals, very small regions, and failed GMM likelihood evaluation with explicit fail-closed status/diagnostics.
- [ ] Publication/coherence: existing properties retain exact slot/cardinality semantics, unrelated properties/topology remain unchanged, new feature/boundary-role properties advance canonical revisions, stale results do not publish, and undo/redo restores all touched properties atomically.
- [ ] Control-surface parity: config-file, Editor, AgentCli, and Programmatic requests produce the same validated params/result; invalid combinations fail before execution; the UI distinguishes detected features, provisional superpatches, and final boundary roles.
- [ ] Complexity/health: generated smoke and 100k-face cohorts complete without a dense `O(F^2)` allocation, report nonnegative stage timings and bounded working-set diagnostics, and preserve deterministic payloads. These are health checks, not performance claims.

## Docs
- [ ] Update `methods/geometry/curvature_segmentation/{paper.md,feature_aligned_intake.md,README.md,method.yaml}` with the selected known formulation, input/output units, `F` versus `Gamma`, parameters, statuses, diagnostics, complexity, and limitations.
- [ ] Document the interpretation of Fixed/Automatic GMM selection: components are curvature hypotheses, while final patches are connected energy-selected regions and may merge neighboring over-fitted hypotheses.
- [ ] Document seed behavior and the stability gate prominently: seeds initialize an oversegmentation and do not define final boundaries.
- [ ] Document hard/soft/closure boundary roles and the fact that soft feature lines may remain internal while curvature closures may occur away from a classical line.
- [ ] Add benchmark manifests and replay instructions for generated feature, patch-quality, seed-stability, and health cohorts. Any quantitative current-state result entering method docs must first have a matching ARA claim/evidence binding.
- [ ] Update geometry/runtime/Sandbox current-state docs and generated module inventory only if their implemented surfaces change; keep roadmap language explicitly future-tense until adoption.

## Acceptance criteria
- [ ] The selected equations and parameters are fixed before the integrated quality run, with stable citations and explicit failure behavior; no novelty claim is made.
- [ ] With oracle feature evidence, every analytic fixture produces the expected connected patch count and boundary topology, demonstrating that remaining closure cuts are solved independently of detector quality.
- [ ] With computed evidence, mandatory hard folds are never crossed, plane/cylinder controls remain unsplit, and the smooth transition/ridge/valley controls pass their frozen line and patch tolerances.
- [ ] Extra seeds on homogeneous regions disappear during merging, while curvature-incompatible neighboring regions remain separated; every surviving non-feature boundary has a positive recorded curvature-model justification.
- [ ] The accepted final partition passes the seed, traversal, diagonal, scale, and noise stability gates. Failure of this criterion blocks adoption and triggers a separately scoped global-optimization decision rather than threshold tuning on the failed cases.
- [ ] Results remain deterministic, patches connected, topology/cardinality unchanged, diagnostics complete, and malformed inputs fail closed.
- [ ] The existing config/agent/runtime/UI path can execute and inspect the accepted reference mode without a new service/framework, while the v1 reference remains available for comparison/fallback.
- [ ] Claim-grade evidence supports only the implemented correctness, stability, and bounded health statements. No novelty, parameterization, GPU, or speedup statement is published.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke IntrinsicCurvatureSegmentationProfile
ctest --test-dir build/ci --output-on-failure -R 'CurvatureSegmentation|HalfedgeMeshFeatures|SandboxCurvatureSegmentation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
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
- Target: `Operational` for one deterministic CPU reference through the existing runtime/config/UI/publication path, with quality and stability evidence against generated controls.
- No optimized CPU or GPU follow-up is owed by default. A performance backend opens only after accepted reference profiling identifies a concrete bottleneck and a separate task freezes its parity/baseline contract.
