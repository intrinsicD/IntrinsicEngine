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
  - CI-010
  - CI-011
  - BUILD-004
  - BUG-114
---
# CI-009 — Route heavy gates by lifecycle and evaluate runner scaling

## Status
- Completed on 2026-07-18 at `Operational` for the checked-in GitHub Actions
  topology; owner: Codex; branch: `main`.
- Retirement commit: this commit records the audited closure; no PR.
- Implementation commits `941fa2d3`, `5e7e4e91`, `8e3973c9`, `2a946b50`,
  `f6a8bdde`, `c91b362c`, and `191bec28` add and harden the lifecycle
  topology. Dependency fix `502422ce` repairs the Release SLO contract, and
  `26ad0e7a` records its hosted evidence before this retirement.
- The stable contexts remain candidates rather than externally required
  checks: the repository has no branch protection, ruleset, merge queue,
  auto-merge policy, or registered runner. No live `merge_group` execution
  exists because no queue is configured; executable routing fixtures own that
  event contract.
- Retain `ubuntu-24.04`. No comparable larger runner was available, so no A/B
  is claimed. The documented queue/total-time, candidate-cost,
  adoption-benefit, maintenance, and rollback criteria govern any future
  experiment.

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
- `BUG-114` retired before this task resumed; the dependency remains in front
  matter as resolved provenance. Five sequential attempt-1 hosted
  `ci-release` runs passed at unchanged SHA
  `502422ce7559a757354bce105ddebd2a0966c996`:
  [`29633396211`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29633396211),
  [`29633689288`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29633689288),
  [`29633934571`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29633934571),
  [`29634185888`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29634185888),
  and
  [`29634432796`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29634432796).
  The failed diagnostic pilot `29631970411` is excluded.
- Owner: GitHub Actions trigger topology, required-check contracts, benchmark
  routing, and runner policy.
- `CI-003` observed independent cold compile-heavy jobs on every PR update:
  ~25m PR-fast, ~17m16s UBSan, ~23m03s Vulkan, and ~7m51s benchmark smoke.
  Compilation represented 80–97% of non-doc wall time.
- Before this task, the now-retired `ci-bench-smoke` workflow built
  `IntrinsicBenchmarkSmoke` and then `IntrinsicBenchmarks` even when changed
  paths could not affect benchmark code. `ci-vulkan` likewise paid a full
  promoted build for about 60 selected tests.
- Stale-run cancellation (`CI-003`), gate aggregates (`CI-004`), touched-scope
  PR feedback (`CI-005`), sanitizer rationalization (`CI-006`), ccache
  (`CI-007`), grouped tests (`CI-008`), CPU coverage (`CI-010`), the measured
  slow cohort (`CI-011`), and source-complete hotspot evidence (`BUILD-004`)
  must land before hardware or trigger changes are judged. Otherwise runner
  scaling can mask avoidable software duplication or route an incomplete gate.
- Candidate lifecycle points are PR synchronize/open, ready-for-review,
  `merge_group`, and default-branch push. Required checks must report a terminal
  success/failure/skipped-via-success status for every applicable event.
- Intake on 2026-07-18 found no repository branch-protection rule, ruleset,
  merge queue, auto-merge policy, or repository runner registration to mutate.
  The repository patch therefore defines candidate stable check names and
  lifecycle behavior only; enabling external protection remains an explicit
  owner decision.
- Right-sizing: use workflow-local result jobs plus the existing
  `touched_scope.py` `needs_cpp` verdict for Release benchmark/SLO routing.
  Do not introduce a second dispatcher or a benchmark-specific path classifier.
  Reconsider that decision only if an independently required gate needs a
  different path vocabulary.
- Candidate stable contexts are `docs-validation`, `pr-fast`,
  `ci-linux-clang`, `ci-vulkan`, and `ci-release`. They are not currently
  protected externally. The unsanitized grouped full-CPU `main` push therefore
  remains uncancelled; sanitizer work is not duplicated on that event.
- No comparable larger hosted or ephemeral runner is registered. Retain
  `ubuntu-24.04` and reopen the experiment when a five-sample standard Release
  population has queue p95 above five minutes or total p95 above 20 minutes
  and a candidate can be budgeted at no more than $1 incremental cost per
  merge candidate. Adoption then requires at least 20% lower median, 25% lower
  p95, no queue-p95 regression above 60 seconds, and an explicit rollback.
- Five standard Release samples remain retirement evidence, not preparation
  evidence. Manual Release runs are uncancelled and the documented protocol
  requires five separate runs at one unchanged ref/SHA, waiting for each run
  to complete before dispatching the next.

## Slice plan
- Slice A — inventory required checks and implement always-reporting lifecycle
  wrappers, keeping cheap structural validation and touched-scope feedback at
  the front of every PR update.
- Slice B — place the complete CPU source-coverage lane and an explicit
  optimized Release build/smoke at default-branch, merge-candidate, scheduled,
  or manual lifecycle points without making normal correctness builds pay their
  instrumentation/optimization cost.
- Slice C — run the standard-versus-larger runner A/B only when a comparable
  runner is available. Runner unavailability does not block routing: record a
  quantified queue/latency/cost threshold that reopens the experiment.

## Required changes
- [x] Inventory branch-protection/merge-queue required check names and model
      how each candidate workflow resolves for draft, ready, synchronize,
      reopened, `merge_group`, and default-branch events.
- [x] Keep docs/structural and touched-scope quick feedback on every PR update.
- [x] Order cheap structural checks before expensive compilation wherever job
      dependencies permit, while preserving an always-reporting required-check
      result.
- [x] Evaluate running full CPU/ASan/UBSan/Vulkan/benchmark gates on
      ready-for-review and `merge_group` while retaining an explicit manual/full
      path for pre-review diagnosis.
