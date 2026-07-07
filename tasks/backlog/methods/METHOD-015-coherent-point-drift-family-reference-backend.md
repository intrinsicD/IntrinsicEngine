---
id: METHOD-015
theme: none
depends_on: [GEOM-058]
maturity_target: CPUContracted
---
# METHOD-015 — Coherent Point Drift registration family reference backend

## Goal
- Add a CPU reference method package for the Coherent Point Drift point-set registration family — rigid, affine, and nonrigid variants sharing one EM core — as the probabilistic complement to the existing deterministic `Geometry.Registration::AlignICP` pipeline.

## Non-goals
- No Bayesian CPD in this task (variant D below opens as a follow-up method task when prioritized).
- No fast Gauss transform, permutohedral, or low-rank acceleration — the reference is an explicit O(N·M) EM; optimized backends open only after reference parity (`GEOM-060` is the named lattice seam).
- No editor/UI integration (a follow-up UI task can reuse the retired `UI-029` ICP-editor pattern).
- No claim that CPD supersedes ICP; both stay public with documented trade-offs.

## Context
- Paper/method: Myronenko & Song — "Point Set Registration: Coherent Point Drift", IEEE TPAMI 2010.
- Method package: `methods/geometry/coherent_point_drift/`.
- Port source: framework24 `lib_bcg_framework/include/bcg_coherent_point_drift.h`, `bcg_coherent_point_drift_rigid.h`, `bcg_coherent_point_drift_affine.h`, `bcg_coherent_point_drift_nonrigid.h` (`bcg_coherent_point_drift_bayesian.h` maps to follow-up variant D). The bcg originals are untested and duplicate EM plumbing per variant; this package shares one E-step and per-variant M-steps.
- Numerics gate: `GEOM-058` (`Geometry.GaussianMixture` responsibilities/EM machinery, Anderson-accelerated EM as an opt-in policy).
- Fits the registration-pipeline modularity roadmap (`docs/architecture/geometry-pipeline-modularity.md`, retired `GEOM-054`/`GEOM-055`): CPD reports per-iteration traces through the same observer idiom `AlignICP` uses.

## Variants and default selection

Mark `[x]` next to the variant that should be the public-facing default backend.

- [x] **A — Rigid CPD (Myronenko & Song 2010, §IV).** Closed-form SE(3)-plus-scale M-step; the most common research use and directly comparable to `AlignICP`. **Selected as the default.**
- [ ] **B — Affine CPD (§V).** Shares the EM core; ships in this package as an opt-in variant token.
- [ ] **C — Nonrigid CPD (§VI, coherence-regularized Gaussian-kernel displacement).** Ships in this package as an opt-in variant token; explicit O(N·M + M²) reference with documented memory bounds.
- [ ] **D — Bayesian CPD (Hirose, IEEE TPAMI 2021).** Stronger convergence/robustness story; follow-up method task, not part of this package.

Default recommendation: **A**, with B and C as same-package variant tokens sharing the EM core (they differ only in the M-step).

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/coherent_point_drift/`.
- [ ] Fill `method.yaml` (`id: geometry.coherent_point_drift`; metrics: `rmse_to_ground_truth`, `sigma2_trajectory`, `iterations`, `negative_log_likelihood`, `runtime_ms`).
- [ ] Fill `paper.md` with the claim capture (expected accuracy versus noise/outlier/overlap regimes from the paper).

### Public API in `src/geometry`
- [ ] Add module `Geometry.Registration.CoherentPointDrift` (`.cppm` + `.cpp`): `Params` (variant token, outlier weight `w` in [0,1), max iterations, sigma² floor, convergence tolerance, optional seed, acceleration policy passthrough) and per-variant `Result` (transform or displacement field, final sigma², iterations, converged flag, explicit failure state).
- [ ] E-step responsibilities delegate to the `GEOM-058` surface; no duplicated Gaussian plumbing.
- [ ] Per-iteration observer emitting transform/sigma²/log-likelihood traces, mirroring the retired `GEOM-055` `IterationObserver` idiom; observed and unobserved runs stay numerically identical.
- [ ] Fail-closed on empty inputs, `w` outside [0,1), and non-finite points; the sigma² floor prevents collapse instead of NaN.
- [ ] Register the module in `src/geometry/CMakeLists.txt`.

### Benchmarks
- [ ] Smoke benchmark manifest per `docs/benchmarking/` policy on deterministic synthetic fixtures (no external datasets): known-transform recovery for A/B and a synthetic smooth deformation for C.

## Tests
- [ ] `tests/unit/geometry/Test.CoherentPointDrift.cpp` with `unit;geometry` labels.
- [ ] Rigid: recover a known SE(3) (plus scale) on noisy, partially overlapping, subsampled fixtures within documented tolerance; an outlier fixture with `w > 0` stays within tolerance where `w = 0` demonstrably fails.
- [ ] Affine: recover a known affine map within tolerance.
- [ ] Nonrigid: on a synthetic smooth deformation, mean landmark error stays under a documented bound and the coherence regularization keeps the displacement field smooth (bounded finite-difference proxy).
- [ ] Determinism: identical inputs/params produce bitwise-identical results across runs and thread counts.
- [ ] Degenerate/fail-closed cases return explicit failure states.

## Docs
- [ ] `methods/geometry/coherent_point_drift/README.md` including ICP-versus-CPD guidance and complexity/memory notes.
- [ ] Document numerical limitations (sigma² floor behavior, small-overlap failure modes, sensitivity to `w`).
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Variant A marked default; A, B, and C implemented against one shared EM core.
- [ ] All correctness tests pass in the default CPU gate.
- [ ] Benchmark smoke manifest validates and runs.
- [ ] Public API exposes only `std`/`glm`/scalar types.
- [ ] The `GEOM-058` surface backs the E-step (no private Gaussian re-implementation).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift|GaussianMixture' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No optimized CPU or GPU backend before reference parity.
- No performance claims without baseline comparison.
- No live ECS/runtime/graphics knowledge in the method package.
- No `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted`. The CPU reference is the correctness oracle for later optimized backends (fast Gauss transform via the `GEOM-060` permutohedral seam, low-rank nonrigid) and for the Bayesian variant-D follow-up; those open as separate method tasks per `AGENTS.md` §6.
