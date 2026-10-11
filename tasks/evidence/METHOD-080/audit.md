# METHOD-080 — Which Nyström target rows become negative?

The failures occur in the observed **fixed FarthestPoint** and **resampled uniform union-box**
configurations. Every invalid target row in these prefixes is an injected clutter point;
all native invalid sums are finite negatives and every corresponding exact Gaussian sum is
positive. The affected targets have weak support from the current source cloud, despite nearby
landmarks. This describes the observed population, not a causal sampler comparison. ARA: **C121**.

## Sampling and exact row lists

`points/fixed/random` uses `PointSampling::Method::FarthestPoint`, beginning at index 0.
The runner sets the seed but leaves the method at that default. The `random` suffix controls
only the box pattern and is ignored for Points. Landmark identities remain fixed, half from
target and half from source; source landmark coordinates follow the current moved source.
The point count can grow during retries. `box/resampled/random` samples uniformly over the
union of the current sigma-padded target/source boxes, using seed plus draw.

Draws and target indices below are zero-based. Every attempt has 100000 targets, comprising
5000 injected clutter targets (`index % 20 == 0`) and 95000 regular targets. CSVs contain
every invalid index, normalized XYZ, label, native sum and three normalized distances.

| Sampling | Draw | Landmarks | Negative rows | Rate among clutter | Complete rows |
| --- | ---: | ---: | ---: | ---: | --- |
| Fixed FarthestPoint | 3 | 256 | 2 | 0.04% | [83500, 86500](runs/points_1.draw3.L256.invalid.csv) |
| Fixed FarthestPoint | 4 | 512 | 3 | 0.06% | [83500, 97100, 97640](runs/points_1.draw4.L512.invalid.csv) |
| Fixed FarthestPoint | 6 | 1024 | 13 | 0.26% | [All 13 IDs](runs/points_1.draw6.L1024.invalid.csv) |
| Resampled uniform boxes | 1 | 512 | 140 | 2.80% | [All 140 IDs](runs/box_1.draw1.L512.invalid.csv) |
| Resampled uniform boxes | 1 | 1024 | 27 | 0.54% | [All 27 IDs](runs/box_1.draw1.L1024.invalid.csv) |

The 13 FarthestPoint draw-6 rows are `580, 3680, 19320, 24240, 44360, 46820, 50040,
53300, 72880, 79200, 83500, 86500, 95280`. Counts across attempts are row occurrences,
not distinct targets. There are no invalid regular targets, zero sums or nonfinite sums in
any of the 26 captured attempts, including repeated/accepted attempts.

At draw 3, FarthestPoint's L512 retry passes after L256 fails; at draw 4, L1024 passes after
L512 fails. Draw 6's L1024 failure exhausts the permitted retry cost. For boxes, draw 0/L256
fails the sampled accuracy check, not target positivity; L512 passes that draw. Both draw-1
attempts above fail positivity and the cost limit prevents another attempt. This is why an
attempt with no negative rows is not automatically an accepted approximation.

## What characterizes the points

Distances refer to the **same attempt's moved source and sigma**. The comparison uses all
positive clutter rows in that attempt; posterior mass comparisons separately use the frozen
uniform valid-row sample. Nearest-other-target distance excludes the point itself.

| Attempt | Median nearest-source distance, invalid / positive clutter | Invalid nearest-landmark range | Maximum exact invalid-row inlier mass |
| --- | ---: | ---: | ---: |
| FPS draw 3/L256 | 5.94 / 2.75 sigma | 0.637–0.727 sigma | 6.87e-7 |
| FPS draw 4/L512 | 6.20 / 2.82 sigma | 0.601–0.622 sigma | 6.24e-8 |
| FPS draw 6/L1024 | 6.30 / 2.91 sigma | 0.569–0.803 sigma | 1.18e-7 |
| Boxes draw 1/L512 | 4.07 / 2.42 sigma | 0.119–1.107 sigma | 0.07633 |
| Boxes draw 1/L1024 | 4.69 / 2.44 sigma | 0.119–0.906 sigma | 0.004601 |

