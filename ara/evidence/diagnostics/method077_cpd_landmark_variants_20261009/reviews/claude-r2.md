I reviewed the packet with digest de8b3cc3d307a814f87c20761271ca79ff8afe64ed7eff241f2f4bafcdd1fa56, taken as given; I could not re-hash it without running anything. My verdict is **REVISE**: seven blocking findings, all fixable in text or in the decision rule, plus one optional one-line runner change.

**Rule compliance:** I read only files inside the repository: `layer-b.md`, the METHOD-077 task file, and searches for a goal file and for editor use of the library. The `experiment-preflight` checklist sits outside the repository, so I could not read its item texts. My numbering in section 3 follows your six categories and may not match the skill's own numbers. No rule broken.

### 1. Questions and scope
- **Coverage:** the run serves question 1. Every family it names is in the plan: fixed and resampled point landmarks, fixed and resampled sampling from the box union, random and structured (Halton) boxes.
  - `points/mixed` is the paired baseline B.
  - `box/mixed` (12 cells) is not named in question 1. It is defensible as one reading of "fixes sampling" for boxes, but Layer B should say so.
- **Scope:** synthetic data, 10^5 points and Bayesian CPD match the configuration. The scope is a paraphrase; only "Ja bitte" is quoted verbatim.
- **Reframing:** "schnell" becomes "fewest kernel evaluations while `rms_to_exact` ≤ 1e-3". That is a reasonable reading, but it reframes question 1, and the kill conditions can block the answer. The global workflow makes both the operator's decision, and no such decision is recorded (B7).
- **No goal file:** confirmed; I found none by file name or content.
- **Shortest path:** sound. The runner is a thin command-line wrapper over the same library module the editor uses (`Runtime.RegistrationOperations.CoherentPointDrift.cpp`), and 78 cells in about 2.2 h is proportionate.

### 2. Round-1 reconstruction against the intent
These match:
- a variant that always falls back to exact cannot win, because the cost criterion rejects it;
- the doubling, the σ² latch and the unrecorded landmark count;
- clutter setting the box landmarks, and 8 of the 32 sampled target rows being clutter;
- `points/resampled` being a shift of the start index;
- the stated timing limits;
- the floor control differing from `exact` only by fast Gauss.

Mismatches (all blocking):
- **B1. The cost metric is not shown to track speed.**
  - Layer B says "kernel evaluations dominate runtime at this size" (`layer-b.md:42-43`).
  - The library's own cost model charges about 2L³ per Nystroem attempt for the single-threaded eigendecomposition (`Geometry.Registration.CoherentPointDrift.EStep.cpp:1644-1647,1657`). `kernel_evaluations` excludes that (`layer-a.md:86-88`).
  - At L ≥ 512 (the doubling can reach 1024 in `full`), the uncounted cost is larger than the counted (2M+N)·L. Landmark selection, which differs by schedule, is also uncounted.
  - So "V beats B" could be declared for a variant that is slower.
  - Fix: veto "beats B" when `seconds(V)` exceeds `seconds(B)` by more than twice the repeat noise, and correct the claim. The alternative is to record L per iteration and add 2L³.
- **B2. `kernel_evaluations` undercounts early-rejected attempts.**
  - When `NystromRows` returns early (`EStep.cpp:1426,1438-1440,1497,1520`), `evaluations` stays 0 and the caller adds only n·L (`:1659-1668`). Between L² + m·L and L² + 2m·L evaluated terms go uncounted.
  - Layer A says every attempt is counted (`layer-a.md:86-87`).
  - Fix: correct the text and state the bound — a handful of attempts per run, mostly in the negative control.
- **B3. The negative-control kill condition passes trivially if that cell fails.**
  - An incomplete result writes `rms_to_exact: null` (`CoherentPointDriftLandmarkVariantsRunner.cpp:195-199`).
  - The kill condition fires only on "equivalent" or "never approximates" (`layer-b.md:73-75`), so a failed negative control triggers neither.
  - Fix: require a complete result with `rms_to_exact` > 1e-3 and `nystrom_iterations` > 0 in every pair.
- **B4. Random versus Halton rests on a single landmark draw.**
  - The runner never sets `LandmarkSampling`, so the box seed is 0 in every cell (`CoherentPointDrift.cpp:228`, `PointSampling.cppm:128`, `EStep.cpp:98-101,1358-1360`).
  - The six pairs therefore replicate the data, not the draw. Yet P4 and question 1's "random oder strukturiert" are judged across pairs (`layer-b.md:53-54,90-93`).
  - Fix: either derive the landmark seed from the fixture seed in the runner (the seed is documented for `Random` only, so the farthest-point arms should not change), or state the limit and narrow P4 to "this draw".

