# BUG-007 — GpuAssetCache uploads remain pending in default CPU gate

## Goal
- Restore the default CPU-supported test gate for the `GpuAssetCache`/texture-binding failures by making successful mock-backed uploads promote from `GpuUploading` to `Ready` and descriptor-backed sampler creation/diagnostics behave deterministically.

## Non-goals
- No renderer framegraph executor fix; the originally observed `RendererFrameLifecycle` ASan heap-buffer-overflow was another symptom of the same stale transfer-queue virtual slot and now passes after this fix.
- No GPU/Vulkan backend changes.
- No cache eviction policy changes.
- No texture asset bridge or runtime fallback bootstrap work (`RUNTIME-070`).

## Context
- Status: done.
- Owner/agent: GitHub Copilot on `main`.
- Owner/layer: `src/graphics/rhi` ABI-safe transfer queue surface; preserves `graphics/rhi -> core`.
- Symptom: default CPU CTest failed reproducibly on `GpuAssetCache.BufferRequestTransitionsToReadyAfterTick`, `GpuAssetCache.TextureRequestPopulatesBindlessIndex`, hot-reload/retire tests, descriptor sampler diagnostics, `GraphicsMaterialSystem.ResolvesReadyTextureAssetBindingsToBindlessMaterialParams`, and renderer frame-lifecycle tests.
- Repro command:
  ```bash
  ctest --test-dir build/ci --output-on-failure -R 'GpuAssetCache\\.BufferRequestTransitionsToReadyAfterTick|GpuAssetCache\\.TextureRequestCanOwnSamplerFromDescriptor' --timeout 60
  ```
- Expected behavior: successful mock-backed buffer/texture uploads complete when the injected transfer queue reports completion; descriptor-backed texture uploads call `SamplerManager::GetOrCreate`, retain the sampler lease, and surface sampler-create failures through existing diagnostics.
- Root cause: `UploadTextureFullChain(...)` had been inserted before the pre-existing `IsComplete(...)` / `CollectCompleted()` virtual slots in `RHI::ITransferQueue`. Some module consumers still called the historical `IsComplete` slot while rebuilt test doubles exposed `UploadTextureFullChain` there, so completed uploads were interpreted as pending. The fix appends the newer virtual after the original upload/poll/collect slots and documents the slot-order invariant.
- Impact: the default CPU-supported gate reported multiple graphics asset failures, blocking reliable Theme A verification.

## Required changes
- [x] Identify why `GpuAssetCache::Tick()` leaves completed mock uploads in `GpuUploading` and fix the smallest owning implementation or test seam.
- [x] Restore descriptor-backed sampler creation and sampler failure diagnostics in the mock-backed `GpuAssetCache` path.
- [x] Keep live ECS/runtime/asset-service dependencies out of graphics assets.

## Tests
- [x] Existing `unit;graphics` `GpuAssetCache.*` tests pass.
- [x] Existing `unit;graphics` material-system texture binding test passes if it shares the same root cause.
- [x] Add or update regression coverage only if the existing failing unit tests do not fully cover the fix. Existing tests already covered the slot mismatch and now pass without new cases.

## Docs
- [x] Update `tasks/backlog/bugs/index.md` to list this active issue while in progress and move it to verified/closed when retired.
- [x] Update `src/graphics/assets/README.md` only if the public cache contract changes. No cache contract change was made.

## Acceptance criteria
- [x] Focused CTest repro passes.
- [x] `CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicGraphicsAssetsUnitTests IntrinsicGraphicsRendererCpuUnitTests` passes.
- [x] No new layering violations.
- [x] The default CPU CTest gate no longer reports the BUG-007 `GpuAssetCache`/material-system failures.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicGraphicsAssetsUnitTests IntrinsicGraphicsRendererCpuUnitTests
ctest --test-dir build/ci --output-on-failure -R 'GpuAssetCache|GraphicsMaterialSystem\\.ResolvesReadyTextureAssetBindingsToBindlessMaterialParams' --timeout 60
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Shipping a fix without a regression test when existing tests do not cover it.
- Adding fallback paths that hide failed uploads as success.
- Adding live `AssetService`, ECS, runtime, or Vulkan dependencies to `src/graphics/assets`.
- Marking failing tests `slow`, `gpu`, `vulkan`, or `flaky-quarantine` to bypass the default gate.

## Next verification step
- Continue with the next unblocked Theme A slice (`RUNTIME-070` or `GRAPHICS-030B`) now that the default CPU gate is green again.

## Completion
- Completed: 2026-05-13.
- Commit references: pending local commit/PR.
- Verification run in this session:
  - `cmake --preset ci`
  - `CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicGraphicsAssetsUnitTests IntrinsicGraphicsRendererCpuUnitTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'GpuAssetCache|GraphicsMaterialSystem\\.ResolvesReadyTextureAssetBindingsToBindlessMaterialParams' --timeout 60`
  - `CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  - `python3 tools/agents/check_task_policy.py --root . --strict`
  - `python3 tools/docs/check_doc_links.py --root .`
  - `python3 tools/repo/check_layering.py --root src --strict`
  - `python3 tools/repo/check_test_layout.py --root . --strict`
  - `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
