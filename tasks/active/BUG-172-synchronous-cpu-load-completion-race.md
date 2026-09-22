---
id: BUG-172
theme: J
depends_on: []
workflow_schema: 1
template: micro
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive delegated repair; reviewed diff and executed CPU/sanitizer gates provide evidence."
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation]
contract_review: "The current catalog has no asset-load synchronization contract. This restores existing CPU completion and event-delivery behavior without changing module surfaces or layer ownership; revisit if a reusable contract or public seam is introduced."
---
# BUG-172 — Synchronous CPU completion can observe an unfinished or stale load transition

## Goal
- Coordinate scheduled and direct CPU completion so a successful import cannot
  return a stale InvalidState, and success includes publication of its Ready event.

## Non-goals
- No geometry segmentation changes, parser repair, timing-budget inflation,
  scheduler-wide waiting, or test quarantine.

## Context
- METHOD-040 full canonical CPU gate on 2026-09-06 failed once at
  `RuntimeAssetImportFormatCoverage.DirectObjImportPreservesAuthoredCornerNormals`:
  `ImportAssetFromPath` returned InvalidState with ingest diagnostic DecodeFailed.
  The raw receipt and full output are retained under
  `tasks/evidence/METHOD-040/commands/cpu-ctest-final.*`.
- A subsequent isolated `--repeat until-fail:100` passed 100/100; the retained
  log is `tasks/evidence/METHOD-040/experiments/import-repeat-100.log`.
  The original failure is real, but its exact cause is not yet demonstrated.
- Read-only review found existing race windows in
  `src/assets/Asset.Service.cpp` (`CompleteCpuLoadAndFlushEvent`): metadata can
  be read before a worker completes, followed by IsInFlight after archival,
  returning the stale initial error. A competing completion also gets only
  1,024 yields to finish, which is not a wall-time completion guarantee.
- `Asset.LoadPipeline.cpp` sets registry Ready before queuing its Ready event.
  Observing Ready and flushing immediately can therefore precede publication.
  The generic runtime DecodeFailed mapping does not prove OBJ parser failure.
  These paths predate METHOD-040 (present at af0a7fead).
- Fixture lifetime and deterministic parser failure are weaker hypotheses:
  the file is closed before import, lives through engine shutdown, and all
  100 isolated repetitions passed. Concurrent same-test fixture collision is
  possible but has not been observed in this run.

## Required changes
- [ ] Establish a deterministic winning-worker pause seam around decode claim
      and around Ready-state publication; reproduce stale failure or missing
      event delivery without relying on scheduler timing.
- [ ] Coordinate the entire completion transition, including event publication
      and archival, while preserving independence from unrelated scheduler jobs.
- [ ] Keep errors for true decode, cancellation, and stale-load failures explicit.

## Tests
- [ ] Scheduled completion winning over direct completion returns success and
      delivers exactly one Ready event after the winning transition completes.
- [ ] Pausing between registry Ready and event publication cannot produce early
      successful completion without that event.
- [ ] Existing unrelated-scheduler-work contract and authored-normal import pass.

## Docs
- [ ] Document the proven cause and synchronization ownership at implementation;
      retain the original failure and discriminating repro evidence.

## Acceptance criteria
- [ ] Deterministic before/after evidence establishes the fixed race.
- [ ] Exactly-once completion/event semantics and unrelated-job independence pass.
- [ ] Canonical CPU and isolated sanitizer gates pass without threshold changes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'AssetService.*CompleteCpuLoad|RuntimeAssetImportFormatCoverage.DirectObjImportPreservesAuthoredCornerNormals' --repeat until-fail:100 --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Hiding the original failure, weakening or quarantining the import assertion,
  increasing a yield count as a synchronization fix, or waiting for all scheduler work.

## Plan and reuse decision
- Use existing `AssetLoadPipeline` as synchronization owner; its existing mutex can cover registry transitions, event queue publication and archival. Registry/EventBus calls do not retain their locks or call back into the pipeline. Event flush remains outside pipeline synchronization.
- Add two constructor-injected pause hooks following `JobServiceTestHooks` to reproduce both windows before changing behavior. This is a present deterministic test seam, not an algorithm/config variant.
- Replace the service yield loop with a pipeline-owned completion decision; include cancellation and failure transition serialization. Preserve duplicate GPU-decode rejection and explicit stale/failed errors.
- Run focused regressions before/after, repeat the authored-normal import and completion cases, then canonical CPU and isolated ASan/UBSan gates. Claude and Codex review the final diff.