### 3. Checklist (my numbering)
**Question and configuration**
1. Questions verbatim, and every cell maps to one: yes (`layer-b.md:3-10`); `box/mixed` needs its justification stated.
2. Goal file and shortest path: no goal file; the shortest-path claim holds.
3. Configuration matches the question: yes (runner `:139-155`, driver `:26-50`).
4. Reframing and gates approved by the operator: **no (B7)**.

**Mechanism**
5. Hypothesis with a mechanism: surface landmarks versus landmarks spread through a volume (`:21-24`). Reviewed as a plan.
6. Predictions written as checkable assertions: yes, but P7 is missing from the analysis list (`:58,110`).
7. What an ineffective method would show: stated (`:63-66`). Repeat noise in the counts is zero, so the null that actually operates is the 10% band. Reviewed as a plan.

**False successes**
8. Null arms: `exact` and the floor control. The floor reproduces `exact` bit for bit unless fast Gauss runs, so it does not show how much a small perturbation moves the result (O1). Reviewed as a plan.
9. Degenerate solutions: falling back to exact and premature convergence are caught; a failed negative control is not (B3). Reviewed as a plan.
10. Positive control: B (P6); **what happens if it fails is undefined (B5)**.

**Measurement validity**
11. Metrics and units stated: yes (`:26-32`).
12. Main metric valid for "schnell": **no (B1)**.
13. Metric definitions match the code: **no (B2)**.
14. Detection limit: the 10% paired cost threshold is acceptable as a plan; 1e-3 is a decision choice (B7). Reviewed as a plan.
15. Reference integrity: sha256 plus a size check (driver `:104-105`, runner `:73-81`).
16. Null probes: run inside the plan, with kill gates. Reviewed as a plan.
17. Technical test: `exact` and one Nystroem variant ran at 10^5 (task `:34-36`). The box variants were not timed; the 600 s timeout leaves about 5× margin. Reviewed as a plan.

**Comparison design**
18. Paired arms: yes.
19. Replication: data only, not the landmark draw (B4). Reviewed as a plan.
20. Decision rule complete: **no (B5, B6)**.
21. Multiple comparisons: a deterministic all-six-pairs rule with no p-values; fine.
22. Stated limits complete: add O1, O6 and B4.

**Execution**
23. Timeouts and stop rules in code: driver `:125,139-142`. Reviewed as a plan.
24. Bounded plan with enforced order: driver `:33-50`.
25. Resume and provenance: records are append-only and the reference is hashed; gaps listed in O5.
26. Freeze: the exact command is at `layer-a.md:73-76`; see O7 and O8.

Further blocking findings:
- **B5.** If B is not equivalent, that is only a failed prediction (`layer-b.md:60-61,71-72`). The rule would then label a variant "worse than B" even when B fails the same way, and could label the cheapest equivalent variant "worse". This is plausible here, because every technical-test run hit the 150-iteration cap (task `:34-36`). Fix: decide what a non-equivalent B means (that pair gives no decision, or stop), or rank the variants that are equivalent in all pairs by cost.
- **B6.** Failed cells have no rule (`layer-b.md:35-41,110`; driver `:129-133`). Fix: a failed variant counts as not equivalent; a failed or unevaluated B, repeat or control cell makes that pair give no decision, or triggers the kill.
- **B7.** The cost proxy, the 1e-3 threshold and the kill gates need the operator's confirmation, recorded verbatim (global AGENTS.md, "Questions, goal, scope").

Optional findings:
- **O1.** There is no small-perturbation arm, for example B with an error limit of 1e-5. Without it, a non-equivalent result cannot be separated into approximation error versus the capped EM's sensitivity to any change in its path.
- **O2.** Add P7 to the analysis.
- **O3.** Justify `box/mixed` in Layer B.
- **O4.** Take the timing noise as the maximum over the six repeat pairs.
- **O5.** The audit should assert one commit, one `runner_sha256` and `points` = 10^5 across all 78 records. On resume the driver checks none of these, has no lock, and does not check the reference size.
- **O6.** State that the doubling cap is 512 in `partial` and 1024 in `full` (`EStep.cpp:1648-1658`).
- **O7.** The task's verification command passes `--positions`, which the driver does not accept, so it exits with argparse code 2 (task `:29`).
- **O8.** `manifest.txt` was untracked at session start; commit it before the start check.

### 4. Verdict
**REVISE**
- Blocking: B1–B7. All are text or rule fixes; B4 can optionally be fixed with a one-line runner change instead.
- Optional: O1–O8.

Not checked: whether farthest-point sampling ignores the seed beyond what its documentation says, and the skill's own item numbering.