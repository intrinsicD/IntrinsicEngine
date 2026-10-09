# METHOD-077 pre-flight — Layer B (intent)

## Operator questions and decisions (verbatim, 2026-10-09)
1. "um P nun schnell zu bestimmen brauchen wir ja K_{YX}. Hier kann man subsampeln und K_{VV}
   nutzen. Hier möchte ich gerne verschiedene varianten testen: fixes subsampling und
   resampling von den vertices/punkten. fixes sampling und resampling aus der union der
   bounding boxes mit random oder strukturiertem sampling."
2. "Interessant. Kannst du nun die sampling Varianten implementieren?" (done: METHOD-076)
3. "Ja bitte" — to running the comparison. Scope chosen by the operator (answers to the
   agent's questions): data "Synthetische Fixtures (Recommended)", size "10^5", CPD variant
   "Bayesian (BCPD) (Recommended)".
4. Decisions after the first reviews (answers to the agent's questions): cost metric
   "Wandzeit, wiederholt (Recommended)" (wall time, every timed cell run twice; kernel counts only
   explain); equivalence "RMS ≤ 1e-3 (Recommended)" (distance to the exact run, plus a
   perturbation control); gates "Paar ohne Entscheidung (Recommended)" (a failed control removes
   its pair from the decision; more than 2 of 6 such pairs: no ranking).

No goal file exists in the repository (searched by name and content). This run answers
question 1 for the chosen scope; it is the shortest path because the variants exist
(METHOD-076) and the runner is a thin wrapper over the library module the editor operation uses.
`box/mixed` is the third box schedule: the boxes follow the current source while the draw stays
the same, the box analogue of the default point scheme.

## Question
For Bayesian CPD at 10^5 points, which Nystroem landmark variant (points or bounding-box union;
mixed, fixed or resampled; random or Halton boxes) registers fastest (wall time) while the
result stays equivalent to the exact E-step?

## Configuration choices (not tuned on this data)
- `Beta 1.0`, `Lambda 2.0`: the values of the repository's Bayesian CPD tests
  (`Bayesian()` in `tests/unit/geometry/Test.CoherentPointDriftBayesian.cpp`), in normalized
  units, so the fixture's scale does not change them.
- `OutlierWeight 0.1`: the clutter share is 5 % (`full`) and about 11 % (`partial`).
- `LowRank 100`: required above 8192 source points (the full-kernel limit); G is identical in
  every arm, so the rank cannot favour an arm; its sampled error is reported per cell.
- `MaxIterations 150`: one budget for every arm; the technical tests reached it, so arms are
  compared at a fixed iteration budget, and convergence is not required.

## Hypothesis
Landmarks on the data (points) approximate the kernel on a 2-D surface far better than
landmarks spread through the boxes' volume, so box variants pass the sampled-error limit in
fewer iterations and save less time. Fixing or redrawing point landmarks matters little.

## Metrics (per cell; units)
- Speed: `seconds` (wall clock), mean of the two passes per arm (decision metric).
- Equivalence: `rms_to_exact` (world units; the fixture spans about 2.6 units, target noise
  sigma 0.004).
- Explanatory only: `nystrom_iterations` (count of up to 150), `kernel_evaluations` (count; see
  Layer A for what it misses), `rms_to_truth` (world units).

## Gates per (fixture, seed) pair (a failed gate: the pair gives no decision)
- exact: completed; on `full` also `rms_to_truth <= 0.02`.
- determinism: both passes of every timed arm have the same recorded outcome, and where both
  completed, identical iterations, `nystrom_iterations`, `kernel_evaluations`, `rms_to_exact`,
  `max_to_exact` and `sigma2` (a variant failing in both passes keeps the pair and counts as not
  equivalent; a one-sided outcome removes the pair).
- baseline B = `points/mixed/random` (positive control): `nystrom_iterations > 0` and
  `rms_to_exact <= 1e-3`.
- floor control: completed, `nystrom_iterations = 0`, `rms_to_exact <= 2e-4` (the exact fallback
  alone stays well below the equivalence threshold).
- negative control: completed, `nystrom_iterations > 0`, `rms_to_exact > 1e-3` (the evaluation
  rejects a poor approximation).
- perturbation control: completed, `nystrom_iterations > 0` and `rms_to_exact <= 1e-3` (a tighter
  approximation of the same scheme is equivalent: path sensitivity alone does not exceed the
  threshold).
More than 2 of the 6 pairs undecided: no ranking at all.

## Decision rule (fixed now; implemented in `cpd_landmark_variants.py --analyze`)
Over the k decided pairs, for each variant V other than B:
- equivalent: V completed and `rms_to_exact(V) <= 1e-3` (a failed or unevaluated V is not
  equivalent);
- faster: mean seconds ratio r = s(V) / s(B) <= 0.9 and 1 - r > 2 x noise, where noise is the
  pair's largest relative spread |s@1 - s@2| / mean over its 10 timed arms;
- V beats B: equivalent and faster in all k pairs;
- V is worse than B: not equivalent, or r > 1.1 with r - 1 > 2 x noise, in at least ceil(2k/3)
  pairs;
- otherwise: no decision.
Smallest decision-relevant effect: 10 % less wall time at equivalence, and at least twice the
measured timing spread.

## Predictions (written before the run; evaluated by the analysis; "not evaluable" without a
ranking, a decided pair or anything to compare)
- P1: In every decided pair every box variant has fewer `nystrom_iterations` than B, and no box
  variant beats B.
- P2: `points/fixed/random` is within 10 % of B in mean seconds and equivalent in every decided
  pair.
- P3: `points/resampled/random` does not beat B.
- P4: Halton versus random within the box variants shows no consistent sign in mean seconds over
  the decided pairs and schedules.
- P5: The negative control's `rms_to_exact` is at least 10 times the largest `rms_to_exact` of
  the 9 variants in every decided pair.
- P6: B engages (`nystrom_iterations > 0`) and is equivalent in all 6 pairs.
- P7: The floor control passes its gate in all 6 pairs.

## What an ineffective method would show
If landmark choice did not matter, all 9 variants would be equivalent with mean seconds within
twice the timing spread of B (their `nystrom_iterations` may still differ). If Nystroem never engaged, every variant would show
`nystrom_iterations = 0`; B's gate then fails in every pair and there is no ranking.

## Controls
- Null arm: `exact` (reference for `rms_to_exact`; tolerance-bounded `Auto`, not dense).
- Floor control: the exact fallback without fast Gauss.
- Perturbation control: B with a 100 times tighter error limit.
- Negative control: 2 starting landmarks, no error rejection (it may still double on
  non-positive sums; its gate requires that it approximated some iterations).
- Positive control: B.
- Timing: every timed arm twice, pass 2 in reverse order.

## Replication
Unit: (fixture, seed) pair, 6 pairs over two overlap regimes; arms are paired (same data, same
configuration except the variant). The landmark seed follows the fixture seed, so there are
three independent box draws and Halton shifts (`full/s` and `partial/s` share seed s). Counts and distances repeat bitwise; only wall time
varies (two samples per arm). The all-pairs / two-thirds rule needs no p-values.

## What this run cannot distinguish (stated limits)
- Landmark quality versus acceptance dynamics: the landmark count per iteration (start at the
  last passing count, doubling to 1024 on `full` and 512 on `partial`, no retry at or below a
  rejected sigma^2) is not recorded; `nystrom_iterations` and the times show the combined effect.
- The clutter cube sets the target box, so box landmarks are partly placed by the clutter; 8 of
  the 32 sampled target rows of the acceptance check are clutter points in `full`; PX moments are
  not checked by the acceptance test.
- `points/resampled` redraws farthest point by shifting its start index, not randomly.
- `exact` is tolerance-bounded `Auto`; a non-zero floor cannot be split into fast Gauss and other
  path effects.
- `kernel_evaluations` undercounts early-rejected attempts and excludes the L^3
  eigendecompositions; it explains, it does not decide.
- One size, one start landmark count (256), one error limit (1e-3), one deformation, one surface,
  one host.

## Analysis
`cpd_landmark_variants.py --analyze` writes `analysis.json`: per pair the gates, whether it is
decided and its timing noise; per variant the decision and per pair `equivalent`, the seconds
ratio, `nystrom_iterations` and `rms_to_exact`; predictions P1-P7. The referee audit recomputes
these from `results.jsonl` and the cell JSONs, and reports `rms_to_truth` and kernel counts
beside them. Unevaluated or failed cells are listed.
