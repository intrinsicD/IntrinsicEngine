# GRAPHICS-023B — GpuSceneSlot asset rebind decision

## Goal
- Add a deterministic CPU-only `GpuSceneSlot` helper that lets runtime residency code decide whether a ready GPU asset view generation requires rebinding without importing `Graphics.GpuAssetCache` into the renderer component module.

## Non-goals
- No runtime polling loop, ECS query changes, or sidecar rebind implementation.
- No file watching, shader compilation, material layout reload, texture decode, or asset ingest work.
- No dependency from `GpuSceneSlot` to `Graphics.GpuAssetCache`, live `AssetService`, ECS, runtime, or Vulkan.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: completed implementation slice.
- Owner: `graphics/renderer` value type plus CPU-only graphics unit coverage.
- `GRAPHICS-023A` added `GpuSceneSlot::SourceAsset` and `GpuSceneSlot::LastSeenAssetGeneration`.
- Runtime will later obtain `GpuAssetView::Generation` from `Graphics.GpuAssetCache`, but the renderer component should only compare asset identity and generation values passed in by runtime.

## Required changes
- [x] Add a small public decision enum/function to `src/graphics/renderer/Components/Graphics.Component.GpuSceneSlot.cppm` that classifies:
  - [x] no source asset bound,
  - [x] observed asset mismatch,
  - [x] pending/unknown generation,
  - [x] generation already current,
  - [x] generation advanced and rebind required.
- [x] Extend `tests/unit/graphics/Test.Graphics.GpuSceneSlot.cpp` with focused cases for each decision state.
- [x] Update `docs/architecture/graphics.md`, `src/graphics/renderer/README.md`, and `tasks/backlog/rendering/README.md` only to reflect the landed helper and task status.

## Tests
- [x] Add/update `unit;graphics` coverage only.
- [x] No GPU/Vulkan tests are required.

## Docs
- [x] Keep docs factual about the decision helper and non-goals.
- [x] Keep rendering backlog links synchronized.

## Acceptance criteria
- [x] Runtime callers can ask a `GpuSceneSlot` whether an observed `(AssetId, Generation)` requires rebind without importing `Graphics.GpuAssetCache` into `GpuSceneSlot`.
- [x] Default/dynamic slots do not request rebind.
- [x] Asset mismatch and unknown generation are deterministic non-rebind states.
- [x] Advanced generation is the only state that requests rebind.
- [x] Tests cover all decision states and existing `GpuSceneSlot` behavior remains intact.

## Completion
- Completed: 2026-05-07.
- Commit reference: local commit for GRAPHICS-023B implementation slice.
- Notes:
  - Landed only the `GpuSceneSlot` CPU decision helper and focused unit coverage.
  - Runtime polling/rebinding, `GpuAssetCache` integration, file watching, shader reload, and GPU/Vulkan behavior remain outside this slice.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsRendererCpuUnitTests
ctest --test-dir build/ci --output-on-failure -R 'GpuSceneSlot' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
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
- Importing ECS, runtime, `AssetService`, `AssetEventBus`, `Graphics.GpuAssetCache`, or Vulkan/backend modules into `GpuSceneSlot`.
- Adding GPU/Vulkan-only test requirements.
