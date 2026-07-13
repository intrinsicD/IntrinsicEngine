---
id: BUG-074
theme: G
depends_on: []
---
# BUG-074 — Orphaned GpuAssetCache slot causes per-entity bake retry livelock

## Status
- Completed 2026-07-13 at `CPUContracted`.
- Commit: this task-retirement commit; production cleanup `4347617b`, causal
  retry regressions `c05ab5ce`, and generation-safe cache cleanup `80130b06`
  are already ancestors of `main`.

## Goal
- Ensure a failed object-space-normal-bake submission releases the pending
  GpuAssetCache slot it opened, so re-scheduling the same asset cannot livelock.

## Non-goals
- No change to the bake success path or the cache completion model.

## Context
- Owner/layer: `runtime`/`graphics`;
  `src/runtime/Runtime.ObjectSpaceNormalBakeGpuQueue.cpp`, GpuAssetCache
  submission.
- Before the fix, a `RecordObjectSpaceNormalTextureBake` or ready-frame
  publication failure dropped the ticket without pushing it to `m_InFlight`,
  after `BeginGpuProducedTexture` had already opened a pending cache slot.
  The slot was then orphaned — never drained or bound.
- Consequence: a future re-`Schedule` of the same asset hit
  `BeginGpuProducedTexture` → `ResourceBusy` → `CacheRejected` →
  `ShouldRetrySubmission == true` → requeued and retried every frame forever,
  because the orphaned slot never completes. Error-path only.
- The original asset-ID-only cleanup could also race asset destruction or a
  replacement generation. The final API therefore requires the generation
  returned by `BeginGpuProducedTexture`, fails closed when it does not match,
  and never creates a missing slot.

## Required changes
- [x] On any bake submission failure that occurs after `BeginGpuProducedTexture`
      has opened a slot, release/cancel that pending cache slot before dropping
      the ticket.
- [x] Verify the retry classifier does not treat a self-inflicted orphaned-slot
      `ResourceBusy` as an infinitely retryable condition.

## Tests
- [x] A forced bake-record/mark-ready failure releases the cache slot and a
      subsequent schedule of the same asset succeeds (no unbounded retry).
- [x] Default CPU gate.

## Docs
- [x] Note the failure-path slot-release contract in the bake subsystem README.

## Acceptance criteria
- [x] A bake submission failure leaves no orphaned pending cache slot.
- [x] Re-scheduling a previously-failed asset does not livelock.
- [x] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ObjectSpaceNormalBake' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Closure verification on 2026-07-13:

- `RecordFailureReleasesCacheSlotAndExplicitRetrySucceeds` forces the command
  recorder to reject a real submission after the cache generation opens. The
  queue records the failure, retires that exact generation, leaves the entry
  in `Failed`, and accepts and completes an explicit second schedule for the
  same asset without an intervening cache tick.
- `ReadyFrameFailureMarksSlotFailedAndExplicitRetrySucceeds` injects a
  ready-frame publication failure without mutating the cache, then proves the
  same exact-generation cleanup and explicit retry path.
- `GpuProducedTextureFailureRequiresMatchingGeneration` proves a stale cleanup
  cannot retire a newer pending generation, while
  `GpuProducedTextureFailureDoesNotRecreateDestroyedSlot` proves late cleanup
  cannot recreate an asset removed in the meantime.
- The six-test causal queue/cache selection passed 100 repetitions (6/6 CTest
  entries, 28.95 seconds), and the broader object-space-bake/generated-texture
  selection passed 49/49.
- `IntrinsicTests` built at exact integrated code head `11f56bf5`. The complete
  default CPU-supported gate passed 3,692/3,692 after all ten selected task
  slices and the final ARCH-006 review corrections were integrated; this
  retirement commit changes task documentation only.
- Strict layering, task policy/state links, test layout, documentation links,
  generated inventory/session-brief freshness, and diff checks passed. Root
  hygiene completed in warning mode with only the pre-existing `ara/`
  allowlist mismatch.

## Completion

- Completed: 2026-07-13. Maturity: `CPUContracted`.
- Outcome: every post-open queue failure retires only the cache generation it
  owns. Its pending resources enter the retire queue and the asset can be
  explicitly scheduled again immediately, while a stale callback cannot
  destroy a replacement or recreate a removed slot.
- The production Vulkan plan provider and end-to-end runtime bake seam remain
  owned by `RUNTIME-129`; CPU mocks and cache contracts do not establish
  `Operational` maturity.

## Forbidden changes
- Do not mask the livelock by capping retries without releasing the slot.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Closed at `CPUContracted`; `Operational` remains explicitly owned by
  `RUNTIME-129`.
