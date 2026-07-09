---
id: RUNTIME-164
theme: F
depends_on: [RUNTIME-163]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-164 — Extract asset residency service out of Engine

## Status
- Retired on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.AssetResidencyService` owns `Graphics::GpuAssetCache`, the
  cache's asset-event listener token, `AssetModelTextureHandoff`, and
  `AssetModelSceneHandoff`.
- `Runtime.Engine` keeps lifecycle/frame ordering and the public
  `GetAssetService()` / `GetGpuAssetCache()` compatibility facades while
  delegating cache bootstrap, listener wiring, handoff setup, maintenance
  ticks, and teardown ordering through the service.
- Verification passed: focused runtime asset/import/render/object-space normal
  bake and Engine-layering coverage passed 93/93, `IntrinsicTests` built, and
  the default CPU-supported CTest gate passed 3646/3646. Strict task/docs/
  layering/test-layout checks and the clean-workshop automated rows passed;
  warning-mode root/task-state findings were pre-existing and unchanged.
- PR/commit: pending.

## Goal
- Move GPU asset residency owner state out of `Runtime.Engine.cppm` /
  `Runtime.Engine.cpp` into a focused runtime service: `GpuAssetCache`
  construction, asset-event subscription, fallback texture bootstrap,
  model-texture/model-scene handoff ownership, pending material binding
  resolution, and teardown ordering. Keep `Engine` as the concrete lifecycle
  and frame-order owner and preserve the existing asset/GPU cache public
  facades.

## Non-goals
- Changing `Assets::AssetService` ownership, asset import state-machine
  behavior, promoted model/texture payload semantics, GPU cache behavior, model
  texture upload semantics, model-scene ECS materialization semantics, or object
  space normal bake scheduling semantics.
- Moving render-extraction cache ownership, renderer frame hooks, geometry
  residency retire queues, or `Runtime.Engine:FrameLoop` into this service.
- Completing any Vulkan-only residency, GPU readback, or production
  object-space normal bake plan-provider work.

## Context
- Owner: `runtime`; this is runtime composition glue around lower-layer asset
  payload authority (`src/assets`) and graphics residency (`GpuAssetCache`).
- After `RUNTIME-163`, `Runtime.Engine.cppm` still imports and stores
  `Graphics::GpuAssetCache`, `AssetModelTextureHandoff`,
  `AssetModelSceneHandoff`, and an `AssetEventBus` listener token directly.
- `Runtime.Engine.cpp` still constructs the GPU asset cache, registers its
  asset-event listener, bootstraps the fallback texture, constructs model
  texture/model-scene handoffs, resets those handoffs during shutdown, and ticks
  the cache plus pending material-binding resolution through frame-loop hooks.
- This follows the `RUNTIME-146` through `RUNTIME-163` decomposition pattern:
  `Engine` remains the frame skeleton and subsystem wiring root, while
  subsystem-local ownership and compatibility facades move behind
  runtime-owned modules.

## Required changes
- [x] Add `Extrinsic.Runtime.AssetResidencyService` under `src/runtime/`
  owning `GpuAssetCache`, its `AssetEventBus` listener token, the model texture
  handoff, and the model scene handoff.
- [x] Move GPU cache construction, fallback texture initialization,
  asset-event subscription, model handoff construction, per-frame
  `AssetService`/cache/handoff tick, scene borrower teardown, and asset teardown
  behind the service.
- [x] Update `Runtime.Engine.cppm` to store the service instead of raw GPU
  asset cache/listener/model-handoff members and to stop directly importing the
  model handoff and asset-event modules.
- [x] Update `Runtime.Engine.cpp` so initialization, shutdown, render contract
  hook construction, maintenance hooks, object-space normal bake service wiring,
  asset-import dependencies, and `GetGpuAssetCache()` delegate through the
  service while preserving lifecycle order.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Add runtime source-contract coverage proving GPU asset cache listener,
  fallback bootstrap, model handoff ownership, and pending binding resolution no
  longer live in `Runtime.Engine.cppm` / `Runtime.Engine.cpp`.
- [x] Preserve asset import, model texture/model scene handoff, render
  extraction, object-space normal bake, and Engine layering coverage.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document
  `Extrinsic.Runtime.AssetResidencyService` and revise Engine asset-residency
  current-state wording.
- [x] Update `tasks/backlog/runtime/README.md` with the factual decomposition
  state.
- [x] Update `tasks/backlog/README.md` if the Theme F Engine-decomposition
  summary changes.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and retirement.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` no longer stores `m_GpuAssetCache`,
  `m_GpuAssetCacheListener`, `m_AssetModelTextureHandoff`, or
  `m_AssetModelSceneHandoff`, and it no longer directly imports
  `Extrinsic.Runtime.AssetModelSceneHandoff`,
  `Extrinsic.Runtime.AssetModelTextureHandoff`, or
  `Extrinsic.Asset.EventBus`.
- [x] `Runtime.Engine.cpp` no longer constructs `Graphics::GpuAssetCache`,
  subscribes the GPU cache to `AssetEventBus`, constructs model handoffs, or
  resets those handoffs/cache directly during shutdown.
- [x] Existing behavior remains unchanged: the asset service still ticks before
  GPU cache retirement, fallback texture bootstrap still runs after cache
  construction, model handoffs still observe promoted asset events, pending
  material bindings still resolve during maintenance, and GPU leases still
  unwind before renderer/device teardown.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from
  pre-existing warning-mode root/task-state findings if unchanged by this
  slice.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering|RuntimeAssetModelTextureHandoff|RuntimeAssetModelSceneHandoff|AssetImportFormatCoverage|RuntimeRenderExtraction|ObjectSpaceNormalBake' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/check_task_state_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Changing asset-service state-machine behavior, asset import diagnostics,
  model texture/model scene materialization semantics, GPU cache allocation or
  retirement algorithms, fallback texture bytes/descriptors, or object-space
  normal bake queue policy.
- Changing renderer contract ordering, `IRenderFrameHooks`, or
  `IAssetFrameHooks`.
- Moving render-extraction or scene-document ownership into this service.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational` for this cleanup slice.
- This slice closes at `Operational` when live Engine initialization,
  maintenance, and shutdown delegate asset-residency owner state to the new
  runtime service and focused asset/import/layering coverage plus the default
  CPU gate pass.
