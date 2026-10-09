**Verdict: REVISE** on digest `b35c917286f67a4ecc9e1ae8e68941da4ec498ff9934734dfc7f7704af46af6e`; I took the digest as given and could not recompute it. All seven round-2 blockers are resolved. Four new blockers remain, each a small fix: two in the frozen analysis (N1, N2) and two from the checklist (C1, C2).

**Rule compliance:** read-only. The only file outside the repository I opened is the diff you supplied. Inside it I read Layer A and B, the driver, the runner, the new analysis test, and parts of `PointSampling.cpp`, `CoherentPointDrift.cpp` and `Test.CoherentPointDriftAccelerated.cpp`.

### 1. Round-2 blockers
- **B1 (cost metric), resolved.**
  - Wall time is now the decision metric, by operator decision (`layer-b.md:12-14,35`).
  - Every timed arm runs twice, pass 2 in reverse order, pinned to CPUs 0-15 (`layer-a.md:17-19,61-67`; driver `:60-72,139`).
  - The rule compares mean seconds (driver `:253-254`).
- **B2 (kernel-count undercount), resolved.** It is documented (`layer-a.md:98-100`, `layer-b.md:110-111`) and the count is now explanatory only (`:38-39`).
- **B3 (negative control passing trivially), resolved.** Its gate now requires a completed run that approximates some iterations and has `rms_to_exact` > 1e-3 (`layer-b.md:49-50`; driver `:236-238` via `:198-200`).
- **B4 (one landmark draw), resolved.** The runner sets the landmark seed from the fixture seed (runner `:151-153`). Farthest-point sampling never reads the seed (`PointSampling.cpp:529-538`), so G (`CoherentPointDrift.cpp:761`) and the point arms are unchanged.
- **B5 (positive control), resolved.** It is now a gate (`layer-b.md:45-46`; driver `:231-233`; test `:102`).
- **B6 (failed cells), resolved.** See `layer-b.md:57-58`, driver `:218,252`, tests `:102,111`; but see N1.
- **B7 (operator decisions), resolved.** They are recorded verbatim (`layer-b.md:12-16`) and implemented (driver `:241,246`).

### 2. New problems
**Blocking**
- **N1. The determinism gate in Layer B and in the code disagree.**
  - Layer B requires both passes of every timed arm to complete (`layer-b.md:43-44`).
  - The code skips any arm that is missing a pass (driver `:222-224`). So if V@1 completes and V@2 fails or times out, the pair stays decided and V just counts as "not equivalent".
  - A one-sided outcome is itself a determinism failure, yet here it counts as a pass (item 14).
  - Fix: require the same outcome in both passes and identical counts where both completed; align the text; add a test.
- **N2. The perturbation gate does not require the control to engage** (driver `:239-240`; `layer-b.md:51-52`).
  - If the 1e-5 limit is never met, the arm runs the exact fallback (like the floor control) and passes trivially. The claim "path sensitivity alone does not exceed the threshold" is then never measured (items 12, 14).
  - Fix: add `nystrom_iterations > 0` to the gate, with a test.

**Optional**
- "Worse" (r > 1.1) has no noise condition, while "faster" does (driver `:256` vs `:254`).
- P1's second clause, P3 and P4 come out true when there is no ranking or nothing to compare (driver `:278,282,296`). Report them as "not evaluable" instead.
- The analysis test does not cover the perturbation gate, the baseline RMS check, the exact truth limit, the slowness rule or P1–P7.
- Resume refuses any change of commit (driver `:133-138`), so a commit in this checkout during the ~4.5 h run would strand the run. Run from a worktree at the frozen commit.
- If `taskset` is missing, the driver silently runs unpinned (`:139`), and the analysis ignores the `pinned` field.
- Run `--analyze` once on the 2000-point technical-test output as a format check.
- Seed-change wording: `full/s` and `partial/s` share seed s, and `partial` is a subset of `full`'s draws (`layer-a.md:35-36`). The draws therefore differ between seeds (3 independent draws), not "between pairs" as `layer-b.md:96-97` says.

