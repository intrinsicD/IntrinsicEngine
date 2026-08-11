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
claimed_at: "2026-08-11T08:33:51Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "The completed slice adds deterministic mesh-surface fixture and profiling evidence around the existing METHOD-037 CPU reference without changing its public method, same-cardinality publication, runtime/config/UI path, or topology. geometry.parameterization-optimization does not apply because no UV, chart, seam, cut, or parameterization operation is implemented; GEOM-076 remains re-gated on METHOD-039. geometry.support-radius-policy is point-set/LOP-specific and does not govern this bounded surface-control intake."
maturity_target: CPUContracted
---
# METHOD-038 — Feature-aligned, remeshing-stable curvature segmentation evidence intake

## Status

- Completed on 2026-08-11 at `CPUContracted` for the deterministic profiling,
  fixture, oracle, and preregistration surface described below. The original
  `ParityProven` v2/optimized-backend target was not reached and is not claimed.
- METHOD-037 `cpu_reference_v1` remains the only operational production
  segmentation path. This task did not select or implement candidate A-D, did
  not change runtime/config/UI defaults, and did not produce a remeshing-stable
  feature-aligned backend.
- The practical feature-first patch method is now owned by METHOD-039. It
  consumes this task's immutable controls but selects a smaller deterministic
  feature-network, seeded-oversegmentation, and curvature-aware merge contract.
- GEOM-076 remains blocked on METHOD-039 rather than being unblocked by this
  evidence-only retirement. No parameterization work is part of this closure.
- Scientific implementation checkpoints: `874d09c3`, `a944b1a1`, `f622cd0e`,
  and `251f2dab`; final pre-retirement evidence checkpoint: `e103e0c6`.
  Completion commit: pending historical seal.
  The exact retirement revision is recorded by
  `tasks/evidence/METHOD-038/seal.yaml` after the closure commit.

## Goal

- Establish a deterministic, replayable evidence foundation for deciding a
  later feature-aligned curvature-patch method: measure the current v1 stages,
  freeze continuous-surface comparison vocabulary and candidate-killing rules,
  and validate analytic controls that separate hard creases, smooth surfaces,
  and smooth curvature-transition boundaries without changing production.

## Non-goals

- No `cpu_reference_v2`, optimized CPU backend, GPU backend, public backend
  selector, production-default change, or performance/acceleration claim.
- No claim that any candidate A-D was executed, selected, rejected, or shown to
  converge across remeshings. The controls validate fixtures and oracles only.
- No UV generation, chart construction, seam selection, mesh cutting, vertex
  duplication, topology mutation, parameterization, or atlas-quality claim.
- No novelty claim. The intake records established feature-sensitive,
  curvature-segmentation, and continuous-boundary literature for engineering
  use.
- No reinterpretation of Fixed/Automatic Gaussian-mixture components as final
  patches. C40-C41 preserve the observed Automatic over-selection as a bounded
  negative result.

## Context

- Owner/layer: the observational timing fields and opt-in profiling fixtures
  live with `Geometry.HalfedgeMesh.CurvatureSegmentation` and its benchmark
  runner (`geometry -> core`). Runtime remains the owner of the unchanged v1
  config, publication, undo/stale checks, and Sandbox UI path.
- METHOD-037 averages signed vertex `(k1,k2)` onto faces, fits the existing
  deterministic Gaussian mixture, and spatially regularizes labels on the
  source face-dual graph. This task measures and probes that implementation; it
  does not replace it.
- GEOM-071 supplies the strict reusable hard-feature fact used by the fold
  controls: a valid interior edge is a feature exactly when
  `dihedral_angle > 45 degrees`; equality is not a feature.
- "Remeshing stability" remains defined as label-permutation-invariant region
  agreement and continuous surface-boundary distance after projection to a
  common embedded reference. No combinatorial face/edge-ID equality is claimed.
- Boundary fairness vocabulary distinguishes geodesic curvature `k_g` from
  normal curvature `k_n`; future surface-boundary regularization may use length
  and `k_g`, but must not penalize `k_n` merely because the support surface
  bends.

## Completed evidence

- C40: on the deterministic paired supplied-curvature 10k control, Automatic
  selected four components and failed the exact-two population gate while the
  matched Fixed control selected two. This refutes an exact-count assumption;
  it does not establish general Automatic quality.
- C41: the bounded fixture lanes preserve their declared Fixed counts while
  the Automatic lane remains count-unstable. The recorded planar continuous
  boundary and paired analytic-sphere rows are fixture contracts, not a broad
  remeshing result.
- C42: paired-diagonal `30/45/60`-degree folds produced exact hard-feature
  counts `0/0/24`, zero feature-mask error, at most `0.000000038` normalized
  paired-edge-length delta, and identical one-component, one-region,
  zero-boundary v1 negative-control payloads.
- C43: both open-cylinder phases produced zero hard features, one v1 region,
  no false ring boundary, and exact payload parity. Both paired-diagonal smooth
  `q(x)=0.5(1+tanh(x/0.08))` phases recovered the exact 24-edge `x=0`
  reference mask with two endpoints and no junction. The final task-bound
  replay passes all 17 frozen gates and remains `claim_authorized: false`.
- Every claim above is bounded to the exact sealed result, raw rows, portable
  bundle, and independent audit named in ARA C40-C43. No result authorizes a
  candidate-selection, acceleration, production, parameterization, or novelty
  statement.

## Candidate intake retained for METHOD-039

- A feature-first formulation remains the smallest practical direction: detect
  hard and soft feature evidence, create a conservative connected
  oversegmentation, and remove unsupported seed fronts by curvature-aware
  region merging.
