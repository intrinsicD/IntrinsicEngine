# CI Backlog

Workflow and automation changes (`CI-` prefix): making the test/CI feedback loop
faster **without net coverage loss**. These tasks change GitHub workflows, CI
tooling, and the test harness registration in `tests/CMakeLists.txt`; the two
harness-restructuring members (CI-009, CI-010) touch test code but no engine
(`src/`) code.

Origin: the 2026-07-09 agentic test-effectiveness survey, which measured (via the
GitHub Actions API) that a PR push spawns 5 independent cold C++23-module builds
(~111 min of redundant compile), that `pr-fast` spends ~83% of its wall clock
building before any test runs, that `ci-vulkan` builds ~25 min to run a 6s
skip on GPU-less runners, and that coverage is already leaking silently
(14/30 nightlies dying at Configure; the `regression` label selecting zero
tests). Full evidence: the survey result captured on that branch.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence map.

## Tasks

Sequenced safe-wins-first; the two large structural members are gated behind the
audit keystone and an open flake fix.

- [CI-003 — Front-load structural validators and add cancel-in-progress concurrency](CI-003-frontload-validators-and-pr-concurrency.md)
  (small; independent). Layering/task-policy failures signal in <60s instead of
  ~25 min; stacked pushes stop piling up.
- [CI-004 — Warm compiler cache (ccache) for the non-authoritative PR build lanes](CI-004-ccache-warm-pr-build-lanes.md)
  (small; independent). Attacks the 83%-of-wall-clock build step; authoritative
  lanes stay cold as truth.
- [CI-005 — ci-vulkan device probe short-circuit and per-merge re-tier](CI-005-ci-vulkan-device-probe-and-retier.md)
  (small; depends on BUG-064). Reclaims ~25 min/push; GPU tests go from
  never-executed to nightly-on-hardware.
- [CI-006 — Parallelize the sanitizer ctest and run diff-relevant tests first](CI-006-parallel-sanitizer-and-earliest-failure-ordering.md)
  (small; independent). One flag restores ~5 min; earliest-failure ordering as a
  provable superset.
- [CI-007 — Gate-inventory and executed-coverage audit](CI-007-gate-inventory-and-coverage-audit.md)
  (medium; keystone, lands first). Makes any silent coverage loss a failing
  check; every re-tiering member cites it as mitigation.
- [CI-008 — Fail-closed touched-scope PR tier with a merge-queue full gate](CI-008-tiered-touched-scope-pr-tier-merge-queue.md)
  (large; depends on CI-007, BUG-063). Docs-only PRs in seconds; per-push compute
  ~5x lower; full gates paid once per PR at merge.
- [CI-009 — Scope-split the IntrinsicTests aggregate for the fast lane](CI-009-scope-split-intrinsictests-fast-aggregate.md)
  (small; independent). `pr-fast` builds only what its gate runs; compounds with
  CI-004.
- [CI-010 — Collapse per-case process spawns and reintroduce the shared engine fixture](CI-010-grouped-ctest-and-shared-engine-fixture.md)
  (large; depends on CI-007, BUG-063). Shrinks the test phase itself; the only
  member with a measured ≥3x precedent (CI-001).

## Convergence

- These tasks form the **test/CI feedback-speed** work stream.
- Dependency order: CI-007 first (coverage-detectability keystone). CI-003,
  CI-004, CI-006, CI-009 are independent and may land in any order. CI-005 waits
  on BUG-064. CI-008 and CI-010 wait on CI-007 and BUG-063.
- Cross-references into Theme G bugs: CI-005 ⇐ BUG-064 (ci-vulkan headless
  display); CI-008 and CI-010 ⇐ BUG-063 (streaming-import flake bounces queued
  merges / must not be misattributed to the fixture migration).

Forbidden across all members (inherited from CI-001): no merging the test
executables into one binary, no deleting assertions to hit a wall-clock target,
no `GTEST_SKIP` weakening, no new CTest label without updating `tests/README.md`
and `tests/CMakeLists.txt`, and no weakening/skipping/quarantining a gate to
reach green without a diagnosed `BUG-` task.

## Retired

Prior CI/test-speed work this stream builds on (history; narratives in the
[retirement log](../../done/RETIREMENT-LOG.md)):

- [CI-001 — Slim engine test runtime](../../done/CI-001-slim-engine-test-runtime.md)
  (done 2026-05-06): slow-labeling, the (since-deleted) shared per-process engine
  fixture, `TEST_P` consolidation, CTest `TIMEOUT` 30/120 + `-j`. CI-010 revives
  its shared-fixture pattern.
- [CI-002 — Touched-scope verification helper](../../done/CI-002-touched-scope-verification-helper.md)
  and [CI-002B — Document touched-scope helper](../../done/CI-002B-document-touched-scope-helper.md)
  (done 2026-05-13): added `tools/ci/touched_scope.py` as a local aid, explicitly
  not wired into Actions. CI-008 is the sanctioned follow-on that wires and
  hardens it.
- [INFRA-001 — vcpkg manifest mode](../../done/INFRA-001-vcpkg-manifest-mode.md)
  (done 2026-06-15): the vcpkg binary cache that already keeps warm CI configure
  at ~8s — the caching precedent CI-004 extends to the compiler.
