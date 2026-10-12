# Preflight A — actual Round 2 response

Session `/root/method083_preflight_a`; independent and read-only, no other review read.

1. **User question:** “Bitte pass unsere implementieren entsprechend dieser Punkte des Original Codes an. Dann wiederhole den Vergleich, auch mit dem Original.”

The packet serves that question: the existing cases compare old/adapted engine policies and the author’s executable without adding an unrelated method or performance campaign. Retaining the engine’s safeguards is disclosed. “Ok ohne Claude” authorizes the two-Codex substitution.

**Verdict: REVISE** for commit `0e37ea222721eddeb9b41a79dea26b4bd33898f6`, digest `3f19fe4028f3d59d15d831b4ff6fba75973ef461aa7b9f5625f191d0c4d465e5`.

What this change does: it rejects improvement caused only by normalization, preserves missing Auto evidence, validates upstream final numerical state, and expands replay checking. Those repairs are correct and appropriately small. Two items still prevent execution approval:

1. **A censored original can crash replay reporting** (`comparison.py:147`). When the original attempt has no trace and the replay has an endpoint, `np.asarray(first[1])` is one-dimensional; the two-dimensional slice raises `IndexError` before endpoint availability is checked. I reproduced this by evaluating the actual expression in memory. Check availability before comparing traces, preserve unavailable replay comparisons as unevaluated, and add that negative control.

2. **Final-configuration terminal technical coverage is missing** (`verification/technical.py`). The retained test deliberately kills every native process after its first completed-step marker. It verifies initialization and one update, not the final-configuration path through termination, endpoint output, native metadata and parsing. The 64-point/full-rank adapter test and synthetic report tests cover useful separate pieces, but do not establish checklist item 23. Supply terminal technical evidence without interpreting the numerical outputs as experiment results.

The coordinator subsequently notified me that a further delta is planned. I did not open another reviewer’s report or the `reviews/` directory.

**Independent verification**

- Regenerated the manifest using the exact frozen command: digest matches.
- Confirmed 58 unique cells and the enforced 54,000-second total budget.
- Rechecked all six actual fixtures in memory: native-frame no-op, raw identity and collapse fail P2; known-truth endpoints pass.
- Confirmed upstream endpoint quantization fits the stated additive margin.
- Rechecked actual `load()` rejection of nonfinite final upstream sigma, mass and scale.
- Inspected the before-fix failure, full-check pass and five removal-mutation records.
- Reviewed the delta’s callers and implementation. No additional complexity finding.

**Full preflight checklist**

| # | Assessment |
|---|---|
| 1 | Pass: configurations map to Q1; retained choices and confounders are documented. |
| 2 | Pass: solver/world units, shared scale, weights and thresholds are explicit; rounding margin checked. |
| 3 | Pass: borrowed solve, sampling and sigma trigger reconstructed from actual implementations. |
| 4 | Pass: transition tests distinguish sigma from sigma²; unequal cloud sizes distinguish union sampling from quotas. |
| 5 | Pass for scope: existing fixed fixtures; no new-data calibration or population prediction. |
| 6 | Pass: inspected retained first/last source, truth and target examples for all six fixtures. |
| 7 | Pass: known-answer ridge/repair/transition tests and mutation evidence; gradients are inapplicable. |
| 8 | Pass for scope: truth-swap adapter check excludes truth leakage; landmark tests verify moving identities. |
| 9 | N/A: no teacher or learned target estimator. |
| 10 | Pass with limit: truth is excluded from solver inputs. Hidden partial-overlap truth is not assumed identifiable. |
| 11 | Pass: explicit exhaustive native-cell list and sampler contract tests. |
| 12 | Pass after repair: raw/frame-aware no-op, collapse and wrong-consistent controls; scale-invariant spread screen. |
| 13 | Pass for stated interpretation: no claim that upstream J256/J0 is a clean E-step ablation. |
| 14 | **Open:** empty-original replay handling; required terminal technical evidence remains absent. |
| 15 | Pass for endpoint questions: fixed world units, indexed truth distances, maxima and subset counts. No directional-bias claim. |
| 16 | Pass for repaired endpoint evaluator: positive/negative controls and removal mutations; replay-censoring control still needed. |
| 17 | Pass for descriptive scope: six fixed paired cases, explicit effect scales and limited replay coverage; no population inference. |
| 18 | Pass with disclosed limits: engine G construction held fixed; cross-program and upstream J-dependent RNG differences are explicit. |
| 19 | Pass for native-outcome comparison: matched caps/time bounds, distinct stopping rules, failures retained. This is not equal-training-budget causal evidence. |
| 20 | Pass for scope: no outcome-based tuning; truth does not enter optimization. |
| 21 | Pass: endpoint selection, summaries and screens frozen before new results. |
| 22 | Pass: uniform box-distribution test, exact union-identity tests and uniform permutation implementation; no cross-program draw matching. |
| 23 | **Not met:** first-update prefixes are not terminal final-configuration coverage. |
| 24 | Pass: enforced process/total time and address-space limits; largest-size prefix memory checked; terminal RSS collected when available. |
| 25 | N/A: no speed-up prediction or performance claim. |
| 26 | Pass: two disjoint CPU sets, fixed thread limits, fresh output directory and unique cell files; no GPU sharing. |

No repository `LESSONS` file or separate goal file was found.

The repaired controller changes are accepted **apart from item 1 above**. This does not accept experiment completion or any P1–P3 conclusion. Material limits remain: different G bases and stopping rules, unavailable full ELBO/trajectory observations, descriptive fixture coverage, and no causal attribution to an individual adaptation.

Not checked: no native experiment or new build ran; existing full-check/mutation logs were inspected rather than rerun. Execution approval remains withheld until the two items above are resolved on a new frozen digest.
