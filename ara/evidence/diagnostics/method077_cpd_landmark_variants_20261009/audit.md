# METHOD-077 referee audit (2026-10-10)

Run: commit `74a198c0e`, packet digest `7bb6c4250b736f4f1f4e1e707546a13fbe856fe2b1815db434da8dcb22bc4e3f`
(both reviewers PASS: Claude session `656249ac-…`, Codex thread `01a121ef-…`; verdicts in
`reviews/`), start gate: live manifest digest equal, tree clean. Driver
2026-10-09 21:56 to 2026-10-10 00:48, exit 0. All 138 cells completed; one commit, one runner
sha256, 10^5 points, pinned. Raw records: `runs/results.jsonl`, `runs/cells/`; reference position
files are not committed (sha256 in the exact records).

## Frozen analysis (`runs/analysis.json`, unchanged)
- No ranking: the exact gate fails on all three `full` pairs (`rms_to_truth` 0.158, 0.157, 0.156
  > 0.02), so only 3 of 6 pairs are decided.
- Every other gate passes in every pair: determinism, baseline engagement and equivalence,
  floor, negative and perturbation controls; timing noise at most 0.7 %.
- Predictions: P1 and P3 not evaluable (no ranking); P2, P4, P5, P6, P7 hold.

## Recomputed from the raw records (agrees with the analysis)
- The exact registration does not solve the fixture: the similarity scale converges to about
  1.011 against the true 1.1, `rms_to_truth` about 0.157 (`full`) and 0.80 (`partial`), and every
  run stops at the 150-iteration cap. The `partial` fixture has no truth gate by design; its
  truth error is larger still.
- Nystroem engages in very few of the 150 iterations: points variants 2-6, box variants 0-1,
  perturbation control like the baseline. So the landmark variant affects a handful of early,
  wide-kernel iterations; the rest of every run is the exact fallback.
- All variants stay equivalent to `exact` (largest `rms_to_exact` 4.2e-4, box variants on
  `partial`); the negative control is far off (0.07-0.92).

## Descriptive only (not a decision: the frozen rule gives no ranking; labelled exploratory)
- Mean seconds, `full`: points variants 84-95 s, box variants about 102 s, floor (all exact)
  about 106 s. `partial`: points 54.5-56.1 s, box 56.1-58.3 s, floor about 58 s.
- Box variants on one seed give identical results for all three schedules and both patterns'
  schedules: with at most one Nystroem iteration (the first), mixed, fixed and resampled draw the
  same landmarks.
- The negative control ends after about 1 s in most pairs (4 Nystroem iterations, then the run
  terminates); its distances are large, so its gate holds.

## Surprise and consequence
The experiment rested on a fixture and configuration that the exact method does not register
(scale and shape far from the truth) and on a regime where the approximation runs in only a few
iterations. The question (which landmark variant computes P fastest) is not answerable from this
run beyond "box landmarks are accepted less often than point landmarks". Per the pre-flight
rules the remaining plan is void; no follow-up runs were started.

## Lesson (proposed safeguard)
A comparison of approximation variants needs a calibration check before the packet freezes:
the reference arm must solve the fixture (accuracy to the ground truth) and the approximation
must be active in a meaningful share of the work. The technical test deliberately did not read
accuracy, and the reviewers accepted "the base task is learned in every arm" as a plan item;
both were insufficient here.
