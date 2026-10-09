Delta review on the revised packet (same rules: read-only, inside the repository only, no builds/runs, no other agents or
CLIs). New commit 0d581c8c9, new digest b35c917286f67a4ecc9e1ae8e68941da4ec498ff9934734dfc7f7704af46af6e (sha256 of
ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/manifest.txt). Diff from the reviewed commit 3fea80e71: /tmp/claude-1002/-home-alex-Documents-IntrinsicEngine/28308375-d660-4b1a-9430-8f199eb32064/scratchpad/m077/rev2.diff (git diff --no-ext-diff --no-textconv).
Read the revised Layer A and Layer B in full. The operator's decisions on the cost metric, the equivalence threshold and the
gates are recorded verbatim in Layer B item 4. At most ~900 words.
1. For each of your blocking findings: resolved / not resolved, with file:line evidence.
2. New problems introduced by the revision (driver, frozen analysis and its test, runner seed change).
3. The checklist, whose text you could not read before (below): evidence or n/a with reason per item; items marked † may be
   reviewed as a plan (pilot and confirmation in one packet, relative thresholds, paired arms).
4. Verdict PASS | REVISE | BLOCK naming the digest.

Checklist:
## Checklist
Items marked † (5, 7, 8, 9, 14, 16, 17, 19, 23) need evidence that a pilot or calibration run may be meant to produce. In a pilot
packet they are reviewed as a plan (what is measured, expected signature, stop condition); a
confirmation packet needs them passed.

**Question ↔ configuration**
1. The config is derived for this question; anything inherited from an earlier run is justified.
2. Every weight, threshold and tolerance has a unit and a reference scale checked against our
   data; numbers copied from papers or examples are converted; combined terms are compared in
   magnitude at initialization.
3. Borrowed terms are read from the implementation, not the paper: quantity, normalization,
   schedule, gradient paths, and whether an advertised feature actually fires.
4. If two scales or conditions coincide in all test cases, one case separates them.
5. † Predictions on new data follow a measurement of that data's own statistics.
6. Raw data and actual input/target examples were looked at, from the start and the end of each
   source.

**Mechanism** (each: if applicable)
7. † A deterministic check on a case with known answer shows the term does what the derivation
   says: values and gradients.
8. † Information flows, not only gradients: swapping or removing a source changes the output;
   changing hidden or future content leaves it unchanged.
9. † Teacher before student: an estimate used as a target beats the quantity it replaces.
10. The target is derivable from the input, and no shortcut lets the answer be read off an input.
11. Every path or case that should work is actually produced by the sampler.

**False successes**
12. Degenerate solutions (collapse, clones, shrink, wrong-but-consistent) are named and either
    excluded or caught by a hard gate. Collapse is measured scale-invariantly on the quantity
    that should carry information; no penalty can be bypassed by rescaling.
13. Ablations actually remove the information they claim to remove.
14. † A missing or deferred measurement is never a pass; it blocks.

**Measurement validity**
15. Metrics cannot be gamed: fixed units, coverage and signed values beside unsigned distances,
    subsets reported with counts, measured where targets differ rather than averaged away.
16. † Positive and negative controls show that the evaluation as a whole rejects the relevant
    false successes, not just that one mechanism works.
17. † Replication unit, justified number of replications, uncertainty analysis and the smallest
    decision-relevant effect are stated; the plan can separate that effect from noise.

**Comparison design**
18. Arms differ only in the variable under test: a switch does not shift random streams; equal
    seeds give equal weights, batches and evaluation data.
19. † Budgets are matched and long enough that the base task is learned in every arm.
20. Tuning and evaluation data are separate, and training cannot reach the evaluation set.
21. Selection rules (time point, aggregation, thresholds) are fixed before results are seen.
22. Randomized parameters are tested for their actual distribution, not only valid shapes.

**Execution**
23. † The real code path runs end to end with the final configuration (technical test, or a
    pilot packet if outputs are read).
24. Budget, guards and stop rules per stage, enforced in code; timeout means unevaluated, not
    failed; peak memory measured at the largest configuration.
25. Runtime or speed-up predictions follow a profile of the baseline and a cost model.
26. Shared devices: the repository's resource limits per device are respected; outputs are
    merged, never overwritten.

