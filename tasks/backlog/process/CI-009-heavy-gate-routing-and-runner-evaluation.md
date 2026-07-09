---
id: CI-009
theme: H
depends_on:
  - CI-003
  - CI-004
  - CI-005
  - CI-006
  - CI-007
  - CI-008
---
# CI-009 — Route heavy gates by lifecycle and evaluate runner scaling

## Goal
- Use the measured results of `CI-003..008` to choose a required-check topology
  that gives quick feedback on every PR update while running full CPU,
  sanitizer, Vulkan, and benchmark confidence at the appropriate review/merge
  lifecycle, with a costed decision on larger or ephemeral runners.

## Non-goals
- No weakening of merge requirements or backend-specific correctness.
- No build-tree/BMI artifact reuse between jobs.
- No path filter that leaves a required GitHub check permanently pending.
- No runner purchase/plan change without measured cost and queue data.

## Context
- Owner: GitHub Actions trigger topology, required-check contracts, benchmark
  routing, and runner policy.
- `CI-003` observed independent cold compile-heavy jobs on every PR update:
  ~25m PR-fast, ~17m16s UBSan, ~23m03s Vulkan, and ~7m51s benchmark smoke.
  Compilation represented 80–97% of non-doc wall time.
- `ci-bench-smoke` builds `IntrinsicBenchmarkSmoke` and then
  `IntrinsicBenchmarks` even when changed paths cannot affect benchmark code.
  `ci-vulkan` likewise pays a full promoted build for about 60 selected tests.
- Stale-run cancellation (`CI-003`), gate aggregates (`CI-004`), touched-scope
  PR feedback (`CI-005`), sanitizer rationalization (`CI-006`), ccache
  (`CI-007`), and grouped tests (`CI-008`) must land before hardware or trigger
  changes are judged. Otherwise runner scaling can mask avoidable software
  duplication.
- Candidate lifecycle points are PR synchronize/open, ready-for-review,
  `merge_group`, and default-branch push. Required checks must report a terminal
  success/failure/skipped-via-success status for every applicable event.

## Required changes
- [ ] Inventory branch-protection/merge-queue required check names and model
      how each candidate workflow resolves for draft, ready, synchronize,
      reopened, `merge_group`, and default-branch events.
- [ ] Keep docs/structural and touched-scope quick feedback on every PR update.
- [ ] Evaluate running full CPU/ASan/UBSan/Vulkan/benchmark gates on
      ready-for-review and `merge_group` while retaining an explicit manual/full
      path for pre-review diagnosis.
- [ ] Make benchmark smoke path-aware only if an always-reporting check wrapper
      preserves required-check semantics; benchmark/method/toolchain/CMake
      changes must always run it.
- [ ] Evaluate co-locating compatible build+test phases in one job, but do not
      transfer configured C++ module build trees between runners.
- [ ] Compare the optimized standard runner with at least one available larger
      hosted or ephemeral runner using the same commit/preset, reporting queue
      time, build/test/total median/p95, billed minutes, estimated cost per
      merged PR, and operational maintenance burden.
- [ ] Record a decision: retain standard runners, adopt a larger runner for
      named gates, or defer with a quantified threshold that reopens the choice.
- [ ] Add workflow-contract regressions for event/required-check resolution.

## Tests
- [ ] Exercise workflow logic fixtures for every event/lifecycle combination
      and changed-path class.
- [ ] Prove full merge confidence still includes CPU, ASan, UBSan, Vulkan, and
      benchmark checks with the intended skip semantics.
- [ ] Compare at least five standard-runner samples and, when available, five
      larger-runner samples using `CI-003` telemetry.
- [ ] Validate benchmark manifests/results and required-check contracts.

## Docs
- [ ] Document the final quick-feedback versus merge-confidence topology,
      manual rerun path, and required-check names.
- [ ] Record the runner cost/performance decision and assumptions in the CI
      performance report.
- [ ] Update benchmark CI policy for any lifecycle/path routing change.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [ ] Every PR update gets a fast terminal feedback signal, and every merge
      candidate receives the full required confidence set.
- [ ] Required checks cannot remain pending because a path/event filter omitted
      the workflow.
- [ ] Heavy-gate timing and runner decisions use post-`CI-003..008` comparable
      data rather than the unoptimized topology alone.
- [ ] Any larger-runner adoption includes a quantified cost/latency benefit and
      rollback path.

## Verification
```bash
python3 tests/regression/tooling/Test.WorkflowRouting.py
python3 tools/repo/check_pr_contract.py --root .
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Removing a full gate merely because it is slow.
- Using path filters directly on required workflows without an always-reporting
  result.
- Comparing runner sizes with different presets, commits, or test selectors.
- Uploading/restoring CMake build directories or BMIs as cross-job artifacts.
