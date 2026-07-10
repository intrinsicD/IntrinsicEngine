---
id: BUG-068
theme: G
depends_on: []
---
# BUG-068 — AssetModelSceneHandoff not rebound on active-world change (UAF)

## Goal
- Restore the active-scene rebind on world switch/teardown so the
  `AssetModelSceneHandoff` (and any other Initialize-bound scene borrower) never
  retains a reference to a destroyed `ECS::Scene::Registry`.

## Non-goals
- No change to the ARCH-010 WorldRegistry deferred-op model.
- No redesign of the asset→scene material/texture binding pipeline.
- No new multi-world features; only correct lifetime on the paths that already
  exist and are tested.

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.Engine.cpp`,
  `src/runtime/Runtime.AssetResidencyService.*`,
  `src/runtime/Runtime.AssetModelSceneHandoff.*`.
- Introduced by merge `76528e6`. The extraction dropped logic that pre-merge
  main performed in `RebindActiveSceneRuntimeState`.
- `AssetModelSceneHandoff` is created once in `Engine::Initialize`
  (`Runtime.Engine.cpp:546`, `InitializeSceneHandoffs`) and reset only in
  `Engine::Shutdown` (`:717`, `DestroySceneBorrowers`). Its `Impl` stores the
  scene and renderer **by reference** (`Runtime.AssetModelSceneHandoff.cpp:1934-1935`)
  and subscribes to the `AssetService` event bus for its lifetime (`:1953`).
- The refactored `Engine::ApplyWorldRegistryMaintenance` (`Runtime.Engine.cpp:399-414`)
  handles an active-world change with only `ClearSceneRuntimeState`,
  `RefreshActiveWorldScenePointer`, and `RebuildStableEntityLookupAfterSceneReplacement`.
  It never resets or recreates the handoff. Main reset the handoff **before** the
  old world registry was destroyed and recreated it bound to the new active scene.
- Failure scenario: a runtime module (or teardown) creates+activates a second
  world via WorldRegistry deferred ops; `m_WorldRegistry.ApplyMaintenance(...)`
  (`:403`) changes the active world and destroys the old world's
  `ECS::Scene::Registry`. The handoff's `Scene&` now dangles. The next frame,
  `AssetResidencyService::TickAssets` →
  `AssetModelSceneHandoff::ResolvePendingMaterialTextureBindings()`
  (`Runtime.AssetResidencyService.cpp:141-145`, driven from
  `AssetHooks::TickAssets`, `Runtime.Engine.FrameLoop.cppm:326-346`) →
  use-after-free. Any asset `Ready`/`Reloaded` event also re-enters the still
  subscribed listener and mutates the freed scene. Functionally, bindings for the
  newly-activated world are never wired.
- Reachability: the default single-world Sandbox may not switch worlds today, so
  this can be dormant, but the WorldRegistry active-world-switch/teardown path is
  live and covered by `Test.RuntimeWorldRegistry.cpp`. Memory-unsafe when reached.

## Required changes
- [ ] In `ApplyWorldRegistryMaintenance`, reset scene borrowers that hold the
      old scene by reference **before** the destroyed world's registry is freed,
      and recreate them bound to the new active scene (restore the
      `RebindActiveSceneRuntimeState` behavior for the extracted services).
- [ ] Confirm ordering: handoff reset must precede the WorldRegistry
      `ApplyMaintenance` free of the old registry (or the rebind must be driven
      by the maintenance callback before the free), not the frame after.
- [ ] Secondary (same area, lower priority): `Engine::Initialize` calls
      `m_WorldRegistry.CreateWorld("Main")` (`Runtime.Engine.cpp:541`) without the
      preceding `Clear()` main had; add it so a re-entered `Initialize` cannot
      leak a world.
- [ ] Secondary: restore `m_SelectionController.SetStableEntityLookup(nullptr)`
      for the null-new-scene case dropped from the maintenance path (benign
      today, but re-align with main).

## Tests
- [ ] Add a runtime contract test that creates a second world, activates it (via
      the deferred-op path), destroys the first, then ticks assets and fires an
      asset `Ready`/`Reloaded` event — proving no access to the freed scene and
      that new-world bindings resolve. Run under ASan.
- [ ] `ctest --test-dir build/ci --output-on-failure -R 'RuntimeWorldRegistry|AssetResidency|SceneLifecycle' --timeout 60`.
- [ ] Default CPU gate (ideally the ASan-enabled `ci` preset, which enables
      sanitizers).

## Docs
- [ ] Document in `src/runtime/README.md` (or the world/scene lifecycle doc) that
      Initialize-bound scene borrowers must be rebound on active-world change.

## Acceptance criteria
- [ ] After an active-world switch or teardown, no scene borrower references a
      destroyed registry; ASan is clean on the new test.
- [ ] Material/texture bindings for the newly-activated world are wired.
- [ ] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeWorldRegistry|AssetResidency|SceneLifecycle' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not "fix" this by leaking the old registry or by keeping worlds alive
  indefinitely.
- Do not convert the handoff's scene reference into a hidden owning pointer.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` (ASan-verified contract test reproducing the switch);
  no `Operational` follow-up is owed — this is a lifetime correction proven by the
  CPU/ASan contract path, not a backend capability.