## Before-fix reproduction — 2026-09-22
Constructor hooks and the two new tests were built against the original completion
implementation. Both failed: `WaitsForClaimedDecode` returned early with an error;
`WaitsForReadyPublication` returned early with success and zero Ready events.
Command: `ctest --test-dir build/ci --output-on-failure -R '^AssetService.CompleteCpuLoadAndFlushEventWaitsFor' --timeout 60`.
The paused winner establishes each race window. The 100 ms controller observation
asserts non-return while paused; it is not a production completion budget. Raw
local output: `/tmp/bug172-before.log`; the checked-in tests reproduce the mechanism.
These results do not establish which mechanism caused the historical METHOD-040
failure; its original evidence is retained unchanged.

## Review decisions
- Claude Opus and the Codex subagent found no blocking production issues. The
  existing mutex is sufficient because payload loaders execute before pipeline
  enqueue; no new per-load completion records, condition variables, or locks are
  needed. The added hooks are the deterministic test seam required by this task.
- Strengthened Failed/canceled tests to assert `InvalidState`, added concurrent
  `MarkFailed` coverage, and clarified that GPU requests may complete their CPU
  stage while remaining not Ready. Tests call failure through a standalone
  pipeline's public API; the service's private pipeline accessor remains private.
- The final declaration comments describe synchronization and main-thread
  callback lifetime contracts; the source-documentation audit reports zero errors.
- Full pre-review CPU gate: 4,886 entries, zero failures, one existing GLFW/LSan
  skip. Seven completion/import tests passed 100 repetitions each. Final CPU and
  separate ASan/UBSan gates are rerun after strengthening the tests.

- Final Claude Sonnet and independent Codex review of the corrected shared pause
  helper and standalone failure test found no defects.
- Strict live routing initially rejected the seven new source cases absent from
  the pinned BUG-106 cohort. Added exactly those seven identities to its baseline,
  increased the affected runtime producer count 83 → 90 and total 226 → 233, and
  updated its README count. No old identity was removed. Routing self-tests pass
  19/19; live routing covers 4,790 logical CPU cases from 29 producers/364 sources.
  Individual/grouped registration equality passes for all 4,790 logical cases.

## Final verification — 2026-09-22
- Canonical `ci` configure and `IntrinsicTests` build: pass, Clang 23.
- Final CPU gate: 4,887 selected entries, zero failures; existing GLFW/LSan
  lifecycle test skipped.
- Final isolated ASan gate: 3,238 grouped-plan entries, zero failures.
- Final isolated UBSan gate: 3,238 grouped-plan entries, zero failures; existing
  GLFW/LSan lifecycle test skipped. Both sanitizer gates ran with `--parallel 1`
  and the unchanged exclusion-only CPU selector/timeouts.
- Nine selected completion, cancellation, failure and authored-normal import
  tests passed 100 repetitions each (900 executions) on the final test source.
  Selector: `AssetService.*CompleteCpuLoad|AssetService.MarkFailedWaitsForClaimedDecode|AssetService.DestroyWaitsForReadyPublication|RuntimeAssetImportFormatCoverage.DirectObjImportPreservesAuthoredCornerNormals`.
- Strict layering, test layout, task policy, task-state links, documentation links,
  root hygiene and diff formatting pass. Module inventory regenerated (no content
  change). Source-documentation audit: zero errors; declaration-comment findings
  reviewed as synchronization/lifetime contracts or unchanged existing comments.
- Routing self-tests pass 19/19; the exact new-case baseline and live CPU routing
  pass. Individual/grouped registries retain identical 4,790 logical cases.
- Claude Opus reviewed production synchronization; Claude Sonnet and the Codex
  subagent reviewed the corrected final tests with no remaining findings.
- No blocker or deferred implementation remains. The CPU lifecycle contract is
  repaired; no performance or operational GPU capability claim is made.
