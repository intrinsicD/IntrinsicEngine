# Intent and assessment

1. "Also heißt das identifizieren und exakt nachrechnen?"
2. "Dann versuchen wir es damit mal."
3. "Mach dann nochmal einen Test bitte"

Operator-directed shared CPD/BCPD work outside Framework24 P0; no separate goal file.
Q1/Q2 are implemented and checked by analytic regression. Q3 is answered by repeating the
already diagnosed synthetic failure prefixes against the current implementation and matching
Auto endpoints; this is the shortest bounded check, not a new sampler search.

Full preflight tier: the result informs whether the candidate addresses the observed rejection.
Hypothesis/prediction: complete exact replacement rescues at least one additional accepted
Nyström iteration across the two prefixes, without reducing the other's accepted count, while
endpoint displacement from Auto stays RMS <= .001 and max <= .01 world units and
status/termination/iteration count stay equal. These are the previous diagnostic tolerances.
An ineffective method gives zero gain, worsens one variant, or fails any endpoint gate.
The bounded prediction is not supported in those cases. No automatic tuning or additional
failure-layer repair follows. The implementation may remain as a reviewed candidate; this
screen cannot establish a general runtime benefit, convergence, posterior positivity or an
optimal sampler. A new failure layer is reported to the operator.

Metrics: accepted Nyström iterations (count; resolution one iteration), sampled error per
accepted iteration (relative estimate, not bound), sigma² (world units²), NLL/stopping objective
and matched mass in the raw trace, endpoint RMS/max (world units), registration wall seconds.
No numerical zero/negative invalid row is dropped as clutter; all source statistics are repaired.
Positive row sums still do not certify pairwise positivity.

Controls before results: 41 focused accelerated/Bayesian tests pass. The analytic two-landmark
fixture has known negative affinity and a sampled underflow row; repair matches every exact
sufficient statistic for shifted weights, with/without outliers, serial/parallel. Before repair
both new regressions fail. Removing the budget, sampled-accuracy guard, stable sampled-row
bypass or exact PXy update individually fails the corresponding regression. compare.py self-test
rejects zero, collapsed, displaced and nonfinite position buffers; identity passes. A no-op
accepted-count delta is rejected; an improvement in one variant coupled with a regression in
the other is rejected. Test these analysis controls with their guard removed before freezing.
These are deterministic tests, not scientific observations. The native Auto cells are the
positive reference arms. No separate disabled-update run is needed: collapsed/identity/no-op
null probes cover the stated endpoint and accepted-count false successes. No visual-only gate.
The synthetic report check also rejects a missing trace and preserves an unevaluated HTML
report with a named failure when a completed-looking cell has invalid data.

Two repetitions in reverse order per arm/variant test replay and expose timing variation;
they are not independent seeds and carry no population CI. Require byte-identical endpoints
and iteration-trace numeric fields excluding elapsed time within each repeated cell. Report
both timing values/ranges, not a significance claim or speed ranking. The method signal is
one accepted iteration; the timing screen has no adoption threshold and is descriptive only.
One fixture/seed is enough only for this already observed failure, not generalization.

The baseline and candidate share runner, fixture seed, physical cores, initial parameters,
iteration budget and landmark randomness. Later states/draw sizes can diverge because the
repair changes accepted steps; that is part of the intervention, not a matched-state causal
claim. Compare points to its 7-step Auto prefix and box to its 2-step Auto prefix.

Enforced technical kill conditions: timeout, memory/process failure, missing/nonfinite/wrong
shape output, inconsistent trace or changed frozen digest. Preserve partial report and mark
UNEVALUATED. A quality/hypothesis surprise alone is retained and the frozen remaining cells
continue. Reviewers judge the packet prospectively; final claims require a separate raw audit.

The exposed BCPD trace is a likelihood/stopping diagnostic, not a decomposed variational ELBO.
There is no training loss mixture to audit: logged NLL weight 1, raw = weighted; 100% share of
logged NLL. Do not infer unreported variational loss components from this trace.
