---
id: METHOD-015
theme: I
depends_on: [GEOM-058]
maturity_target: CPUContracted
---
# METHOD-015 — Coherent Point Drift registration family reference backend

## Goal
- Add a CPU reference method package for the Coherent Point Drift point-set registration family — rigid, affine, and nonrigid variants sharing one EM core — as the probabilistic complement to the existing deterministic `Geometry.Registration::AlignICP` pipeline.

## Non-goals
- No Bayesian CPD in this task; it requires a separately allocated follow-up
  method task if prioritized.
- No fast Gauss transform, permutohedral, or low-rank acceleration — the reference is an explicit O(N·M) EM; optimized backends open only after reference parity (`GEOM-060` is the named lattice seam).
- No editor/UI integration (a follow-up UI task can reuse the retired `UI-029` ICP-editor pattern).
- No claim that CPD supersedes ICP; both stay public with documented trade-offs.

## Context
- Paper/method: Myronenko & Song — "Point Set Registration: Coherent Point Drift", IEEE TPAMI 2010.
- Method package: `methods/geometry/coherent_point_drift/`.
- Port source: framework24
  `lib_bcg_framework/include/bcg_coherent_point_drift.h`,
  `bcg_coherent_point_drift_rigid.h`,
  `bcg_coherent_point_drift_affine.h`, and
  `bcg_coherent_point_drift_nonrigid.h`. The Bayesian header is excluded and
  requires a separate method intake. The bcg originals are untested and
  duplicate EM plumbing per variant; this package shares one E-step and
  per-variant M-steps. Treat these headers as out-of-build comparison material:
  record repository/revision/license provenance during intake and implement
  from the paper; copy no code unless its license is explicitly compatible.
- Numerics gate: `GEOM-058` (`Geometry.GaussianMixture` responsibilities/EM machinery, Anderson-accelerated EM as an opt-in policy).
- Fits the registration-pipeline modularity roadmap (`docs/architecture/geometry-pipeline-modularity.md`, retired `GEOM-054`/`GEOM-055`): CPD reports per-iteration traces through the same observer idiom `AlignICP` uses.

## Variants and default selection

- **Default — rigid CPD** (Myronenko & Song 2010, §IV).
- **Same-package alternatives — affine and nonrigid CPD** (§V/§VI). They
  share the E-step and differ in typed M-step state; neither duplicates EM
  control flow.
- **Deferred — Bayesian CPD.** It changes the method formulation and requires
  a separately allocated follow-up task; no dormant token is added here.

## Slice plan
- **Slice A — intake/shared contract.** Freeze units, transform conventions,
  likelihood/responsibility equations, stopping/floor rules, variant payloads,
  fixtures, tolerances, and failure states before implementation.
- **Slice B — rigid oracle.** Land the shared E-step plus rigid M-step and
  analytic transform recovery first.
- **Slice C — affine/nonrigid alternatives.** Add one M-step at a time with
  independent correctness/degeneracy tests; keep the shared EM trace unchanged.
- **Slice D — benchmark/docs.** Add the bounded executable smoke and backend
  limitations after all shipped variants pass.

## Right-sizing
- One shared EM loop with three typed M-step branches is justified. Do not add
  a CPD backend registry, per-variant service, or generic optimizer framework.
- Keep the explicit O(N*M) reference. Acceleration structures and Bayesian CPD
  require later evidence/tasks.

