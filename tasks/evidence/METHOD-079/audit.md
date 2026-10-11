# METHOD-079 — First invalid Nyström target row

The captured failures are finite negative approximated affinity sums. Both exact sums are
positive, and the negative approximation survives all six frozen arithmetic/operand checks.
This supports a persistent approximation sign error at these two rows. The points variant
also has a poorly conditioned retained spectrum; higher-precision reconstruction does not
remove its negative sign. Neither selected row meets the frozen weak-coverage flag, and no
captured target kernel entry is zero. ARA claim: **C120**.

## Bound experiment

The operator approved the [first-row question](plan.md), not a census or a production fix.
The full synthetic 100000-point fixture uses seed 1 and eight CPU threads. Prefixes are
four iterations for points/fixed/random and two for box/resampled/random, each repeated
in reversed order. The execution commit is `cae9a475690c5b5285d523403ba71689b6d21007`;
the [packet](preflight/pilot/manifest.txt) SHA256 is
`63fd1b25f9224df84c4419214aa769221f73e22b945c940606e7f0f804aca565`.
Both independent [preflight approvals](reviews/approvals.json) bind that packet. All four
cells completed within the frozen limits. Repeated snapshots and numerical summaries are
identical; this establishes determinism for these repeats, not population uncertainty.

## What the failed rows show

Sums below use the native common weight shift. Ratios and inlier mass use the same shift.
Distances use normalized internal coordinates divided by internal sigma.

| Quantity | Points / fixed | Box / resampled |
| --- | ---: | ---: |
| Selected target index; zero-based draw | 83500; 3 | 220; 1 |
| Landmarks; retained rank | 256; 243 | 512; 512 |
| Exact weighted Gaussian sum E | +0.00006416285356 | +0.3272350236 |
| Native Nyström sum S | -0.0006214047402 | -7.788496231 |
| Rebuilt long-double sum | -0.0006214045815 | -7.788496231 |
| Six-path spread / absolute native sum | 4.04e-7 | 6.73e-12 |
| Retained spectral condition | 7.32e9 | 4.00e4 |
| Smallest retained eigenvalue | 7.29e-9 | 3.33e-4 |
| Target / nearest-landmark distance in sigma | 0.6372 | 0.6254 |
| Target kernel projection residual | 0.22165 | 0.074975 |
| Top-eight exact contributors: nearest-landmark distance in sigma | 0.109–0.138 | 0.422–0.436 |
| Top-eight contributor projection residuals | 1.46e-7–2.04e-7 | 0.00678–0.00706 |
| Exact target inlier mass E/(E+c) | 1.521e-7 | 0.0005381 |
| Absolute native sum / outlier component c | 1.473e-6 | 0.01281 |

No evaluation stage changes sign. The six paths keep native operands with extended
accumulation, reapply saved eigenfactors, re-diagonalize the saved Gram matrix, and rebuild
Gaussian kernels with both native retained rank and their own cutoff. Ranks/masks agree.
The scalar native replay and native inverse-times-aggregate replay match bitwise.

Gram reconstruction differences are at most 1.68e-16 / 1.36e-16; target-row differences
4.03e-17 / 7.21e-17; relative eigen residuals 4.14e-15 / 4.16e-14. Source aggregate absolute
differences are 3.05e-11 / 2.58e-11, or 6.44e-16 / 4.46e-16 relative to their largest aggregate entries. The independent
raw audit finds the differences consistent with rounding during the large sums. The frozen accumulation scales are 2.45e-14 / 9.02e-13. They are heuristics,
not rigorous bounds on eigensolver/kernel error or proofs of exact-arithmetic signs.

Within these individual target rows, the rebuilt approximation assigns negative contributions
to 50077 / 80211 of the 100000 sources. Positive and negative masses are respectively
+0.00198229 and -0.00260370, and +1.56792 and -9.35642. The per-source and per-eigencomponent
reconstructions corroborate the signed sum. These counts are **not counts of bad target rows**.

## Interpretation and limits

- **Inverse sensitivity:** the retained points spectrum is ill-conditioned, so numerical
  sensitivity is a real concern. Its measured arithmetic spread is still much smaller than
  the negative sum. The box row also remains negative with a much better conditioned spectrum.
  These checks do not support roundoff as the dominant explanation of either captured sign.
- **Local coverage:** both targets have a landmark within sigma, and all tested target/top-eight
  projection residuals stay below the frozen 0.9 flag. The approved weak-coverage diagnostic
  therefore does not flag these points. It does not prove that the global landmark subspace
  resolves tiny cross-affinities accurately. The nearest landmark alone is no relative-error
  guarantee for an affinity between two different points.
- **Underflow / hard zero:** target-kernel zero counts and reconstructed source exponents below
  the native -708 cutoff are zero. That mechanism does not explain these captured failures.
- **Sign and significance:** a Gaussian affinity is positive, but this low-rank reconstruction
  produces signed source contributions and a negative sum. Both lowest-index failed targets
  happen to be injected clutter and have low exact inlier mass. Their small relevance does
  not establish that all other rows or later E-step statistics are accurate.

No relaxation of the positivity check, clamping rule, row-repair algorithm, larger landmark
budget, or speed claim follows from this experiment. A future repair must preserve all E-step
statistics and still pass the existing accuracy controls. That is a separate operator decision.
The two variant configurations differ; their comparison is not a controlled causal test of
conditioning or landmark placement. Upstream errors in double-precision coordinates, weights
and sigma are outside this replay, and long double is not exact arithmetic.

## Evidence and cleanup

- [Frozen raw-output HTML report](runs/report.html), [summary and derived ratios](audit-record.json),
  [raw checksums](raw-sha256.txt), and per-cell snapshots/CSV/trace files in `runs/`.
- Analytic, sign, cutoff-model, truncation, snapshot replay/corruption, optimization guard,
  report failure and real timeout checks are retained in `verification/`.
- [Original temporary probe patch](candidate/probes.patch) is preserved. Both production files
  are restored exactly to `0631ef12c`; no production algorithm or configuration changed.
- The cleanup `ci` build succeeds and [all 28 original Accelerated tests pass](verification/cleanup-focused-tests.log).
- The [independent results/cleanup review](reviews/results-review.txt) passes all three
  acceptance criteria; the final narrative/diff review is retained beside it. Full CPU/sanitizer/Vulkan suites were not rerun for this
  bounded CPU diagnostic. Existing main CI failures and BUG-241 are separate, pre-existing issues.
