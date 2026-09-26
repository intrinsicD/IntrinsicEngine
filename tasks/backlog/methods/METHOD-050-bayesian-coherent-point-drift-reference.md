---
id: METHOD-050
theme: I
depends_on: [METHOD-015, GEOM-059]
maturity_target: CPUContracted
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, method.engine-integration]
---
# METHOD-050 — Bayesian Coherent Point Drift reference backend

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
- [ ] `bayesian` variant in `Geometry.Registration.CoherentPointDrift` with parameters λ (deformation smoothness), β (kernel width), ω (outlier probability), γ (initial scale), κ (Dirichlet prior on mixing), and the similarity transform plus displacement result.
- [ ] Deformation upsampling from a subsample to all source points (Hirose §5).
- [ ] `paper.md` section with the variational update equations and the stopping rule.

## Tests
- [ ] Recovery of a known similarity plus smooth deformation; outlier and density robustness fixtures; upsampling interpolation error bound.
- [ ] Degenerate inputs and divergence return explicit failure states; bitwise determinism.

## Docs
- [ ] Method README: BCPD versus rigid/affine/nonrigid CPD, parameter guidance and limitations.

## Acceptance criteria
- [ ] Shared E-step and trace/observer with METHOD-015; BCPD is a variant, not a parallel framework.
- [ ] Recovers a known similarity plus smooth deformation within documented tolerances; handles outliers via ω and non-uniform point density.
- [ ] Deformation upsampling from a subsample to the full source with a documented interpolation error on fixtures.
- [ ] Fail-closed states for degenerate inputs and divergence; deterministic results.
- [ ] Benchmark smoke entry next to the METHOD-015 variants; method docs with parameter guidance and limitations.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Two finite point spans plus BCPD parameters. |
| Compatible entity sources | Every canonical domain via RUNTIME-273. |
| RuntimeModule | RUNTIME-273 adds the variant token. |
| Config/agent | `sandbox.coherent_point_drift` variant `bayesian` with its parameters. |
| UI | UI-055 variant with upsampling controls. |
| Publication | Similarity transform plus displacement, published as in RUNTIME-273. |
| End-to-end tests | Variant covered in RUNTIME-273/UI-055 tests. |

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
