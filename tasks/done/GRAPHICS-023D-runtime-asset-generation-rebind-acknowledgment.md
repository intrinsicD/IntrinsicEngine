# GRAPHICS-023D — Runtime asset-generation rebind acknowledgment

## Goal
- Add the smallest runtime-owned acknowledgment helper that closes the loop
  opened by `GRAPHICS-023C` by recording the observed asset generation onto a
  `Graphics::Components::GpuSceneSlot` after a runtime-confirmed rebind, so
  repeated observations of the same generation are not perpetually classified
  as `RebindRequired`.

## Non-goals
- No GPU geometry upload, `GpuWorld` geometry rebinding, material rebinding,
  texture decode, shader reload, or file watching.
- No automatic acknowledgment inside `RenderExtractionCache::ExtractAndSubmit`;
  acknowledgment must remain an explicit caller-driven step until a future
  upload/rebind slice exists.
- No renderer-owned dependency on ECS, runtime, live `AssetService`, or
  `AssetEventBus`.
- No change to `GpuAssetCache` state machine semantics.
- No change to `GpuSceneSlot::UpdateLastSeenAssetGeneration` semantics.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: completed implementation slice.
- Owner: `runtime` composition/extraction layer, using lower-layer
  `graphics/renderer` `GpuSceneSlot` value semantics from `GRAPHICS-023A`/`B`.
- Branch: `claude/setup-agentic-workflow-deiFw`.
- `GRAPHICS-023C` landed `Runtime::ObserveRenderableAssetGeneration` and the
  `Source*` observation counters but explicitly did not mark a newer generation
  as last-seen. As a result, observing a `RebindRequired` slot every frame
  returns `RebindRequired` forever — the closing acknowledgment step belongs
  here.
- This slice is one of the slice-plan continuations of parent task
  `tasks/backlog/rendering/GRAPHICS-023-shader-material-texture-hot-reload.md`.

## Required changes
- Extend `src/runtime/Runtime.RenderExtraction.cppm` with a CPU-testable helper
  `Runtime::AcknowledgeRenderableAssetRebind(slot, observation)` that:
  - Returns a deterministic result enum
    (`Acknowledged`, `SkippedNoObservedGeneration`, `SkippedAssetMismatch`,
    `SkippedNoSourceAsset`).
  - Updates `slot.LastSeenAssetGeneration` only when the observation reports a
    non-zero `ObservedGeneration` for the slot's current `SourceAsset`.
  - Does not introduce any new `Graphics`/RHI/ECS dependency edges and is
    implementable from existing imports.
- Extend `RuntimeRenderExtractionStats` with a single new counter
  `SourceAssetRebindAcknowledgedCount` so future callers (and tests) can
  account for explicit acknowledgments.
- Keep `RenderExtractionCache::ExtractAndSubmit` behavior bit-for-bit
  identical: it must continue to observe but not acknowledge until a future
  rebind slice wires the upload/rebind side.

## Tests
- Add focused `integration;runtime;graphics` CPU coverage in
  `tests/integration/runtime/Test.RuntimeRenderExtraction.cpp`:
  - Acknowledging a `RebindRequired` observation transitions the next
    observation to `UpToDate` and updates `LastSeenAssetGeneration`.
  - Acknowledging an `UpToDate` observation is a no-op on
    `LastSeenAssetGeneration` and reports `Acknowledged`.
  - Acknowledging when the observation reports `CacheUnavailable`/zero
    generation returns `SkippedNoObservedGeneration` and does not mutate.
  - Acknowledging an observation whose `SourceAsset` no longer matches the
    slot returns `SkippedAssetMismatch` and does not mutate.
- No GPU/Vulkan tests are required.

## Docs
- Update `docs/architecture/graphics.md` GRAPHICS-023C paragraph to mention the
  GRAPHICS-023D acknowledgment closure and remaining non-goals (file watching,
  shader reload, texture residency reload still deferred).
- Update `src/graphics/renderer/README.md` runtime-owned-observation bullet to
  cross-reference GRAPHICS-023D.
- Update `tasks/backlog/rendering/README.md` to list GRAPHICS-023D under the
  parent GRAPHICS-023 child series.

## Acceptance criteria
- A runtime caller holding a `GpuSceneSlot` and a
  `RuntimeRenderableAssetGenerationObservation` can call
  `AcknowledgeRenderableAssetRebind` to advance `LastSeenAssetGeneration` to
  the observed generation when the asset identity matches.
- Subsequent `ObserveRenderableAssetGeneration` of the same `(asset,
  generation)` reports `UpToDate` without mutation.
- Acknowledgment of a stale or asset-mismatched observation never mutates the
  slot and is reported as a typed skip result.
- `RuntimeRenderExtractionStats` exposes a new
  `SourceAssetRebindAcknowledgedCount` counter; the default
  `ExtractAndSubmit` path continues to leave it at zero.
- Tests prove the helper behavior on the CPU/default path.

## Completion
- Completed: 2026-05-08.
- Commit reference: 2c9b716 ("GRAPHICS-023D: acknowledge runtime asset rebinds").
- Notes:
  - Landed only the runtime-owned `AcknowledgeRenderableAssetRebind` helper,
    the `SourceAssetRebindAcknowledgedCount` counter, and focused CPU
    integration coverage in `tests/integration/runtime/Test.RuntimeRenderExtraction.cpp`.
  - `RenderExtractionCache::ExtractAndSubmit` continues to observe without
    acknowledging; uploads, file watching, shader reload, and texture
    residency reload remain outside this slice and stay scoped to the parent
    `GRAPHICS-023` series.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeRenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing ECS, runtime, live `AssetService`, or `AssetEventBus` into
  `graphics/assets` or `graphics/renderer`.
- Changing `GpuAssetCache` upload/reload state machine behavior.
- Auto-acknowledging inside `ExtractAndSubmit` before the rebind/upload slice
  actually performs the binding work.
- Adding GPU/Vulkan-only test requirements.