The invalid FPS rows lie 5.82–6.61 sigma from their nearest source; box rows lie 3.27–5.62 sigma
away. These are source-distant clutter points, not targets with no nearby landmark. None is
itself a landmark. Neighbour/landmark distances overlap other groups and do not define a
universal rejection rule; some positive clutter points are also very far from the source.
See the full within-attempt quantiles and spatial plots in the [raw-output report](runs/report.html).

All 185 invalid row occurrences fit below the frozen cap, so their exact affinities were all
evaluated, together with 256 sampled valid rows per distinct failing attempt. The independent
audit recomputed all 1465 evaluated rows. The invalid FPS rows have tiny exact posterior mass,
but **Box/L512 reaches 7.633%**. The fixture's clutter label therefore does not justify treating
every invalid row as having zero significance. The regular-target valid samples have high
posterior mass; those samples are not a complete accuracy audit of every positive row.

## Where the sum becomes negative

In the restored [E-step implementation](../../../src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp),
the weighted landmark aggregate `a` is nonnegative in every failing snapshot. Multiplication
by the truncated pseudoinverse creates signed coefficients at line 1465:

```text
a = sum_i exp(shifted_log_weight_i) k(Z, y_i)
g = W_plus a
S_j = k(x_j, Z) g
```

Signed `g` is not by itself invalid. The affinity sum becomes invalid when the final dot product
at **line 1480** becomes negative. The positivity guard at line 1481 rejects the attempt before
outlier/denominator normalization at line 1482 and before the subsequent h/q/source pass.
Positive Gaussian entries and a positive-semidefinite low-rank construction do not guarantee
elementwise-positive reconstructed cross-affinities.

For target **220**, boxes draw 1/L512, the positive landmark-dot terms sum to **+497.6067573**,
the negative terms to **-505.3952536**, producing native **-7.7884962** instead of exact
**+0.3272350**. For target **38420** in that same attempt, native is **-48.7881895** versus
exact **+14.5735990**, with exact inlier mass **2.342%**. These are shifted weighted sums;
the common shift cancels only when outlier normalization restores it consistently.

The diagnostic `nan` sampled-error field on these attempts means the temporary probe initialized
the **not-yet-computed** error to NaN and target validation returned early. It is not a NaN
affinity sum. The raw row census distinguishes this explicitly.

Rebuilt long-double Gaussian target kernels with saved native `g` preserve all invalid signs.
That checks the final kernel/dot arithmetic, not a newly recomputed eigensystem for every row.
The two byte-identical first-row snapshots retain METHOD-079's broader arithmetic conclusions;
those conclusions are not silently extended to the new rows. Near landmarks alone do not
guarantee relative accuracy for tiny target/source affinities, and this census does not establish
a universal inverse-instability or coverage mechanism.

## Evidence and closure

The [packet](packet.md), [preflight approval](reviews/approval.json), [raw manifest](raw-sha256.txt)
and [audit binding](audit-record.json) identify execution commit `37c2a0e0a` and the four frozen
cells. Both repeats have identical native captures, summaries and final positions. Raw evidence
is unchanged after execution. The [independent review](reviews/results-review.txt) approves
all three task acceptance criteria after recomputing the row lists, geometry and exact sums.

Both native files are restored exactly to `dcbb0e599`; the [probe patch](candidate/probes.patch)
is retained. The `ci` cleanup build and [28/28 original Accelerated tests](verification/cleanup-focused-tests.log)
pass. Controls include known Gaussian/log-mass answers, mutation rejection and the real final
configuration path. No full CPU, sanitizer or GPU suite was run. Structural checks retain the
pre-existing BUG-241 document-link failures; no new production backend, guard or parameter changed.

This answers the four questions for the recorded fixture and prefixes. Other samplers, datasets,
full positive-row accuracy and a production repair remain untested. No timing or adoption claim follows.
