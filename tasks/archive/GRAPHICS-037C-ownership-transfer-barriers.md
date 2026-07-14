# GRAPHICS-037C — Queue-family ownership-transfer barrier synthesis

## Goal
- Synthesize queue-family ownership-transfer barriers for `EXCLUSIVE` cross-queue
  resources through the existing Sync2 barrier compiler (`GRAPHICS-037` decisions 4/5),
  with the transient-`CONCURRENT` / retained-`EXCLUSIVE` resource policy, tested under
  the null RHI against the barrier compiler.

## Non-goals
- No Vulkan recording (that is `GRAPHICS-037D`).
- No second barrier path — the Sync2 compiler gains a queue-family field, not a parallel path.

## Context
- Owner layer: `graphics/framegraph` (barrier synthesis on top of the `GRAPHICS-022`
  Sync2 barrier compiler).
- Depends on `GRAPHICS-037B` (cross-queue edges) for the wait sequencing.
- Decision 4: per-frame transient cross-queue resources default to `CONCURRENT` (no
  ownership transfer); retained cross-queue resources use `EXCLUSIVE` with explicit
  queue-family ownership transfer; single-queue resources stay `EXCLUSIVE` with no transfer.
- Decision 5: for `EXCLUSIVE` cross-queue resources emit a release barrier
  (`srcQueueFamily = producer`, `dstQueueFamily = consumer`) on the producing queue
  paired with an acquire barrier on the consuming queue, both through the Sync2 compiler;
  the decision-3 timeline wait sequences acquire after release; `CONCURRENT` needs only
  the semaphore edge + normal layout/access barriers.

## Status
- Commit reference: this task-landing commit.
- Landed 2026-06-04 at maturity `CPUContracted`. The framegraph Sync2 barrier
  packet surface now carries `QueueSharingMode`, queue-family ownership-transfer
  fields, and before/after-pass packet timing. Live cross-queue transient resources
  compile as `Concurrent` with timeline edges plus normal barriers only; live
  cross-queue imported resources stay `Exclusive` and emit release-after-producer /
  acquire-before-consumer transfer pairs with `CrossQueueOwnershipTransferCount`
  diagnostics. Real Vulkan multi-queue recording remains owned by `GRAPHICS-037D`.

## Required changes
- [x] Add a queue-family field to the Sync2 barrier descriptors (no second path).
- [x] Classify cross-queue resources transient→`CONCURRENT` / retained→`EXCLUSIVE`.
- [x] Emit release/acquire ownership-transfer barrier pairs for `EXCLUSIVE` cross-queue
      resources, sequenced after the `GRAPHICS-037B` timeline wait.
- [x] Surface `CrossQueueOwnershipTransferCount` on the diagnostics result (decision 8).
- [x] `contract;graphics` tests asserting barrier composition vs the existing compiler.

## Tests
- [x] `contract;graphics` — release/acquire pairing for `EXCLUSIVE` resources;
      `CONCURRENT` resources get only the semaphore edge; ownership-transfer counter
      accounting; interaction with the existing single-queue barrier output unchanged.
- [x] CPU gate green.

## Docs
- [x] Document the resource ownership policy in `src/graphics/framegraph/README.md`
      and `docs/architecture/graphics.md`.

## Acceptance criteria
- [x] Ownership-transfer barriers compose with the Sync2 compiler with no second path.
- [x] The transient/retained sharing-mode split is enforced and CPU-tested.
- [x] No new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding a second barrier path instead of extending the Sync2 compiler.
- Bypassing the Sync2 barrier compiler.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the ownership-transfer barrier contract.
- `Operational` owned by `GRAPHICS-037D` (real multi-queue submission smoke).
