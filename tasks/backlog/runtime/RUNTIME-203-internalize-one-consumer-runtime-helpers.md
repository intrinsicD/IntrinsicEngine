---
id: RUNTIME-203
theme: F
depends_on: [RUNTIME-194, RUNTIME-200, RUNTIME-201]
maturity_target: Retired
---
# RUNTIME-203 — Internalize one-consumer runtime composition helpers

## Goal

- Remove exported module/BMI surfaces that are implementation helpers for one
  runtime owner, relocate their tests to the owning public behavior, and keep
  only the actual module/service/snapshot boundaries.

## Non-goals

- No behavior, ordering, backend policy, or public feature change.
- No indiscriminate file merging; implementation units/private headers remain
  where they improve readability.
- No deletion of `Engine`, `IRuntimeModule`, `ServiceRegistry`,
  `WorldRegistry`, `SceneDocumentModule`, `SceneInteractionModule`,
  `CameraModule`, or other durable owners.

## Context

- Source/consumer census identifies exported one-consumer helpers:
  `ModuleSchedule`, `EcsSystemBundle`, `JobServiceGpuQueueBridge`,
  `GizmoFrameService`, `SelectionReadback`, `RenderRecipeActivation`, and
  mixed-role `DeviceBootstrap`.
- Their behavior is load-bearing, but independent public naming is not. Engine,
  SceneInteraction, ConfigControl, AssetWorkflow, and renderer composition are
  the actual owners.
- `RUNTIME-194`, `RUNTIME-200`, and `RUNTIME-201` first remove scheduler,
  import, and gizmo/history dependencies that otherwise make privatization
  ambiguous.

## Slice plan

- **Slice A — exact census/ratchet.** Confirm production import counts and pin
  owner-specific behavior/order tests before surface changes.
- **Slice B — owner-local moves.** Internalize helpers one owner at a time,
  preserving implementation bodies and keeping mechanical moves separate.
- **Slice C — surface deletion.** Remove old `.cppm` files, public accessors,
  CMake entries, and direct helper tests after owner-level behavior tests pass.

## Required changes

- [ ] Internalize `ModuleSchedule` and `EcsSystemBundle` into Engine/module
      composition implementation while preserving dependency/order semantics.
- [ ] Fold the remaining `JobServiceGpuQueueBridge` forwarding behavior into
      `AsyncWorkModule`/renderer-hook implementation after `RUNTIME-194`.
- [ ] Internalize `GizmoFrameService` and `SelectionReadback` under
      `SceneInteractionModule`; expose copied interaction snapshots and
      feature commands, not helper objects.
- [ ] Internalize `RenderRecipeActivation` under `ConfigControl`/recipe
      composition while keeping its pure validation/project functions testable
      in implementation tests.
- [ ] Split `DeviceBootstrap`: Engine/backend selection remains composition
      implementation, while AssetWorkflow fallback-texture initialization
      moves to that owner; delete the mixed public helper.
- [ ] Remove direct helper accessors/tests and verify owner-level lifecycle,
      ordering, shutdown, fallback, and interaction behavior instead.

## Tests

- [ ] Engine module ordering/system execution contracts pass without importing
      `ModuleSchedule` or `EcsSystemBundle`.
- [ ] Async GPU participant attach/detach/shutdown tests pass through
      `AsyncWorkModule` and the renderer.
- [ ] SceneInteraction tests cover gizmo/selection readback behavior through
      public commands/snapshots only.
- [ ] Config activation and device/fallback initialization tests pass through
      their owning modules.
- [ ] Structural ratchets prove every named helper module/accessor is absent.

## Docs

- [ ] Update runtime module inventory/ownership docs to show the surviving
      owners and private implementation roles.
- [ ] Regenerate module inventory and refresh affected architecture diagrams.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [ ] Each named behavior remains covered through its true owner with identical
      lifecycle/order/failure semantics.
- [ ] None of the listed one-consumer helper modules is exported or directly
      imported by production/tests.
- [ ] No replacement wrapper/service/registry is introduced.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering|ModuleSchedule|SceneInteraction|RenderRecipe|DeviceBootstrap|JobService' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Semantic rewrites mixed into mechanical privatization.
- Removing owner-level tests with the direct helper tests.
- Creating new public forwarding types to preserve the old names.

## Maturity

- Target: `Retired`; owner-level parity must exist before each helper's final
  mechanical module-surface deletion.