- [x] Place `CI-010`'s complete coverage job in a default-branch/scheduled or
      merge-candidate lane and retain its artifacts without instrumenting every
      ordinary build matrix entry.
- [x] Add an explicit optimized Release build and small smoke at the chosen
      merge/default-branch lifecycle; performance/SLO evidence must consume that
      identity rather than a sanitizer or `-O0` correctness tree.
- [x] Make benchmark smoke path-aware only if an always-reporting check wrapper
      preserves required-check semantics; benchmark/method/toolchain/CMake
      changes must always run it.
- [x] Evaluate co-locating compatible build+test phases in one job, but do not
      transfer configured C++ module build trees between runners.
- [x] Confirm no comparable larger hosted or ephemeral runner is available; do
      not fabricate an A/B. Record standard-runner queue/build/test/total
      median/p95 and billing evidence, and defer comparison behind the
      quantified reopen, cost, adoption, and rollback criteria.
- [x] Record a decision: retain standard runners, adopt a larger runner for
      named gates, or defer with a quantified threshold that reopens the choice.
- [x] Preserve `BUILD-004`'s established after-correctness/independent-reporting
      contract when rerouting lifecycle events; this task does not redefine the
      analyzer or its ordering policy.
- [x] Add workflow-contract regressions for event/required-check resolution.

## Tests
- [x] Exercise workflow logic fixtures for every event/lifecycle combination
      and changed-path class.
- [x] Prove full merge confidence still includes CPU, ASan, UBSan, Vulkan, and
      benchmark checks plus the chosen coverage and optimized-Release lanes
      with the intended skip semantics.
- [x] Compare at least five standard-runner samples and, when available, five
      larger-runner samples using `CI-003` telemetry.
- [x] Validate benchmark manifests/results and required-check contracts.

## Docs
- [x] Document the final quick-feedback versus merge-confidence topology,
      manual rerun path, and required-check names in
      `docs/benchmarking/ci-policy.md`.
- [x] Record the runner cost/performance decision and assumptions in the CI
      performance report.
- [x] Update benchmark CI policy for any lifecycle/path routing change.
- [x] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [x] Every PR update gets a fast terminal feedback signal, and every merge
      candidate receives the full required confidence set selected by the
      documented lifecycle model.
- [x] Required checks cannot remain pending because a path/event filter omitted
      the workflow.
- [x] Coverage and optimized Release confidence run in a named default-branch,
      scheduled, or merge-candidate lane and are not accidentally produced by a
      debug/sanitizer tree.
- [x] Heavy-gate timing and runner decisions use post-`CI-003..008`, `CI-010`,
      `CI-011`, and `BUILD-004` comparable data rather than the unoptimized
      topology alone.
- [x] No larger runner was adopted; any future adoption requires the documented
      cost/latency benefit and rollback path.
- [x] If no comparable larger runner is available, routing still closes with a
      documented threshold for reopening the runner experiment.

## Evidence
- Workflow routing, concurrency, timing, touched-scope, and sanitizer-identity
  regressions pass 11/11, 19/19, 20/20, 27/27, and 8/8 respectively. The
  routing fixtures execute pull-request, draft, `merge_group`, manual, route
  failure, required-job failure, and valid skip contracts directly from the
  checked-in workflow scripts.
- Five sequential attempt-1 `ci-release` runs pass at unchanged SHA
  `502422ce7559a757354bce105ddebd2a0966c996`: `29633396211`,
  `29633689288`, `29633934571`, `29634185888`, and `29634432796`.
  Every optimized job and stable result wrapper passes.
- Queue median/p95 is 2/3 seconds; optimized-job-wall median/p95 is 445/478
  seconds; measured-phase median/p95 is 417.150/439.345 seconds. These are
  below the 300-second queue and 1,200-second total-time reopen thresholds.
- Every retained JUnit passes both SLO cases without a skip and contains all
  four `SLO_METRIC` records. All 22 benchmark results and the timing result
  from every sample pass strict validation. Exact phase timings, job IDs,
  image/cache identity, artifact IDs/digests, and metric values are recorded
  in
  [`docs/benchmarking/ci-policy.md`](../../docs/benchmarking/ci-policy.md#optimized-release-and-runner-evidence).
- The GitHub billing API reports zero billable milliseconds for this
  public-repository hosted population. That is the API billing response, not
  a reusable compute-cost estimate; the policy therefore retains operational
  job wall and requires a future candidate to fit the explicit incremental
  cost cap.
- Current GitHub repository settings report no branch protection, ruleset,
  registered runner, merge queue, or auto-merge policy. Consequently, no
  larger-runner comparison and no live `merge_group` run are claimed.
  Executable fixtures prove the checked-in merge-group contract.
- Default-branch full CPU run
  [`29633359676`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29633359676)
  passes at the same SHA, including strict task policy, configure determinism,
  the complete CPU cohort, layering, routing reconciliation, and compile
  hotspot validation.

## Verification
```bash
python3 tests/regression/tooling/Test.WorkflowRouting.py
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
python3 tests/regression/tooling/Test.CiTiming.py
python3 tests/regression/tooling/Test.TouchedScope.py
python3 tests/regression/tooling/Test.SanitizerPresets.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-release/benchmark --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --root . --check
```

## Forbidden changes
- Removing a full gate merely because it is slow.
- Using path filters directly on required workflows without an always-reporting
  result.
- Comparing runner sizes with different presets, commits, or test selectors.
- Uploading/restoring CMake build directories or BMIs as cross-job artifacts.
- Blocking the lifecycle/routing correction indefinitely on unavailable runner
  capacity.
