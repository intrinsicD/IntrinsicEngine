---
id: METHOD-028
theme: I
depends_on: [REVIEW-003, METHOD-004]
---
# METHOD-028 — Confidence-driven spatial guiding for Walk on Stars

## Goal

- Add and evaluate a deterministic CPU-reference spatial guiding policy for
  Walk on Stars that refines guide cells only when uncertainty-aware local
  contribution/direction signatures justify their memory cost.

## Non-goals

- No replacement of the unguided `METHOD-004` CPU reference or change to its
  default policy.
- No neural field, learned proposal, GPU backend, renderer path guiding, or
  global adaptive-partition framework.
- No performance or variance-reduction claim without equal-sample and
  equal-memory baseline comparisons.
- No runtime, UI, ECS, graphics, asset, or platform integration.

## Context

- Owning subsystem/layer: the `METHOD-004` geometry method family and a method
  package under `methods/geometry/guided_walk_on_stars/`; engine code remains
  `geometry -> core`.
- Promotion is gated by `REVIEW-003`; `METHOD-004` must first deliver the
  deterministic unguided CPU oracle and analytic PDE fixtures.
- The split criterion is transferred from Illumination-Aware Spatial
  Subdivision for Path Guiding, which compares compact radiance/direction
  signatures with uncertainty before splitting:
  [project page](https://spatial-subdiv.ewi.tudelft.nl/).
- Direct prior art already applies path guiding to Walk on Stars:
  [Guiding-Based Importance Sampling for Walk on Stars](https://arxiv.org/abs/2410.18944).
  The research question here is narrower: whether confidence-driven spatial
  resolution improves the error/variance-per-byte frontier over unguided and
  count-threshold guides. Novelty is not presumed.
- Reuse `METHOD-004` seeded-RNG and estimator diagnostics. Analytic PDE
  solutions remain numerical truth; the unguided `METHOD-004` CPU estimator is
  the canonical implementation baseline for seeded behavior and diagnostics.

## Slice plan

- **Slice A — replayable guide statistics.** Capture deterministic per-step
  samples from the CPU reference and compute file-local cell signatures without
  changing the solver's sampling distribution.
- **Slice B — three-policy comparison.** Add unguided, sample-count-threshold,
  and confidence-threshold policies behind a plain enum/config record and run
  equal-sample killing tests, with identical memory caps for the two guided
  policies and unguided retained as the zero-guide baseline.
- **Slice C — guided estimator.** Only if Slice B shows useful partitioning,
  use the confidence guide as an importance proposal with explicit weighting
  and bias tests.
- **Slice D — benchmark evidence.** Add a stable smoke case plus a bounded
  deeper comparison; do not open GPU or neural work.

## Right-sizing

- The three present policies justify one plain policy enum and data record, not
  a guide interface, registry, service, or pluggable partition framework.
- Keep the partition implementation private to the method until a second real
  algorithm needs exactly the same statistics and split contract.

## Backends

- Backend axis: deterministic `cpu_reference` only; neural and GPU variants
  are explicitly excluded.

## Required changes

- [ ] Create `methods/geometry/guided_walk_on_stars/` with a paper intake,
      method manifest, assumptions, proposal-density formulation, weighting,
      and fail-closed diagnostics.
- [ ] Extend the CPU-reference path with deterministic, replayable step records
      keyed by `(seed, point, walk, step)` without changing unguided output.
- [ ] Define compact per-cell signatures for contribution magnitude and
      contribution-weighted direction, their uncertainty estimates, minimum
      sample count, maximum depth/cell count, and deterministic split order.
- [ ] Implement exactly three comparison policies: unguided, count-threshold
      spatial subdivision, and confidence-threshold spatial subdivision.
- [ ] Freeze exactly three analytic PDE fixtures and 16 evaluation points per
      fixture before execution. For every policy/point use the same 32
      independent seeds and 4,096 walks per seed. Cap each guided policy at
      1 MiB, 16,384 cells, depth 12, and a 64-sample minimum before splitting;
      the unguided policy remains the zero-guide baseline.
- [ ] Establish equivalence to the analytic solution with two one-sided tests
      at Holm-corrected family-wise `alpha = 0.05`, using an equivalence margin
      of `max(1e-6, 0.01 * solution_scale)` where `solution_scale` is frozen per
      fixture. The preregistered 32-seed design must have power `>= 0.80` to
      detect a 15% paired variance reduction; otherwise the comparison is
      underpowered and cannot pass.
- [ ] Apply the fixed killing rule: all policies must pass analytic-solution
      equivalence on all three fixtures. On at least two fixtures confidence
      guiding must have a paired variance-ratio point estimate `<= 0.85` and a
      one-sided 95% upper confidence bound `<= 0.95` against both unguided and
      count-threshold policies. On the third fixture its upper bound must be
      `<= 1.10` against both baselines. Count/confidence comparisons use the
      identical 1 MiB cap; all three comparisons use identical walk budgets.
- [ ] If the partition test fails, record the negative result and retire without
      wiring the guide into estimator sampling.
- [ ] Add stable benchmark IDs
      `geometry.walk_on_stars.confidence_guiding.smoke` and
      `geometry.walk_on_stars.confidence_guiding.comparison`. Use only allowed
      manifest metrics; record variance, interval width, walk length, guide
      bytes/cells, split counts, estimator bias, and sample counts as result
      diagnostics.

## Tests

- [ ] Verify replay records and partition shape are identical across repeated
      runs and supported thread counts for a fixed seed.
- [ ] Verify no split occurs below the minimum evidence threshold and all
      depth/cell/sample caps fail closed without partial guide corruption.
- [ ] On analytic Laplace/Poisson and mixed-boundary WoSt fixtures, verify each
      estimator passes the frozen Holm-corrected equivalence test and report
      confidence bounds, effect sizes, preregistered power calculation, and
      seed count.
- [ ] Compare all three policies at equal walk count and compare the two guided
      policies at the identical guide-memory cap using the fixed
      32-seed/4,096-walk/1-MiB design; assert the
      0.85/0.95 improvement and 1.10 non-regression limits rather than runtime
      alone.
- [ ] Cover empty point batches, zero samples, constant contributions,
      zero-length directions, non-finite samples, singular variance, and
      budget exhaustion.
- [ ] Run and schema-validate the smoke and comparison benchmark results.

## Docs

- [ ] Document the guiding formulation, unbiased weighting, deterministic
      split/tie policy, killing-test outcome, and known failure modes in the
      method package.
- [ ] Update the `METHOD-004` method documentation with this opt-in follow-up
      only after it exists; keep unguided WoSt documented as canonical truth.
- [ ] Update `benchmarks/geometry/README.md` with the stable IDs and baseline
      comparison policy; regenerate module inventory only if a module surface
      changes.

## Acceptance criteria

- [ ] The unguided reference remains bitwise unchanged for existing inputs.
- [ ] The three policies are compared at equal sample budgets, and the two
      guided policies at equal memory budgets, on exactly three declared
      analytic fixtures using 16 points, 32 seeds, 4,096 walks per seed, and
      the declared guide caps.
- [ ] Every policy passes the analytic equivalence test at family-wise
      `alpha = 0.05`, the design has power `>= 0.80`, and confidence guiding
      meets the 0.85 point-estimate/0.95 upper-bound threshold on at least two
      fixtures without exceeding the 1.10 upper bound on the third; otherwise
      it retires with reproducible negative evidence and no implementation
      follow-up.
- [ ] The implementation introduces no neural/GPU dependency and no generic
      guiding/partition framework.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'WalkOnStars|WalkOnSpheres|ConfidenceGuiding|BenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
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

- Changing the `METHOD-004` default or using guided output as the correctness
  oracle.
- Adding a neural model, ML dependency, GPU/Vulkan backend, or renderer path
  guiding code.
- Hiding estimator bias behind variance-only reporting.
- Creating a generic spatial-guiding interface, registry, service, or runtime
  control surface for this one consumer.
- Mixing mechanical file moves with semantic changes.

## Maturity

- Target: `CPUContracted` with comparative evidence against the unguided CPU
  oracle.
- The CPU method endpoint is intentional; no `Operational` follow-up is owed.
