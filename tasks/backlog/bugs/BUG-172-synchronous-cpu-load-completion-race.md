---
id: BUG-172
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
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
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Hiding the original failure, weakening or quarantining the import assertion,
  increasing a yield count as a synchronization fix, or waiting for all scheduler work.
