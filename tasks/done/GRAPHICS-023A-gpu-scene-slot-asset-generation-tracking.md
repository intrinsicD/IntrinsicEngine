# GRAPHICS-023A — GpuSceneSlot asset generation tracking

## Goal
- Add the minimal graphics-owned `GpuSceneSlot` asset-binding metadata required by `GRAPHICS-028` so runtime residency code can later compare an entity sidecar against `GpuAssetView::Generation` during hot reload.

## Non-goals
- No runtime residency polling or rebinding implementation.
- No asset ingest, file watching, shader recompilation, material editor UI, or texture decode work.
- No Vulkan/GPU requirement in the default CPU gate.
- No ECS component carrying GPU handles or graphics asset state.

## Context
- Status: completed implementation slice.
- Owner: `graphics/renderer` value type plus CPU-only graphics unit coverage.
- `GRAPHICS-028` records that runtime-owned sidecars may store `Graphics::Components::GpuSceneSlot` and that a follow-up may add `Assets::AssetId SourceAsset{}` plus `std::uint64_t LastSeenAssetGeneration = 0` for hot-reload comparison.
- `graphics/renderer` may depend on asset IDs from `Asset.Registry`; it must not import live `AssetService`, ECS, runtime, or `graphics/assets` cache internals for this value type.

## Required changes
- Extend `src/graphics/renderer/Components/Graphics.Component.GpuSceneSlot.cppm` with `Assets::AssetId SourceAsset{}` and `std::uint64_t LastSeenAssetGeneration = 0`.
- Add small helper methods for source-asset presence, binding/updating generation, and clearing the binding.
- Add focused CPU graphics unit tests for default invalid state, binding generation metadata, preserving instance/geometry handles, named-buffer behavior, and clearing asset metadata.
- Register the new test source in `tests/CMakeLists.txt`.
- Update renderer architecture/readme docs only if needed to reflect the landed field names.

## Tests
- Add/update `unit;graphics` coverage only.
- No GPU/Vulkan tests are required.

## Docs
- Keep `docs/architecture/graphics.md` and `src/graphics/renderer/README.md` factual if the field names differ from the existing planning text.
- Keep rendering backlog links synchronized.

## Acceptance criteria
- `GpuSceneSlot` stores source asset identity and last-seen generation without adding any ECS dependency or live asset-service dependency.
- Default-constructed slots represent dynamic/non-asset geometry with an invalid `SourceAsset` and generation `0`.
- Tests prove binding, generation update, clearing, and existing handle/named-buffer helpers work together.
- Layering and test-layout checks pass.

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

## Completion
- Completed: 2026-05-07.
- Commit reference: local commit for GRAPHICS-023A implementation slice.
- Notes:
  - Landed only the `GpuSceneSlot` asset-generation value-type seam and focused CPU unit tests.
  - Runtime polling/rebinding, asset ingest, shader reload, and GPU/Vulkan behavior remain outside this slice.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing ECS, runtime, `AssetService`, `AssetEventBus`, or `Graphics.GpuAssetCache` into `GpuSceneSlot`.
- Adding GPU/Vulkan-only test requirements.
