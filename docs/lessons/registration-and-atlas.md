# Registration and atlas lessons

These are bounded historical conclusions from completed experiments, not new
runs or universal guarantees. The claim ledger retains numerical outcomes and
negative results. Raw completed-task evidence was removed from the checkout;
the [retirement record](evidence-retirement.md) identifies its Git snapshot.

## Nyström CPD

- **Calibrate before comparing approximation variants.** METHOD-077's frozen
  landmark comparison ran all 138 cells, but the exact arm did not register
  the full fixture and Nyström ran in at most six of 150 iterations, so it
  gave no ranking. Before freezing, check that the reference arm solves the
  fixture against ground truth and that the approximation is active in a
  meaningful share of the work. See [C125](../../ara/logic/claims.md#c125-method-077s-landmark-variant-comparison-yields-no-ranking-because-the-exact-arm-misses-the-full-fixture)
  and the [result summary](../results/method077_cpd_landmark_variants_2026-10-10.md).
- **Retry only when something relevant changes.** METHOD-078's unconditional
  retries repeated rejected approximations without increasing accepted steps.
  A synthetic counterexample to caching did not demonstrate a benefit on the
  real tested trajectory. See [C119](../../ara/logic/claims.md#c119-removing-rejection-memory-adds-no-accepted-nystrom-steps-in-the-method-078-pilot).
- **PSD is not elementwise positivity.** METHOD-079's reconstructed affinities
  stayed negative under the bounded arithmetic checks. Higher-precision
  accumulation alone did not repair them; two rows do not establish a universal
  cause. See [C120](../../ara/logic/claims.md#c120-first-row-nystrom-sign-failures-persist-under-the-bounded-arithmetic-checks).
- **Clutter can still carry posterior mass.** METHOD-080 found negative rows
  among injected clutter, but their exact posterior contribution was not always
  negligible. Clutter membership cannot justify zeroing/clamping; nearby
  landmarks do not establish source support. An uncomputed diagnostic NaN must
  be distinguished from a nonfinite affinity. See [C121](../../ara/logic/claims.md#c121-complete-invalid-row-census-in-the-two-observed-nystrom-configurations).
- **Repair sufficient statistics consistently.** METHOD-081 repairs an entire
  row across E-step statistics with weighted shifts, deterministic reduction,
  an explicit repair-cost budget and accuracy checks. One rescued prefix step
  did not prove full-convergence preservation. See [C122](../../ara/logic/claims.md#c122-exact-row-repair-candidate-rescues-one-fixed-fps-nystrom-step-in-the-bounded-retest).
- **Convergence is not correct registration.** METHOD-082's partial-overlap
  endpoints worsened truth error even when native convergence and decreasing
  objective values looked successful, including the Auto comparator. Use known
  geometry truth and null/identity/collapse controls in addition to numerical
  stopping rules. Preserve timeout/unknown endpoints and the unexplained
  wrapper exit; replay coverage was limited to full-overlap seed 1.
  See [C123](../../ara/logic/claims.md#c123-method-082s-all-cell-convergence-and-auto-endpoint-prediction-is-refuted)
  and [C124](../../ara/logic/claims.md#c124-method-082s-broader-registration-prediction-fails-on-partial-overlap).

## Atlas quality

Keep semantic region labels separate from UV chart labels: internal cuts can
change topology without erasing protected region boundaries. Fewer charts or
shorter seams alone do not establish better distortion, packing or semantic
parts. Independent final checks must catch overlap and post-pack violations.
The existing [baseline-preserving study](../methods/baseline_preserving_atlas_experiment.md)
and [boundary-refinement study](../methods/uv_boundary_refinement_experiment.md)
retain the rejected alternatives and finite-cohort tradeoffs.

METHOD-047 also shows why chart fragmentation belongs in quality comparisons:
a non-optimizing objective can meet distortion limits by excessive subdivision.
Its [durable result summary](../methods/property_guided_atlas.md#historical-correctness-observations)
keeps that limitation, the audit boundary, and the distinction between CPU,
GPU and performance evidence.