- The broader alternatives remain references, not unfinished METHOD-038 work:
  feature-sensitive coarse-to-fine remeshing, surface Mumford-Shah edge sets,
  and quadric proxies. METHOD-039 freezes its own selected equations and gates.
- The deterministic controls are immutable inputs to METHOD-039. It may not
  delete failed rows, retune these oracles, or broaden C40-C43.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | An owning oriented triangle surface with finite positions, live face adjacency, and supplied or computed signed principal curvatures. The completed controls additionally use exact analytic geometry and paired triangulations. |
| Compatible entity sources | Mesh geometry entities only; faces and embedded surface topology are semantic inputs. |
| RuntimeModule | No new path. METHOD-037's existing curvature-segmentation operation remains operational and unchanged. METHOD-039 owns any future accepted mode. |
| Config/agent | No schema or default change. Fixed and Automatic v1 requests continue through the existing validated shared config path. METHOD-039 owns future migration. |
| UI | No UI change. Existing v1 controls and diagnostics remain current. METHOD-039 owns future feature/superpatch/final-boundary inspection. |
| Publication | No new property. Existing same-cardinality face component/region/color and edge boundary/color publication remains unchanged; topology and unrelated properties are preserved. |
| End-to-end tests | Existing METHOD-037 runtime/config/UI tests remain the production proof. This task adds opt-in profiler controls and replayable evidence only. METHOD-039 owns future end-to-end adoption tests. |

## Required changes

- [x] Record stable primary citations, exact descriptor/feature/boundary
      vocabulary, physical-scale conventions, projected comparison metrics,
      candidate families, and killing order in
      `methods/geometry/curvature_segmentation/feature_aligned_intake.md`.
- [x] Add observational stage and per-candidate timings without changing labels,
      energy, selection, publication, or runtime defaults.
- [x] Add deterministic supplied-curvature profiler cohorts and stable
      population, geometry, feature, orientation, topology, and continuous
      boundary diagnostics.
- [x] Freeze and replay the Fixed/Automatic 10k controls, paired analytic-sphere
      and planar-transition fixtures, strict-threshold fold controls, open
      cylinder, and smooth signed-curvature transition.
- [x] Preserve schema-rejected, negative, superseded, and accepted runs rather
      than tuning the protocol after results.
- [x] Record bounded supported/refuted ARA claims C40-C43 and keep every claim
      tied to exact repository evidence.
- [x] Transfer feature-first patch implementation and operational adoption to
      METHOD-039; re-gate GEOM-076 on that accepted future result.

## Tests

- [x] Exact two-component population diagnostics distinguish Fixed success from
      Automatic over-selection on the deterministic supplied-curvature control.
- [x] Deterministic minimum-region cleanup regression covers the changed private
      profiling path without changing the public result contract.
- [x] Paired folds prove the shared strict hard-feature threshold and preserve
      the constant-curvature v1 negative-control payload.
- [x] Paired open cylinders prove zero spurious hard features and zero false
      v1 ring boundaries across angular phase.
- [x] Paired smooth-transition graphs prove the analytic curvature contract,
      exact `x=0` mask, label-permutation-invariant face agreement, endpoint and
      junction counts, and bounded continuous reference distance.
- [x] Focused curvature/feature/Sandbox tests, the complete default
      CPU-supported suite, isolated ASan and UBSan CPU suites, benchmark/result
      validators, and strict structural checks pass for the final surface.

## Docs

- [x] Document the v1 formulation and diagnostics separately from the
      feature-aligned intake so evidence work cannot be mistaken for an adopted
      backend.
- [x] Document protocols, replay commands, exact control outcomes, limitations,
      and C40-C43 evidence paths in the method package and ARA ledger.
- [x] Preserve all superseded protocols/runs with reasons and immutable paths.
- [x] Synchronize task indexes, dependency state, retirement log, and session
      brief; no architecture/module inventory changes are required because no
      public module surface changes in the retirement patch.

## Acceptance criteria

- [x] The current v1 production behavior remains unchanged and explicitly named
      as the only operational backend.
- [x] Deterministic profiling and fixture rows can be replayed with declared
      inputs, exact gates, machine-readable diagnostics, and no hidden threshold
      changes.
- [x] Fold, cylinder, and smooth-transition controls pass their frozen analytic
      and topology oracles, while the Automatic count failure remains visible.
- [x] Claims are limited to fixture/oracle integrity and the bounded Automatic
      negative result; no candidate, remeshing-convergence, speedup, runtime,
      GPU, parameterization, or novelty claim is made.
- [x] Claim-grade command receipts, frozen protocol/run/bundle, independent
      audit, handoff, fixed-surface review, completion report, and historical
      seal validate before final retirement.
- [x] Unfinished algorithm work has one explicit owner: `Operational` owned by
      `METHOD-039`; GEOM-076 remains blocked on that task.

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
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/experiment_custody.py validate --root .
```

## Forbidden changes

- Treating the accepted controls as candidate-selection, general remeshing,
  acceleration, atlas-quality, parameterization, or novelty evidence.
- Hiding the Automatic over-selection, deleting failed/superseded runs, tuning
  from confirmation data, or weakening a frozen gate after observing a result.
- Changing METHOD-037 production defaults, replacing its GMM, introducing a
  generic feature/segmentation framework, or publishing an unimplemented v2,
  optimized, or GPU backend from this closure.
- Feeding these boundaries into UV cutting or changing authoritative topology.

## Maturity

- Completed at `CPUContracted` for deterministic evidence controls and the
  opt-in CPU profiling seam, below the original `ParityProven` ambition.
- `Operational` owned by `METHOD-039` for the practical feature-network patch
  method and its existing runtime/config/UI path.
- Optimized/GPU work is not owed by this task. Any later backend requires a
  separate measured profile, parity contract, and task.
