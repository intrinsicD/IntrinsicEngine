---
id: BUG-074
theme: G
depends_on: []
---
# BUG-074 — Orphaned GpuAssetCache slot causes per-entity bake retry livelock

## Goal
- Ensure a failed object-space-normal-bake submission releases the pending
  GpuAssetCache slot it opened, so re-scheduling the same asset cannot livelock.

## Non-goals
- No change to the bake success path or the cache completion model.

## Context
- Owner/layer: `runtime`/`graphics`;
  `src/runtime/Runtime.ObjectSpaceNormalBakeGpuQueue.cpp`, GpuAssetCache
  submission.
- On `RecordObjectSpaceNormalTextureBake` failure
  (`Runtime.ObjectSpaceNormalBakeGpuQueue.cpp:221`) or `MarkReady` failure
  (`:234`), the ticket is dropped without being pushed to `m_InFlight`, but
  `BeginGpuProducedTexture` (Submission path) has already opened a pending cache
  slot for that asset. The slot is now orphaned — never drained or bound.
- Suspected consequence: a future re-`Schedule` of the same asset hits
  `BeginGpuProducedTexture` → `ResourceBusy` → `CacheRejected` →
  `ShouldRetrySubmission == true` → requeued and retried every frame forever,
  because the orphaned slot never completes. Error-path only.

## Required changes
- [ ] On any bake submission failure that occurs after `BeginGpuProducedTexture`
      has opened a slot, release/cancel that pending cache slot before dropping
      the ticket.
- [ ] Verify the retry classifier does not treat a self-inflicted orphaned-slot
      `ResourceBusy` as an infinitely retryable condition.

## Tests
- [ ] A forced bake-record/mark-ready failure releases the cache slot and a
      subsequent schedule of the same asset succeeds (no unbounded retry).
- [ ] Default CPU gate.

## Docs
- [ ] Note the failure-path slot-release contract in the bake subsystem README.

## Acceptance criteria
- [ ] A bake submission failure leaves no orphaned pending cache slot.
- [ ] Re-scheduling a previously-failed asset does not livelock.
- [ ] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ObjectSpaceNormalBake' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not mask the livelock by capping retries without releasing the slot.
- Mixing mechanical file moves with semantic refactors.