## Backends
- Backend axis: `cpu_reference` only, with direct EM as the canonical policy.
  GEOM-058's already-tested Anderson policy may be an explicit convergence
  option, but does not create an optimized backend identity. Fast-summation,
  low-rank, or GPU implementations require later parity-gated tasks.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/coherent_point_drift/`.
- [ ] Fill `method.yaml` (`id: geometry.coherent_point_drift`; metrics: `rmse_to_ground_truth`, `sigma2_trajectory`, `iterations`, `negative_log_likelihood`, `runtime_ms`).
- [ ] Fill `paper.md` with objective/equations, correspondence/transform units,
      outlier model, sigma² floor and stopping rules, input/output contract,
      variant assumptions, source/revision/license provenance, and explicit
      failure diagnostics.

### Public API in `src/geometry`
- [ ] Add module `Geometry.Registration.CoherentPointDrift` (`.cppm` + `.cpp`): `Params` (variant token, outlier weight `w` in [0,1), max iterations, sigma² floor, convergence tolerance, optional seed, acceleration policy passthrough) and per-variant `Result` (transform or displacement field, final sigma², iterations, converged flag, explicit failure state).
- [ ] E-step responsibilities delegate to the `GEOM-058` surface; no duplicated Gaussian plumbing.
- [ ] Per-iteration observer emitting transform/sigma²/log-likelihood traces, mirroring the retired `GEOM-055` `IterationObserver` idiom; observed and unobserved runs stay numerically identical.
- [ ] Fail-closed on empty inputs, `w` outside [0,1), and non-finite points; the sigma² floor prevents collapse instead of NaN.
- [ ] Register the module in `src/geometry/CMakeLists.txt`.

### Benchmarks
- [ ] Add executable manifest
      `benchmarks/geometry/manifests/coherent_point_drift_reference_smoke.yaml`
      with stable ID `geometry.coherent_point_drift.reference.smoke`, a
      built-in deterministic transform/deformation dataset,
      `intent: correctness`, fixed seed, explicit warmup/measured counts, and
      allowed metrics `runtime_ms` and `quality_error_l2`.
- [ ] Emit schema-valid `cpu_reference` result JSON. Put transform/landmark
      error, likelihood/sigma² trace, iterations, overlap/outlier regime, and
      variant identity in diagnostics; no external dataset or speed claim.

## Tests
- [ ] `tests/unit/geometry/Test.CoherentPointDrift.cpp` with `unit;geometry` labels.
- [ ] Rigid: recover a known SE(3) (plus scale) on noisy, partially overlapping, subsampled fixtures within documented tolerance; an outlier fixture with `w > 0` stays within tolerance where `w = 0` demonstrably fails.
- [ ] Affine: recover a known affine map within tolerance.
- [ ] Nonrigid: on a synthetic smooth deformation, mean landmark error stays under a documented bound and the coherence regularization keeps the displacement field smooth (bounded finite-difference proxy).
- [ ] Determinism: identical inputs/params produce bitwise-identical results across runs and thread counts.
- [ ] Degenerate/fail-closed cases return explicit failure states.
- [ ] Freeze per-variant analytic/regression tolerances and input scale
      normalization before assertions; cover singular covariance/kernel
      systems, zero overlap, sigma²-floor termination, and iteration cap.

## Docs
- [ ] `methods/geometry/coherent_point_drift/README.md` including ICP-versus-CPD guidance and complexity/memory notes.
- [ ] Document numerical limitations (sigma² floor behavior, small-overlap failure modes, sensitivity to `w`).
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Rigid CPD is the default; rigid, affine, and nonrigid CPD are implemented
      against one shared EM core.
- [ ] All correctness tests pass in the default CPU gate.
- [ ] Benchmark smoke manifest validates and runs.
- [ ] Smoke result validates and reports quality/error plus convergence
      diagnostics for every shipped variant.
- [ ] Public API exposes only `std`/`glm`/scalar types.
- [ ] The `GEOM-058` surface backs the E-step (no private Gaussian re-implementation).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift|GaussianMixture|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No optimized CPU or GPU backend before reference parity.
- No performance claims without baseline comparison.
- No live ECS/runtime/graphics knowledge in the method package.
- No production dependency on framework24 and no copied implementation without
  recorded compatible-license provenance.
- No `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted`. The CPU reference is the correctness oracle for
  later optimized backends (fast Gauss transform via the `GEOM-060`
  permutohedral seam, low-rank nonrigid) and for any separately scoped
  Bayesian method; those open as distinct tasks per `AGENTS.md` §6.
