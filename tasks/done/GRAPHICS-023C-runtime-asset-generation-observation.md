# GRAPHICS-023C — Runtime asset-generation observation bridge

## Goal
- Add the smallest runtime-owned bridge that observes a renderable entity's CPU asset source against `Graphics::GpuAssetCache::GetView()` and classifies whether the associated `GpuSceneSlot` would need a later rebind.

## Non-goals
- No runtime polling loop outside existing render extraction.
- No GPU geometry upload, `GpuWorld` geometry rebinding, material rebinding, texture decode, shader reload, or file watching.
- No renderer-owned dependency on ECS, runtime, live `AssetService`, or `AssetEventBus`.
- No change to `GpuAssetCache` state machine semantics.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: completed implementation slice.
- Owner: `runtime` composition/extraction layer, using lower-layer `graphics/assets`, `graphics/renderer`, and CPU ECS asset-source data.
- `GRAPHICS-023A` added `GpuSceneSlot::SourceAsset` and `GpuSceneSlot::LastSeenAssetGeneration`.
- `GRAPHICS-023B` added deterministic `GpuSceneSlot::EvaluateSourceAssetRebind()` and `NeedsSourceAssetRebind()` helpers.
- This slice should wire those helpers to runtime-owned observation only; actual upload/rebind work remains a later child task.

## Required changes
- Extend `src/runtime/Runtime.RenderExtraction.cppm` with a CPU-testable helper that accepts a `GpuSceneSlot`, an `Assets::AssetId`, and an optional `Graphics::GpuAssetCache` pointer, then returns deterministic observation status/diagnostics.
- Let `RenderExtractionCache::ExtractAndSubmit()` optionally receive a `GpuAssetCache` pointer and observe `ECS::Components::AssetInstance::Source` for renderable entities.
- Keep any mutation limited to source-asset identity tracking needed for later decisions; do not mark a newer generation as last-seen until a future rebind slice exists.
- Extend `RuntimeRenderExtractionStats` with counters for source observations, unavailable cache/views, up-to-date observations, and rebind-required observations.
- Update focused runtime graphics CPU integration tests for pending/no-cache and ready-generation observations.
- Update architecture/backlog docs to reflect this landed bridge and remaining non-goals.

## Tests
- Add/update `integration;runtime;graphics` CPU coverage only.
- No GPU/Vulkan tests are required.

## Docs
- Keep `docs/architecture/graphics.md` and `src/graphics/renderer/README.md` factual about runtime-owned observation and non-goals.
- Keep `tasks/backlog/rendering/README.md` synchronized with this active child task.

## Acceptance criteria
- Runtime render extraction can observe `AssetInstance::Source` against a supplied `GpuAssetCache` without exposing ECS/runtime dependencies to graphics layers.
- Missing cache or missing GPU view is a deterministic non-rebind observation.
- A ready cache view whose generation is newer than the `GpuSceneSlot` last-seen generation is reported as rebind-required without updating last-seen generation.
- Up-to-date observations are reported when the sidecar already records the observed generation.
- Tests prove the observation counters and helper behavior on the CPU/default path.

## Completion
- Completed: 2026-05-07.
- Commit reference: local commit for GRAPHICS-023C implementation slice.
- Notes:
  - Landed only the runtime-owned asset-generation observation bridge and focused CPU integration coverage.
  - GPU geometry rebinding, material rebinding, upload orchestration, file watching, shader reload, and texture decode remain outside this slice.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeGraphicsCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeRenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
git --no-pager diff --check
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing ECS, runtime, live `AssetService`, or `AssetEventBus` into `graphics/assets` or `graphics/renderer`.
- Changing `GpuAssetCache` upload/reload state machine behavior.
- Adding GPU/Vulkan-only test requirements.
