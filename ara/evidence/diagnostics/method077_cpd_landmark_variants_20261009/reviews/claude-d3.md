**Verdict: PASS** on digest `7bb6c4250b736f4f1f4e1e707546a13fbe856fe2b1815db434da8dcb22bc4e3f` (taken as given; I could not recompute it). Outside the repository I opened only the diff you supplied.

**Previous findings:** I had no blockers at `2a83e709…074d`. Three of my four optional items are resolved:
- **Wording:** `layer-a.md:73` now says the old reference is moved to `runs/stale/`.
- **Which analysis file counts:** resolved. The analysis now refuses unless every planned cell has a record (driver `:232-235`), so it can no longer run on partial data mid-run. A versioned file now only marks a later re-analysis.
- **`max_to_exact`:** it is now in the analysis rows (driver `:288`; `layer-b.md:133`).
- **Baseline profile (item 25):** not addressed; it remains optional.

**New checks, all verified:**
- **Provenance:** the analysis refuses unless every record matches HEAD's commit, the given runner's sha256, 10^5 points and `pinned: True`. A missing commit (`""`) or missing runner is refused too (driver `:224-231`, `:359-365`).
- **Missing cells:** the planned-cell check uses the driver's own `plan()`. The test fixture builds all 138 planned cells (6 × 23), so the other test cases still pass this check.
- **P5:** it is "not evaluable" when any of the nine variants lacks measurements in a decided pair (driver `:318-319`). With no decided pairs, `all_decided` returns "not evaluable" as before.
- **Tests:** the exact-failure test now marks the cell failed instead of deleting it, which is needed now that missing cells are refused (`:107`). The new refusal cases cover another commit, no commit, another runner and a missing first cell (`:136-139`). A P5 assertion is added (`:147`).

**New problems:** none blocking. One thing to know before the run:
- `--analyze` is now tied to HEAD and to the exact runner binary. Run it before committing anything, including `runs/` or the review files.
- Otherwise run it from a worktree at the frozen commit, with `--runner` pointing at this checkout's existing binary.
- A rebuild changes the sha, after which the analysis refuses. `layer-a.md:83` states "at the frozen commit"; this is an operational note, not a defect.

I did not run the tests; the mutation-check claims come from the task log. That logical CPUs 0-15 are one per core is still unverified.