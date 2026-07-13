---
id: BUG-073
theme: G
depends_on: []
---
# BUG-073 — Object-space normal bake may be sampled before its GPU write completes

## Status
- Completed 2026-07-13 at `CPUContracted`.
- Commit: this task-retirement commit; production correction `fdeb0a6b` is
  already an ancestor of `main`, and this closure adds the deterministic
  regression, public accounting contract, operational evidence audit, and
  synchronized documentation.

## Goal
- Gate object-space-normal-bake texture readiness on actual GPU completion, so
  the baked texture cannot be bound/sampled before the frame that records the
  bake has retired.

## Non-goals
- No change to the bake algorithm or texture format.
- No change to the GpuAssetCache completion model for other producers beyond
  what is needed to make this producer correct.

## Context
- Owner/layer: `runtime`/`graphics`; `src/runtime/Runtime.ObjectSpaceNormalBakeService.cpp`,
  `src/runtime/Runtime.ObjectSpaceNormalBakeGpuQueue.cpp`, GpuAssetCache.
- The original callback supplied `issueFrame + 1` even though the bake is
  recorded into the current graphics frame. `GpuAssetCache` promotes
  frame-number completions exactly when `currentFrame >= PendingReadyFrame`;
  its frames-in-flight argument affects deferred destruction, not this
  promotion, and `ObjectSpaceNormalBakeBinding` has no bypass around the ready
  texture view.
- Vulkan advances the global frame number only after a successful frame
  submission and waits before reusing a per-frame fence slot. Therefore the
  supported conservative retirement point is `issueFrame + FramesInFlight`.
  Production correction `fdeb0a6b` installed that rule; this closure makes it
  a named contract and pins the complete queue/cache/binding transition for
  three frames in flight.

## Required changes
- [x] Confirm the device's cross-frame completion model for graphics-recorded
      GPU writes (fence/timeline vs bare CPU frame counter).
- [x] Gate readiness on
      `issueFrame + FramesInFlight` (or a real fence/timeline token) so the bake
      cannot be sampled before its frame retires.

## Tests
- [x] Contract/logic test on the ready-frame accounting for frames-in-flight > 1
      (readiness cannot precede issue + framesInFlight).
- [x] Run the feasible opt-in `gpu;vulkan` readback smoke proving the graphics
      bake content matches the CPU contract. This is supporting graphics-bake
      evidence, not runtime-ready-frame `Operational` evidence; the production
      runtime plan provider and end-to-end smoke remain owned by `RUNTIME-129`.
- [x] Default CPU gate.

## Docs
- [x] Document the readiness-gating rule for GPU-produced textures in the bake
      subsystem README.

## Acceptance criteria
- [x] Baked texture readiness cannot precede the conservative retirement point
      of the recording frame under the supported frames-in-flight range.
- [x] Default CPU gate and strict structural checks pass; the executed
      `gpu;vulkan` run is recorded without using it to claim runtime
      `Operational` maturity.

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

- `RuntimeObjectSpaceNormalBakeGpuQueue.ReadyFrameAccountingWaitsForEveryInFlightFrameBeforeBinding`
  computes frame 43 from issue frame 40 with three frames in flight, records a
  real queue submission through `JobService`, proves frames 41 and 42 remain
  `GpuUploading` and unbound, then proves frame 43 promotes and binds the
  texture. The complete four-test GPU-queue suite passed 100 repetitions under
  the `ci` preset's ASan/UBSan configuration.
- `RuntimeEngineLayering.ObjectSpaceNormalBakeServiceKeepsGpuQueueCompositionOutOfEngine`
  passed in the opt-in integration executable and pins the service's use of the
  named ready-frame contract while rejecting the former `+ 1u` expression.
- On the local NVIDIA GeForce RTX 3050, the existing
  `ObjectSpaceNormalTextureBakeGpuSmoke.VulkanBakeMatchesCpuContractAtSelectedTexels`
  executed the Vulkan bake/readback and passed in 8.0 seconds with the
  repository's narrow `lsan.supp`. Without that suppression its assertions
  still passed, but LeakSanitizer exited nonzero on the already-tracked Vulkan
  shutdown allocations owned by `BUG-083`.
- The complete default CPU-supported gate, strict layering/task/state-link/test
  layout/root/PR checks, documentation links, and generated-inventory checks
  passed on this retirement commit.

## Completion

- Completed: 2026-07-13. Maturity: `CPUContracted`.
- Outcome: object-space normal bake readiness is stamped at
  `issueFrame + FramesInFlight`, cache promotion is blocked before that frame,
  and material binding remains blocked until promotion supplies a ready view.
- The hardware-backed graphics bake is operational, but the exact runtime
  ready-frame seam cannot reach `Operational` until `RUNTIME-129` supplies the
  production Vulkan plan provider and its end-to-end smoke.

## Forbidden changes
- Do not claim `Operational` from CPU-only coverage.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Closed at `CPUContracted`; `Operational` is explicitly owned by `RUNTIME-129`.
