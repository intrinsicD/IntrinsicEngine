---
id: METHOD-049
theme: I
depends_on: [METHOD-015]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Finished interactively; the performance and parity result is ARA claim C113 with sealed, source-bound evidence in ara/evidence/diagnostics/method049_cpd_accelerated_20260928/.
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, method.engine-integration]
---
# METHOD-049 — Accelerated Coherent Point Drift E-step and low-rank nonrigid solve

## Completion — 2026-09-28
Commit: `cc5344978` (backends), `d6b6ab494` (tree-ordered truncation, weighted E-step);
review fixes `54927242f`, whose sealed rerun (medians of repeated runs) is the evidence.
Maturity reached: `ParityProven` for the CPU backends on the recorded host and fixture
(claim C113), scoped: reference parity is proven for rigid policies up to 10^4 points and
for the low-rank solve at 10^3; beyond that it is chained (auto against dense at 10^5, low
rank 150 against 50 at 10^4 and 10^5).

Landed: `Geometry.Registration.CoherentPointDrift.EStep` with `Dense` (blocked single pass
or two-pass, thread-count independent, AVX2/baseline row kernels), `Truncated`
(per-row relative bound, tree-ordered), `FastGauss` (IFGT, Raykar bound with exact
fixups), `Auto`; `Params::LowRank` Nystroem eigenpairs with a Woodbury M-step; backend,
per-iteration policy, bound and kernel error in results and traces; config, panel and
agent fields; smoke and scaling benchmarks.

## Deviations
- The `nystrom` E-step policy is not offered: its error has no a-priori bound, which the
  forbidden-change rule on unbounded truncation excludes. Nystroem is used for the
  nonrigid kernel instead.
- GEOM-059 (kernel matrices) and GEOM-060 (permutohedral lattice) were not available; the
  Nystroem kernel lives in the EStep module, and the lattice stays the candidate for wide
  kernels, where the fast Gauss transform measured slower than dense.
- The low-rank solve uses a Nystroem eigen-approximation, not GEOM-024; its error is
  sampled and reported, not bounded.
- Anderson acceleration (deferred here by METHOD-015 and UI-055) moves to METHOD-052.
- Benchmark sizes 10^3/10^4/10^5 as required; the reference runs to 10^4 only, and the
  10^5 comparison is auto against dense. The first bound run failed its gate (low-rank
  iteration cap, slow truncation); both were fixed and the rerun passed (record.json).
- Independent review (Fable 5.1, Codex) found a weighted-row overflow, an out-of-bounds read
  in subsampled BCPD, understated fast-Gauss bounds, backend misreporting and single-run
  timings with the observer in the timed region; all fixed in `54927242f` and re-measured.
- A requested low rank k is an upper bound: numerically null eigenpairs are dropped
  (k = 150 gives an effective rank of 73-74 on the scaling fixture).


## Goal
- Make CPD usable at scan sizes (10^4–10^6 points per side) with an optimized
  CPU backend whose results stay within declared tolerances of the METHOD-015
  explicit O(N·M) reference, and prove the speedup with a baseline comparison.

## Non-goals
- No GPU backend until GEOM-103 decides whether GPU backends pay off here.
- No change to the METHOD-015 reference semantics or default.
- No Bayesian formulation (METHOD-050).

## Context
- Operator request 2026-09-27: CPD must be reimplemented properly, including the
  fast method and a proper UI (RUNTIME-273, UI-055).
- The E-step needs, per iteration, `P1 = P 1`, `Pᵀ1` and `P X` for the Gaussian
  responsibility matrix `P` (M×N). The reference computes it densely.
- Literature to implement from (not from Framework24 code):
  - Myronenko & Song, *Point Set Registration: Coherent Point Drift*, TPAMI 2010,
    §VI-C/§VII: fast Gauss transform (FGT) for the E-step sums and a low-rank
    eigen-approximation of the nonrigid Gram matrix `G`.
  - Greengard & Strain, *The Fast Gauss Transform*, SIAM J. Sci. Stat. Comput. 1991;
    Yang, Duraiswami, Gumerov & Davis, *Improved Fast Gauss Transform*, ICCV 2003
    (IFGT; better scaling in 3-D).
  - Williams & Seeger, *Using the Nyström Method to Speed Up Kernel Machines*,
    NIPS 2001; Hirose, *A Bayesian Formulation of Coherent Point Drift*, TPAMI 2021
    (Nyström plus KD-tree E-step acceleration).
