---
id: METHOD-050
theme: I
depends_on: [METHOD-015]
maturity_target: CPUContracted
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Finished interactively; correctness evidence is Test.CoherentPointDriftBayesian.cpp, the independent NumPy cross-check recorded in paper.md, and ARA claim C114.
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, method.engine-integration]
---
# METHOD-050 — Bayesian Coherent Point Drift reference backend

## Completion — 2026-09-28
Commit: `d6b6ab494`; the enclosing `claude/cpd` commit records retirement. Maturity
reached: `CPUContracted` (reference plus the METHOD-049 policies, which apply unchanged).

Landed: `Variant::Bayesian` on the shared solver (E-step log-weights, Cholesky
deformation posterior or low rank, digamma mixing weights, similarity, sigma^2), `Gamma`,
`Kappa`, `SubsampleSource`/`SubsampleTarget` with kernel-expansion upsampling, the
optional `PosteriorVarianceTerms`; config (`bayesian`, gamma, kappa, subsample), panel,
agent and reference smoke coverage. Updates match an independent NumPy implementation of
Algorithm 1 to 1e-9 per iteration.

## Deviations
- The paper's posterior-variance terms are off by default (as in Hirose's reference
  code): they collapse the scale on smooth kernels, reproduced in both implementations
  (C114). The full update stays available.
- The similarity/deformation split is not identifiable under weak priors; recovery of a
  known similarity is tested under a strong prior, the deformation through positions.
- Upsampling error is documented per sample density (400 of 800: RMS <= 0.005); sparse
  samples of a volumetric cloud keep the shape, not correspondences, and need omega > 0.
- No Anderson acceleration on the transforms (METHOD-052).


## Goal
- Add Bayesian CPD (BCPD) as a CPU reference variant next to the METHOD-015
  family: a joint nonrigid-plus-similarity registration with a variational
  Bayesian formulation, the strongest nonrigid method in Framework24.

## Non-goals
- No new EM framework; BCPD is a variant on the METHOD-015 core and trace.
- No acceleration of its own; it uses METHOD-049 policies.
- No GPU backend.

## Context
- Paper: Hirose, *A Bayesian Formulation of Coherent Point Drift*, IEEE TPAMI 2021,
  and its reference implementation's documented parameters (λ, β, ω, γ, κ).
- METHOD-015 explicitly excludes BCPD and asks for a separate task; this is it.
- Framework24 `bcg_coherent_point_drift_bayesian.h` (Anderson on both transforms,
  Nyström/FGT E-step, upsampling of the deformation to the full source) serves as
  a feature checklist only; implement from the paper.
- Acceleration (Nyström, KD-tree) comes through METHOD-049's E-step policies.
- Upsampling: estimate on a subsample, then interpolate the displacement to all
  source points (Hirose §5); required for scan-size inputs.

## Required changes
- [x] `bayesian` variant in `Geometry.Registration.CoherentPointDrift` with parameters λ (deformation smoothness), β (kernel width), ω (outlier probability), γ (initial scale), κ (Dirichlet prior on mixing), and the similarity transform plus displacement result.
- [x] Deformation upsampling from a subsample to all source points (Hirose §5).
- [x] `paper.md` section with the variational update equations and the stopping rule.

## Tests
- [x] Recovery of a known similarity plus smooth deformation; outlier and density robustness fixtures; upsampling interpolation error bound.
- [x] Degenerate inputs and divergence return explicit failure states; bitwise determinism.

## Docs
- [x] Method README: BCPD versus rigid/affine/nonrigid CPD, parameter guidance and limitations.

## Acceptance criteria
- [x] Shared E-step and trace/observer with METHOD-015; BCPD is a variant, not a parallel framework.
- [x] Recovers a known similarity plus smooth deformation within documented tolerances; handles outliers via ω and non-uniform point density.
- [x] Deformation upsampling from a subsample to the full source with a documented interpolation error on fixtures.
- [x] Fail-closed states for degenerate inputs and divergence; deterministic results.
- [x] Benchmark smoke entry next to the METHOD-015 variants; method docs with parameter guidance and limitations.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Two finite point spans plus BCPD parameters. |
| Compatible entity sources | Every canonical domain via RUNTIME-273. |
| RuntimeModule | RUNTIME-273 adds the variant token. |
| Config/agent | `sandbox.coherent_point_drift` variant `bayesian` with its parameters. |
| UI | This task adds the bayesian variant and upsampling controls to the CPD panel (UI-055, retired). |
| Publication | Similarity transform plus displacement, published as in RUNTIME-273. |
| End-to-end tests | Extend the RUNTIME-273 contract and UI-055 panel tests to the variant. |

## Forbidden changes
- No copied Framework24 or reference-implementation code without compatible-license provenance.
- No change to the rigid/affine/nonrigid reference results.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
```
