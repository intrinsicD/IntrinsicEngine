---
id: RUNTIME-203
theme: F
depends_on:
  - RUNTIME-194
  - RUNTIME-202
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
maturity_target: Retired
---
# RUNTIME-203 — Internalize Engine composition helpers

## Goal

- Remove the exported `ModuleSchedule`, `EcsSystemBundle`, and
  `JobServiceGpuQueueBridge` BMIs after relocating their behavior and tests to
  the one production owner, `Runtime.Engine`.

## Non-goals

- No behavior, ordering, fixed-step, backend, shutdown, or public Engine
  lifecycle change.
- No SceneInteraction cleanup; RUNTIME-205 owns `GizmoFrameService` and
  `SelectionReadback` as a separate owner-local slice.
- No change to `RenderRecipeActivation` or `DeviceBootstrap`. The current
  census shows multiple production consumers and load-bearing boot/hot-apply /
  asset-workflow contracts, so they fail this task's one-consumer premise.
- No deletion of `Engine`, `IRuntimeModule`, `ServiceRegistry`,
  `WorldRegistry`, or any app-composed runtime module.

## Context

- Owner/layer: runtime composition, specifically the private Engine
  implementation.
- Current production census: only `Runtime.Engine.cpp` imports each of
  `Runtime.ModuleSchedule`, `Runtime.EcsSystemBundle`, and
  `Runtime.JobServiceGpuQueueBridge`. Their direct tests preserve exported
  names that production no longer needs.
- The behavior is load-bearing: deterministic module-hook ordering, promoted
  ECS fixed-step/pre-render execution, GPU participant command recording, and
  shutdown ordering must remain. The finding is public naming without a
  second production consumer, not disposable behavior.
- RUNTIME-194 established JobService as the sole work lifecycle; RUNTIME-202
  completed feature-local facade retirement. This task follows both and must
  not recreate an Engine auxiliary API.
- Exclusion census: `RenderRecipeActivation` is consumed by Engine boot,
  EngineConfigControl, AssetWorkflow, and recipe editing; `DeviceBootstrap` is
  shared by Engine backend creation and AssetWorkflow fallback setup. Any
  future change to either requires its own concrete defect or ownership task.

## Slice plan

- **Slice A — census and public-behavior ratchets.** Reconfirm imports and pin
  lifecycle/order behavior through Engine/module composition.
- **Slice B — private relocation.** Move one helper at a time into Engine
  implementation units/include-only detail while preserving bodies.
- **Slice C — exported-surface deletion.** Remove old modules/CMake entries and
  direct helper tests after replacement behavior tests pass.

## Required changes

- [ ] Re-run the production/test consumer census and confirm the three named
      modules still have exactly one production owner before changing them.
- [ ] Internalize `RuntimeModuleSchedule` records/sorting/dispatch in Engine
      implementation detail while preserving deterministic module/sequence
      order and retained viewport/frame hook policy.
- [ ] Internalize promoted ECS bundle registration and pre-render transform
      flush behind Engine's existing fixed-step/frame phases; keep ECS systems
      and `Core::FrameGraph` ownership unchanged.
- [ ] Inline or privately relocate `JobServiceGpuQueueBridge` hook-token
      lifecycle into Engine composition, preserving install-before-use,
      unregister-before-participant-shutdown, GPU-idle callback, and exact
      JobService ownership.
- [ ] Delete the three exported `.cppm` surfaces, obsolete access points,
      CMake entries, re-exports, and tests that exist only to instantiate the
      helpers directly.

## Tests

- [ ] Engine/module lifecycle contracts prove deterministic frame/viewport
      hook ordering, clear/reinitialize behavior, and no retained callbacks
      after shutdown without importing `ModuleSchedule`.
- [ ] Real Engine fixed-step and pre-render contracts prove transform,
      bounds, render-sync, and post-editor transform flush behavior without
      importing `EcsSystemBundle`.
- [ ] Engine renderer/JobService lifecycle contracts prove GPU participant
      frame-command recording and shutdown ordering without importing
      `JobServiceGpuQueueBridge`.
- [ ] Structural ratchets prove all three public module names and direct test
      imports are absent while the owner-level behavior tests remain.
- [ ] Default CPU and sanitizer-supported gates required by the high-risk
      surface deletion pass.

## Docs

- [ ] Update the runtime module/ownership inventory to name the surviving
      Engine-private implementation roles.
- [ ] Record why `RenderRecipeActivation` and `DeviceBootstrap` remain public
      current contracts rather than silently carrying them as unfinished
      scope.
- [ ] Regenerate the module inventory and refresh task/session/retirement
      records.

## Acceptance criteria

- [ ] Engine preserves identical hook ordering, ECS execution/flush, GPU
      participant recording, and shutdown behavior through public lifecycle
      tests.
- [ ] Production and tests no longer import or name `ModuleSchedule`,
      `EcsSystemBundle`, or `JobServiceGpuQueueBridge` as exported modules.
- [ ] No replacement wrapper, service, registry, schedule interface, or Engine
      compatibility accessor is introduced.
- [ ] SceneInteraction, recipe activation, and device bootstrap are unchanged.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering|RuntimeModule|RuntimeEcs|Transform|JobService|AsyncWork' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Mixing semantic lifecycle changes into mechanical privatization.
- Moving ECS system ownership into Engine or graphics ownership into JobService.
- Removing owner-level behavior coverage with direct helper tests.
- Adding SceneInteraction, recipe, device, or unrelated Engine cleanup.
- Creating new public forwarding types to preserve deleted names.

## Maturity

- Target: `Retired`; public helper surfaces disappear only after owner-level
  behavior and lifecycle parity are proven.