- Framework24 `bcg_softmatching.h` offers Full, FGT, Parallel, Nyström, Nyström+FGT
  and KD-tree E-steps with seven Nyström sampling strategies; its Nyström
  initialization is marked non-working. Use it only as a feature checklist.
- Reuse: `GEOM-059` (kernel matrices / Nyström), `GEOM-060` (permutohedral lattice
  as an alternative Gaussian filter), `Geometry.PointLBVH` for truncated
  neighborhoods (bounded-tail only; see the spatial-acceleration note in METHOD-015).
- GPU backends are out of scope until GEOM-103 settles whether GPU backends pay off.

## Variants and default selection
- Default stays the METHOD-015 reference until this task proves parity and speed.
- Accelerated E-step policies: `dense` (reference), `fgt`/`ifgt` with an error
  tolerance, `truncated` (spatial cutoff with a proven tail bound on dropped mass),
  `nystrom` (rank r). Nonrigid M-step: `full` or `low_rank` (k eigenpairs of `G`).
- Each policy reports its backend identity and an a-posteriori error estimate.

## Required changes
- [x] E-step policy parameter on `Geometry.Registration.CoherentPointDrift` with `dense` (reference), `fgt`/`ifgt` (error tolerance), `truncated` (radius with reported dropped mass) and `nystrom` (rank r) implementations sharing the reference's M-steps.
- [x] Low-rank nonrigid M-step: k leading eigenpairs of the Gram matrix `G` (reuse GEOM-024 once available, else a dense eigen solve on a bounded subset with documented limits).
- [x] Per-policy backend identity and a-posteriori error estimate in the result and trace.
- [x] Benchmark manifest `geometry.coherent_point_drift.accelerated` with reference comparison.

## Tests
- [x] Parity per policy against the reference on all METHOD-015 fixtures within frozen tolerances.
- [x] Error-bound tests: the reported FGT/IFGT truncation error and truncated-policy dropped mass bound the actual E-step difference on random fixtures.
- [x] Low-rank nonrigid converges to the full solution as k grows; determinism across thread counts.

## Docs
- [x] Method README section per policy: complexity, memory, when to use, failure modes (small sigma² for FGT, rank selection for low-rank `G`).

## Acceptance criteria
- [x] Optimized CPU backend(s) with explicit backend identity; the reference stays canonical.
- [x] Parity: on the METHOD-015 fixtures every policy reaches the reference transform/displacement within declared tolerances (frozen before tuning), and the E-step sums within the policy's stated error bound.
- [x] Error bounds: FGT/IFGT truncation error and the truncated policy's dropped Gaussian mass are computed and reported, not assumed.
- [x] Benchmark: manifest-backed comparison against the reference at 10^3, 10^4 and 10^5 points (Release build), reporting runtime, iterations and parity delta; a speedup claim only with ARA evidence.
- [x] Deterministic results across runs and thread counts.
- [x] Method docs describe when each policy is appropriate and its failure modes (small sigma² for FGT, rank for low-rank `G`).

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged from METHOD-015: two finite point spans plus variant/EM parameters and the acceleration policy. |
| Compatible entity sources | Every canonical domain via RUNTIME-273. |
| RuntimeModule | RUNTIME-273 selects the policy; no service or registry here. |
| Config/agent | RUNTIME-273 exposes the policy and its tolerance in `sandbox.coherent_point_drift`. |
| UI | This task adds the policy, tolerance, low-rank k and Anderson controls to the CPD panel (UI-055, retired) with requested/actual backend feedback. |
| Publication | Unchanged; RUNTIME-273. |
| End-to-end tests | Parity tests here; extend the RUNTIME-273 contract and UI-055 panel tests to an accelerated policy. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No silent truncation of Gaussian responsibilities without a reported bound.
- No performance claim without a baseline comparison and ARA evidence.
- No copied Framework24 code without compatible-license provenance.
