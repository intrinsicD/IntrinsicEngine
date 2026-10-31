# ARCH-022: where the CI time goes

Rule R6 of the architecture target ([Engine and feature modules](../architecture/engine-and-feature-modules.md)) sets
"CI median under 15 min". The [R6 baseline](2026-10-30-arch-022-r6-baseline.md) measured 41.1 min. This report breaks
that number down over the same cohort, using the step timings GitHub records. It is an analysis of logged data: nothing
was run. Tracked in [`ARCH-022`](../../tasks/active/ARCH-022-engine-and-feature-modules-target.md) (Q46, step 1).

## Summary

- **On a push to `main`, the C++ CI is one job.**
  - `ci-linux-clang` runs only its `full-cpu` job. Every other job in that workflow is skipped on pushes: sanitizers,
    test-selection parity, the evidence profiles and the required-check aggregator.
  - `ci-docs` also runs on every push, in parallel: 23 runs in the window, median 4.2 min, max 6.5. It builds no C++
    and is not part of the R6 number.
  - `pr-fast`, `ci-vulkan` and `ci-release` run only on pull requests, merge groups or by hand. `nightly-deep` and
    `ci-source-coverage` run on a schedule or by hand.
  - Work here lands by direct push.
- **The build is 84 % of the job.**
  - `Build required CPU cohort` (`cmake --build --preset ci --target IntrinsicCpuTests`; 2,676 Ninja steps in the
    two logs that were fetched) has a median of 32.1 min.
  - The test suite has a median of 4.4 min (12 %).
  - Everything else together is 1.5 min in the median: checkout, dependencies, configure and the policy checks.
- **Every push builds cold.**
  - `ci-linux-clang.yml` and `.github/actions/setup-build/action.yml` are byte-identical at all 20 head commits. Their
    only `actions/cache` is the vcpkg binary-package cache, and they never install ccache.
  - `cmake/Dependencies.cmake` enables ccache whenever it finds it. Neither fetched log contains a ccache line, which
    confirms that ccache was not installed.
  - The repository already has a measured, module-safe ccache policy (CI-007, described in
    [CI policy](../benchmarking/ci-policy.md)). It lives in `pr-fast.yml`, which never runs on a push.
- **The job itself takes 38.2 min in the median** (21.3 to 42.0). The run takes 41.1 min because 8 of the 20 runs first
  waited 6.5 to 34.2 min for the previous run on `main`.
  - The workflow's concurrency group cancels in-progress runs only for pull requests and merge groups.
  - In every waiting case, the job was created within 2 s of the end of the run it waited for. Twice, a newer push
    replaced a run that was still pending, and GitHub cancelled the pending one.
  - There was no wait for a runner: job creation to start was under 0.2 min in every run.
- **The build splits into two groups:** 8 runs between 17.6 and 27.1 min, 12 runs between 30.4 and 35.2 min.
  - Two logs were fetched: the shortest build (17.6 min) and the longest (35.2 min). Both show the same runner image
    (`ubuntu-24.04`, version 20261025.353.1) and 2,676 build steps. The other 18 logs were not checked.
  - Neither log records the CPU model, so the cause is unknown. Different hosted-runner hardware is a hypothesis.

## Step times of the `full-cpu` job (minutes, 20 runs)

| Step | Median | Min | Max | Share of summed medians |
|---|---|---|---|---|
| Build required CPU cohort | 32.1 | 17.6 | 35.2 | 84 % |
| Run full CPU test suite | 4.4 | 2.4 | 4.6 | 12 % |
| Set up build dependencies | 0.5 | 0.4 | 2.0 | 1 % |
| Validate backend configure determinism | 0.4 | 0.3 | 0.5 | 1 % |
| Checkout | 0.2 | 0.2 | 0.3 | 1 % |
| Configure (ci preset) | 0.2 | 0.1 | 0.2 | 0 % |
| all other steps (17) | under 0.1 each | | | 1 % together |

The step medians add up to 38.0 min, against a job median of 38.2 min.

## What this means for the 15-minute target

Without the waits, the job has to lose about 23 min. The build is the only step that large.

These levers are hypotheses; none was measured here:
- Use the existing CI-007 compiler-cache policy on pushes to `main`. Unchanged non-module translation units would
  hit the cache, but module interfaces (`.cppm`) would still compile, because ccache 4.9.1 passes them through. The
  policy itself requires new evidence before any other gate uses the cache.
- Build less per push, for example by routing pushes by touched scope.
- Use larger runners, which cost money.

Waiting for the previous run is a separate cost of pushing in quick succession. Cancelling an in-progress run on a new
push would remove it, but `main` would then lose its per-commit CI result.

The R6 baseline's CI number (41.1 min, run start to end) includes these waits. The job time (38.2 min) is the work CI
does. Which of the two the 15-minute target means is the operator's decision.

## Data and method

- **Cohort:** the R6 baseline's 20 green push runs of `ci-linux-clang` on `main`, created 2026-10-29 18:13 to
  2026-10-30 21:14 UTC, all first attempts. In the same window the workflow also ran once with failure and twice
  cancelled.
- **Raw data**, fetched 2026-10-31 and kept outside git in `tasks/evidence/ARCH-022/ci-breakdown/`:
  - per run, `gh api repos/{owner}/{repo}/actions/runs/<id>/jobs?per_page=100&filter=all`;
  - every run in the window;
  - two job logs, one from the shortest build and one from the longest;
  - the jobs responses of the two cancelled runs (0 jobs each, so they were cancelled while pending);
  - every `ci-docs` run in the window.
- **Script:** `analyze.py` in the same directory recomputes every number above except those read from the two logs
  (image version, step count) and the `ci-docs` durations. It asserts:
  - the cohort's shape (20 push runs, all first attempts);
  - that every non-`full-cpu` job was skipped;
  - that each run ends with its last job;
  - that no run queued for a runner;
  - that each waiting job started when exactly one earlier run ended;
  - that each superseded run had no jobs, so it was cancelled while pending.
- **Not covered:** pull-request, merge-group, scheduled and manual runs; the CPU model of the hosted runners; what any
  of the levers above would save.