### 3. Checklist (items marked † reviewed as a plan)
1. **Partly met → C1, blocking (one line).** The variants, size and BCPD follow from Q1 and the operator's answers. But β=1 and λ=2 (as in `Bench.CoherentPointDriftReferenceSmoke.cpp:104-105`), ω=0.1, LowRank 100 and the 150-iteration cap are inherited without justification. The cap matters: according to the task log, every technical-test run hits it.
2. Met for the decision thresholds: world units against the 2.6 span and 0.004 noise (`layer-b.md:36-37`). Combined terms: n/a, the library objective is identical across arms.
3. Met: Layer A (`:46-59,95-105`) describes the code's actual behaviour. Whether a control actually engages is gated for B and the negative control, not for the perturbation control (N2).
4. n/a for the decision. Fixture and landmark cap coincide (1024 on `full`, 512 on `partial`), which is stated (`:101-103`); fixtures are strata, not a factor. Cell order is separated by the reversed second pass.
5. † Plan: the thresholds are operator decisions or ratios, and the gates check each pair against its own controls.
6. Met: `--dump 3`, seed 1, both fixtures (`layer-a.md:37-38`).
7. † Plan: METHOD-076 unit tests (`Test.CoherentPointDriftAccelerated.cpp:675-951`) and the analysis test.
8. † Plan: schedules and patterns change the landmarks (test `:719-784`); the seed change cannot affect the point arms.
9. † Plan: `exact` is gated by the truth limit (≤ 0.02) and the floor control (≤ 2e-4).
10. Met: the reference is read only after `Register` (runner `:192-212`).
11. Met: 138 cells = 6 × 23 (driver `:60-72`).
12. Partly met: a never-engaging B, a negative control that is not negative, a fallback-only V and wrong-but-consistent results are caught; a non-engaging perturbation control is not (N2). Scale invariance is n/a (fixed world units).
13. Met: floor `nystrom_iterations = 0` and negative-control engagement are gated.
14. † **Not met:** N1, N2.
15. Partly met: an RMS can hide a local deviation; reporting `max_to_exact` per pair is optional.
16. † Plan: positive, negative, floor and perturbation gates (subject to N2).
17. † Plan: 6 pairs, i.e. 3 independent seeds × 2 overlap regimes; smallest effect 10% and more than twice the measured timing spread (`:59-65,94-98`).
18. Met: same data and same landmark seed within a pair; a fresh process per cell.
19. † Plan: 150 iterations in every arm; `exact` is truth-gated.
20. n/a: nothing is tuned.
21. Met: frozen in driver `:208-301`, with a test.
22. **Not met → C2, blocking.**
    - The box landmarks are tested only for valid shapes, i.e. that they lie inside the padded boxes (`Test.CoherentPointDriftAccelerated.cpp:712-715,964-969`), not for their distribution.
    - My reading of `EStep.cpp:102-111` supports uniformity over the union of the boxes.
    - Owed: a small test that the landmark shares in A-only, B-only and A∩B match the volume shares, for both random and Halton.
23. † Plan: the final configuration has not run at 10^5.
    - The 10^5 technical test used the earlier runner.
    - At 2000 points the timed arms cannot engage Nystroem because of the cost cap (`EStep.cpp:1657-1658`).
    - Gates and timeouts serve as the stop condition.
24. Met: a timeout is recorded as unevaluated (driver `:184-187`); peak memory 394 MB at 10^5.
25. Partly met: a cost model exists (`EStep.cpp:1644-1657`), but the baseline profile is only total times (106 s / 88 s). Optional: record when B falls back to exact for good, to show a 10% win is reachable.
26. Met: one cell at a time, a lock file (driver `:111-121`), append-only records; I found no per-device limits in the repository.

### 4. Verdict
**REVISE** on `b35c917286f67a4ecc9e1ae8e68941da4ec498ff9934734dfc7f7704af46af6e`.
- Blocking:
  - N1, N2: frozen analysis plus tests.
  - C1: one line of justification.
  - C2: one distribution test.
- Optional: the items listed above.

Not checked: whether logical CPUs 0-15 really are one per core, and whether `manifest.txt` is committed.