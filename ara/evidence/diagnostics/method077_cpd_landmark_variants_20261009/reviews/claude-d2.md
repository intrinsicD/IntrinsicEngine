**Verdict: PASS** on digest `2a83e7093709d23acbfb1adff947544244b543d3e242fb771acb72f8a915074d` (taken as given; I could not recompute it).

**Rule compliance:** read-only. Outside the repository I opened only the diff you supplied; I read everything else inside the repository.

### 1. Last round's blocking findings
- **N1, one-sided outcomes: resolved.**
  - The gate now requires both passes to have the same recorded outcome, and identical counts where both completed (driver `:241-244`).
  - The text now matches: a variant failing both passes keeps the pair and counts as not equivalent; a one-sided outcome removes the pair (`layer-b.md:53-56`).
  - Tests: one failed pass undecides its pair (`Test.CpdLandmarkVariantsAnalysis.py:113-119`); failing both passes keeps the pair and counts as not equivalent (`:136`).
- **N2, perturbation engagement: resolved.** The gate now requires `nystrom_iterations > 0` (driver `:260-261`; `layer-b.md:63-65`). The test that sets the perturbation control to zero Nystroem iterations removes `full/2` (`:113-119`).
- **C1, configuration justification: resolved.** See `layer-b.md:29-37`.
  - I verified the β/λ source: `Bayesian()` in `Test.CoherentPointDriftBayesian.cpp:86-92` sets λ=2.0 and β=1.0.
  - LowRank is forced above 8192 source points and G is the same in every arm.
  - The 150-iteration cap is now stated as a fixed iteration budget for all arms.
- **C2, distribution of the box landmarks: resolved.**
  - New test at `Test.CoherentPointDriftAccelerated.cpp:973-1026`: 100 independent draws (a fresh evaluator per seed), for both random and Halton. It checks the landmark shares in A only, B only and A∩B against the volume shares.
  - I checked the logic: the boxes match `PaddedBox` (pad √σ²), and one 48-landmark attempt fits under the cost cap.
  - With these boxes A∩B holds about 0.21 of the union. Without the ½ rejection in the overlap it would be about 0.35, so the 0.03 tolerance catches the mutation the log reports. The test checks shares per region only, not uniformity within a box; that part is a linear map of uniform draws.

### 2. New problems in this revision (none blocking)
- **Stale text:** `layer-a.md:73` still says the exact cell "deletes any old reference". The driver now moves it to `runs/stale/` (`:175-176,199-200`), as `layer-a.md:81-82` already says.
- **Analysis versioning:** `--analyze` run in the middle of the run (on partial records) becomes `analysis.json`, and the analysis of the complete run would be `analysis.<k>.json` (`:322-327`). Optional: refuse analysis until every planned cell has a record, or name which file is the confirmatory one. The referee audit recomputes from raw records anyway.
- **Checked, no problem found:**
  - provenance and pinning refusal in the analysis, with a test (`:224-228`; test `:129`);
  - `taskset` now required (`:149-152`);
  - `preserve()` never overwrites (`:75-84`);
  - the slowness branch now has a noise condition (`:277`);
  - predictions return "not evaluable" without evidence (`:292-297,321`; test `:121`);
  - the runner's new `kernel_rank`/`kernel_error` fields cannot be non-finite;
  - the negative-control description is now accurate (`layer-a.md:59-62`);
  - the three-draws replication wording is corrected.

### 3. Checklist items marked failed or gap last round
- **1** (configuration justification): met (C1).
- **3** (whether each control actually engages): met; the perturbation control is now gated.
- **12** (degenerate solutions caught): met; the perturbation control can no longer pass without engaging.
- **14 †** (missing measurement never a pass): met (N1, N2, and predictions without evidence are no longer counted as passed).
- **16 †** (controls): met as a plan.
- **22** (distribution of the randomized landmarks): met (C2).
- **23 †** (end-to-end run at final configuration): improved. One box arm ran at 10^5 with the final configuration, pinned: 150 iterations, about 56 s (`layer-a.md:86-87`). Accepted as a plan.
- **15:** still an optional gap. The decision uses RMS, which can hide a local deviation, and `max_to_exact` is not in the analysis rows (driver `:278-280`).
- **25:** still an optional gap. The baseline profile is total times only, so it is not shown that a 10% win is reachable.

### 4. Verdict
**PASS**: no blocking findings; optional: `layer-a.md:73` wording, which analysis file counts, `max_to_exact` in the analysis rows, a profile of the baseline.

I did not run the tests. The mutation-check claims come from the task log. That logical CPUs 0-15 are one per core is still unverified.