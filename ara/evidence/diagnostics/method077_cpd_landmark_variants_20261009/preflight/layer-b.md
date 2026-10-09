# METHOD-077 pre-flight — Layer B (intent)

## Operator questions (verbatim, 2026-10-09)
1. "um P nun schnell zu bestimmen brauchen wir ja K_{YX}. Hier kann man subsampeln und K_{VV}
   nutzen. Hier möchte ich gerne verschiedene varianten testen: fixes subsampling und
   resampling von den vertices/punkten. fixes sampling und resampling aus der union der
   bounding boxes mit random oder strukturiertem sampling."
2. "Interessant. Kannst du nun die sampling Varianten implementieren?" (done: METHOD-076)
3. "Ja bitte" — to running the comparison; scope chosen by the operator: synthetic fixtures,
   10^5 points, Bayesian CPD.

No goal file exists in the repository. This run answers question 1 for the chosen scope; it is
the shortest path because the variants exist (METHOD-076) and the runner reuses the library
path the editor uses.

## Question
For Bayesian CPD at 10^5 points, which Nystroem landmark variant (points or bounding-box union;
mixed, fixed or resampled; random or Halton boxes) computes P (via K_YV K_VV^+ K_VX) with the
fewest kernel evaluations while the registration stays equivalent to the exact E-step?

## Hypothesis
Landmarks on the data (points) approximate the kernel on a 2-D surface far better than
landmarks spread through the boxes' volume, so box variants pass the sampled-error limit in
fewer iterations and save less work. Fixing or redrawing point landmarks matters little.

## Metrics (per cell; units)
- Cost: `kernel_evaluations` (count; deterministic, primary) and `seconds` (wall clock,
  secondary).
- Equivalence to exact: `rms_to_exact` (world units; the fixture spans about 2.6 units, target
  noise sigma 0.004).
- Accuracy to ground truth: `rms_to_truth` (world units), reported, not used for the decision.
- Use of the approximation: `nystrom_iterations` (count of 150).

## Decision rule (fixed now)
Baseline B = `points/mixed/random` (the METHOD-053 default). For each variant V and each of the
6 (fixture, seed) pairs, V is equivalent if `rms_to_exact(V) <= 1e-3`, and cheaper if
`kernel_evaluations(V) <= 0.9 * kernel_evaluations(B)`.
- V beats B: cheaper and equivalent in all 6 pairs.
- V is worse than B: in at least 4 of 6 pairs either not equivalent or more expensive than B by
  more than 10 %.
- Otherwise: no decision for V.
Smallest decision-relevant effect: 10 % fewer kernel evaluations at equivalence (kernel
evaluations dominate runtime at this size). Wall time is reported with the timing noise from the
repeat cell (|seconds(B) - seconds(B#repeat)| / seconds(B)); a wall-time difference below twice
that noise is not interpreted.

## Predictions (written before the run)
- P1: Every box variant has fewer `nystrom_iterations` than B in every pair, and no box variant
  beats B.
- P2: `points/fixed/random` is within 10 % of B in kernel evaluations in every pair and
  equivalent (rms_to_exact <= 1e-3).
- P3: `points/resampled/random` does not beat B.
- P4: Within the box variants, Halton versus random shows no consistent sign in
  `kernel_evaluations` across the 6 pairs.
- P5 (control): the negative control has `rms_to_exact` above 1e-3 and at least 10 times the
  largest `rms_to_exact` of the 9 variants, in every pair.
- P6 (control): B has `nystrom_iterations > 0` and `rms_to_exact <= 1e-3` in every pair
  (METHOD-053 measured equivalence for rigid and low-rank nonrigid CPD).

## What an ineffective method would show
If landmark choice did not matter, all 9 variants would have equal `nystrom_iterations` and
kernel evaluations within the repeat noise. If Nystroem never engaged, every variant would show
`nystrom_iterations = 0` and identical results to `exact` (then the comparison says nothing).

## Kill conditions (stop and report, no ranking)
- An `exact` cell fails, or `exact` on `full` has `rms_to_truth > 0.02` (the registration itself
  is broken).
- B has `nystrom_iterations = 0` in every pair (P6 fails: the setting does not exercise the
  approximation).
- The negative control is equivalent (`rms_to_exact <= 1e-3`) in any pair: the equivalence
  metric does not reject a poor approximation.

## Controls
- Null arm: `exact` (exact E-step choice, `Auto`); reference for `rms_to_exact`.
- Negative control: 2 landmarks with every approximation accepted (limit 1.0).
- Positive control: B, whose equivalence METHOD-053 measured on other CPD variants.
- Timing repeat: B run twice.

## Replication
Unit: (fixture, seed) pair, 6 pairs; arms are paired (same data, same configuration except
the landmark variant; landmark seeds identical). Landmark draws are deterministic, so a repeat
reproduces every count bitwise; only wall time varies. Six pairs over two overlap regimes are
enough for the all-pairs / 4-of-6 rule; no p-values are computed.

## Analysis
One table per fixture: per variant, the 3 seeds' `nystrom_iterations`, `kernel_evaluations`
relative to B, `rms_to_exact`, `rms_to_truth`, `seconds`; then the decision per variant and the
predictions P1-P6 as passed or failed. Unevaluated cells are listed and count as no decision.
