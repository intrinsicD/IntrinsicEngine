---
id: BUG-073
theme: G
depends_on: []
---
# BUG-073 — Object-space normal bake may be sampled before its GPU write completes

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
- `ObjectSpaceNormalBakeService::SetDependencies` supplies
  `ReadyFrame = device->GetGlobalFrameNumber() + 1`
  (`Runtime.ObjectSpaceNormalBakeService.cpp:19-25`). The bake is recorded into
  frame F's graphics command buffer
  (`Runtime.ObjectSpaceNormalBakeGpuQueue.cpp:218`), but GpuAssetCache gates
  readiness purely on the CPU frame counter (`CompletionKind::FrameNumber` →
  `currentFrame >= PendingReadyFrame`) with no fence/transfer-token backing.
- Suspected failure: with frames-in-flight > 1, frame F+1's
  `DrainCompletedTransfers` → `TryBindReadyObjectSpaceNormalBake` → `GetView`
  can bind/sample the texture before the GPU finishes frame F's bake — a
  read-before-write hazard. Safe only if frames-in-flight == 1 or a `WaitIdle`
  sits between frames. This is new code introduced by merge `76528e6`; the exact
  cross-frame fence model was not fully traced and must be confirmed.

## Required changes
- [ ] Confirm the device's cross-frame completion model for graphics-recorded
      GPU writes (fence/timeline vs bare CPU frame counter).
- [ ] If the CPU-frame gate is insufficient, gate readiness on
      `issueFrame + FramesInFlight` (or a real fence/timeline token) so the bake
      cannot be sampled before its frame retires.

## Tests
- [ ] Contract/logic test on the ready-frame accounting for frames-in-flight > 1
      (readiness cannot precede issue + framesInFlight).
- [ ] Where feasible, an opt-in `gpu;vulkan` readback smoke proving the baked
      texture content is correct (non-stale) when sampled.
- [ ] Default CPU gate.

## Docs
- [ ] Document the readiness-gating rule for GPU-produced textures in the bake
      subsystem README.

## Acceptance criteria
- [ ] Baked texture readiness cannot precede GPU completion of the recording
      frame under the supported frames-in-flight range.
- [ ] Default CPU gate and strict structural checks pass; any `Operational`
      claim cites the `gpu;vulkan` run that executed.

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
- Do not claim `Operational` from CPU-only coverage.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `Operational` on Vulkan-capable hosts (readback smoke); `CPUContracted`
  for the ready-frame accounting elsewhere.
