# BUG-009 — Minimal recipe surface pass executes when culling output is unavailable

## Goal
- Gate the `RecordMinimalDebugSurfacePass` prerequisite check on the live culling-output flag so the minimal recipe soft-skips its `DrawIndexedIndirectCount` when the culling pipeline failed to build, matching the contract that `MinimalDebugSurfacePassContract.MissingCullingBucketSkipsUnavailableAndIncrementsCounter` asserts.

## Non-goals
- No change to the culling system's allocation order (`AllocateGpuBuffers` continues to allocate bucket buffers before the cull pipeline is created).
- No change to the GRAPHICS-033C operational-status flip; this bug exists on baseline `c4c63ca` as well.
- No new diagnostic counters.

## Context
- Status: done.
- Owner/agent: Claude (session `claude/inspect-engine-state-k3aik`).
- Owner/layer: `src/graphics/renderer` (executor prerequisite check only).
- Symptom: `MinimalDebugSurfacePassContract.MissingCullingBucketSkipsUnavailableAndIncrementsCounter` fails — the surface pass records a draw and the missing-prerequisite counter stays at zero even though `CullingSystem::Initialize` returned `false`.
  ```
  Test.MinimalDebugSurfacePass.cpp:319: Failure
    stats.MinimalSurfacePassExecutions  Which is: 1  vs  0u
  Test.MinimalDebugSurfacePass.cpp:320: Failure
    Expected: stats.MinimalRecipeMissingPrerequisiteCount >= 1u
      actual: 0 vs 1
  ```
  Reproduces on baseline `c4c63ca` (no work-in-progress slice required).
- Repro command:
  ```bash
  build/ci/bin/IntrinsicGraphicsContractCpuTests --gtest_filter='MinimalDebugSurfacePassContract.MissingCullingBucketSkipsUnavailableAndIncrementsCounter'
  ```
- Root cause: `RecordMinimalDebugSurfacePass` derives `bucketReady` from the bucket buffer handles only. `CullingSystem::Initialize` allocates the bucket buffers via `BufferManager::Create` *before* it tries to create the cull compute pipeline. When the pipeline creation fails (as the test triggers with `MockDevice::FailPipelineCreateCall = 1`), `Initialize` returns `false` and the renderer sets `m_CullingOutputAvailable = false`, but `Initialize` does not call `Shutdown`, so the bucket buffer handles remain valid. The surface pass's prerequisite check sees valid buffer handles and proceeds with a draw against indirect-arg/count buffers that the culling dispatch never writes — the indirect-count would be garbage on a real device, and the renderer would report `MinimalSurfacePassExecutions == 1` and `MinimalRecipeMissingPrerequisiteCount == 0`, contradicting the documented soft-skip taxonomy.
- Impact: regression of the GRAPHICS-032/033C minimal-recipe soft-skip contract under any device failure that leaves the cull pipeline non-functional; misleading counter values for diagnostics consumers.

## Required changes
- [x] Add `m_CullingOutputAvailable` to the `bucketReady` precondition in `RecordMinimalDebugSurfacePass`. The flag is already maintained by `Initialize`/`RebuildOperationalResources` and consumed by `RecordCullingPass`/`RecordDepthPrepass`, so re-using it keeps the contract consistent across all recipe routes.
- [x] Document the rationale inline so a future reader sees that the bucket buffers can exist even when the culling pipeline doesn't.

## Tests
- [x] `MinimalDebugSurfacePassContract.MissingCullingBucketSkipsUnavailableAndIncrementsCounter` — passes locally after the fix; all 6 surface-pass contract tests in the file remain green.
- [x] `MinimalDebugSurfacePassContract.RendererRoutesAndIncrementsExecutionsCounter` — confirms the green path still records (operational MockDevice + valid culling output).
- [x] `MinimalDebugSurfacePassContract.RecordsCommandsAfterOperationalRebuildTransition` — confirms the rebuild path still re-enables the minimal recipe.

## Docs
- [x] No external-doc update needed. The inline comment in `Graphics.Renderer.cpp` documents the dependency on `m_CullingOutputAvailable`; the README documentation around `RebuildOperationalResources()` already lists culling output as one of the things rebuilt by the seam.

## Acceptance criteria
- [x] The repro command above passes.
- [x] No regression in `MinimalDebugSurfacePassContract.*` (6/6 passing locally).
- [x] No regression in the broader graphics contract suite (96/97 passing; the remaining failure is the separate `MinimalDebugPresentPassContract.AcceptanceFrameRecordsBothPassesAndBarrierSequence` bug tracked in BUG-010).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
build/ci/bin/IntrinsicGraphicsContractCpuTests \
  --gtest_filter='MinimalDebugSurfacePassContract.*'
build/ci/bin/IntrinsicGraphicsContractCpuTests
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Reordering `CullingSystem::AllocateGpuBuffers` / pipeline-create so that pipeline failure also releases bucket buffers (would entangle the bucket allocator with pipeline lifecycle and affects unrelated code paths).
- Widening `IDevice` or `IRenderer` to expose `m_CullingOutputAvailable` to other layers.

## Completion
- Completed: 2026-05-14.
- Commit reference: pending (this session's commit on `claude/inspect-engine-state-k3aik`).
- Verification:
  - All commands above ran clean except the pre-existing BUG-010 failure.
