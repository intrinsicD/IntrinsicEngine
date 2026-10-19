# METHOD-077 Nyström landmark-variant comparison

Historical result summary. The tracked raw diagnostic was retired on 2026-10-19;
exact Git links preserve it. No run was repeated for retirement; the numbers
below were recomputed from the raw records before removal.

Question (operator, 2026-10-09): for Bayesian CPD at 10^5 points, which Nyström
landmark variant (points or bounding-box union; mixed, fixed or resampled;
random or Halton boxes) registers fastest by wall time while staying equivalent
(`rms_to_exact <= 1e-3`) to the exact E-step?
[C125](../../ara/logic/claims.md#c125-method-077s-landmark-variant-comparison-yields-no-ranking-because-the-exact-arm-misses-the-full-fixture)
records the disposition.

Packet: [Layer A](https://github.com/intrinsicD/IntrinsicEngine/blob/bfc95f02dd367b11631daa1248605b9ce9548fba/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-a.md),
[Layer B](https://github.com/intrinsicD/IntrinsicEngine/blob/bfc95f02dd367b11631daa1248605b9ce9548fba/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-b.md),
[manifest](https://github.com/intrinsicD/IntrinsicEngine/blob/bfc95f02dd367b11631daa1248605b9ce9548fba/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/manifest.txt)
(digest `7bb6c4250b736f4f1f4e1e707546a13fbe856fe2b1815db434da8dcb22bc4e3f`).
Full-tier pre-flight: Claude and Codex both PASS on that digest after three
delta rounds ([reviews](https://github.com/intrinsicD/IntrinsicEngine/tree/bfc95f02dd367b11631daa1248605b9ce9548fba/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/reviews)).
Source commit `74a198c0e`, one runner (sha256 `0376cb91…d995`), pinned,
2026-10-09 21:56 to 2026-10-10 00:48. Command (the maintained driver and its
analysis test remain in the repository):

```bash
python3 tools/benchmark/cpd_landmark_variants.py \
  --runner build/ci-release/bin/IntrinsicCoherentPointDriftLandmarkVariants \
  --out <run-dir>          # then the same command with --analyze
python3 tests/regression/tooling/Test.CpdLandmarkVariantsAnalysis.py
```

## Dispositions

| Item | Result |
| --- | --- |
| Cells | 138/138 completed, exit 0; 133 at the 150-iteration cap, 5 negative-control cells converged after 4 iterations. |
| Pairs | 6 (fixture `full`/`partial` × seeds 1–3). The exact gate (`rms_to_truth <= 0.02` on `full`) fails on all three `full` pairs; 3 of 6 decided, so the frozen rule gives **no ranking**. |
| Other gates | Determinism, baseline engagement/equivalence, floor, negative and perturbation controls pass in all 6 pairs; timing noise 0.37–0.73 %. |
| Predictions | P1, P3 not evaluable (no ranking); P2, P4, P5, P6, P7 hold. |
| Exact arm | `full`: `rms_to_truth` 0.156–0.158, scale 1.0114–1.0116 (truth 1.1). `partial` (no truth gate by design): 0.797–0.803, scale 1.013–1.014. |
| Nyström use | Points variants accept 1–6 of 150 iterations, box variants 0–1; the rest of every run is the exact fallback. |
| Equivalence | Largest `rms_to_exact`: point variants 2.9e-6, box variants 4.2e-4 (`partial`); negative control 0.08–0.92. |

Descriptive only, not a decision: single-pass wall times on `full` were
84.3–95.6 s for point variants, 102.0–102.7 s for box variants and
106.5–107.0 s for `exact`; on `partial` 54.4–56.2 s, 56.1–58.5 s and
59.0–59.4 s. Box variants on one seed give identical results for all schedules
because at most one Nyström iteration ran.

## Limits and lesson

The fixture and configuration are not registered by the exact method, and the
approximation runs in only a few early wide-kernel iterations, so the run does
not answer which variant is fastest beyond "box landmarks are accepted less
often than point landmarks". The remaining plan was void; no follow-up run was
started. Stated limits of the packet (one size, landmark start count, error
limit, deformation, surface and host; landmark counts per iteration not
recorded) remain. Follow-up diagnosis of the rejections continued in
METHOD-078 to METHOD-083.

Safeguard: before freezing a comparison of approximation variants, a
calibration check must show that the reference arm solves the fixture against
ground truth and that the approximation is active in a meaningful share of the
work. The METHOD-077 technical test deliberately did not read accuracy, which
was insufficient. See [registration lessons](../lessons/registration-and-atlas.md).

Raw records: [`results.jsonl`](https://github.com/intrinsicD/IntrinsicEngine/blob/bfc95f02dd367b11631daa1248605b9ce9548fba/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/runs/results.jsonl),
[`analysis.json`](https://github.com/intrinsicD/IntrinsicEngine/blob/bfc95f02dd367b11631daa1248605b9ce9548fba/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/runs/analysis.json),
[referee audit](https://github.com/intrinsicD/IntrinsicEngine/blob/bfc95f02dd367b11631daa1248605b9ce9548fba/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/audit.md).
The six exact-reference position files (`runs/references/*.bin`) were never
committed; their sha256 values are in the exact records and they were deleted
locally at retirement.
