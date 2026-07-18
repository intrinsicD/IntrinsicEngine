---
id: RUNTIME-172
theme: F
depends_on:
  - HARDEN-086
  - RUNTIME-179
maturity_target: Operational
---
# RUNTIME-172 — Extract the scene-editing composition module

## Goal
- Move scene-document, history, selection, stable lookup/readback, and gizmo
  ownership out of `Runtime.Engine` into one app-composed
  `SceneEditingModule` with explicit active-world/document state.

## Non-goals
- No scene serialization format, undo vocabulary, selection, picking, gizmo,
  or hierarchy behavior change.
- No new scene editor UX or world-switch policy.
- No wrapper module per existing state owner and no generic editor-state
  facade.
- No predetermined document/interaction split. The implementation inventory
  must split when any ADR-0026 cohesion axis diverges and must not split merely
  for naming symmetry or different internal hooks.

## Context
- Owner/layer: `runtime`; the module object has app-global lifetime, while
  document identity, dirty/history state, selection, refined readback, stable
  lookup/binding, and gizmo interaction are scoped to the active
  `WorldHandle`.
- The grouping passes ADR-0026 today: Sandbox composes these states together;
  scene new/load/close already coordinates their ordered reset; selection and
  gizmo commits feed the same document-history authority; and all consumers
  observe the same active document/world.
- Split trigger: create separate owners when lifecycle, state scope,
  dependency/cancellation/commit ownership, or published-state/consumer
  reactions diverge. A production app composing one without the other is
  strong consumer evidence; different internal frame hooks alone are not.
- `HARDEN-086` first replaces the two runtime-local hierarchy walks used by
  history/editor state with the promoted all-or-nothing ECS query contract.
- Queued scene save/load consumes the async capability from `RUNTIME-179`;
  it does not own another executor.

## Required changes
- [ ] Add one concrete `SceneEditingModule` owning `SceneDocument`,
      `EditorCommandHistory`, `SelectionController`, `StableEntityLookup` and
      its scene binding, `SelectionReadbackState`, and `GizmoFrameService`.
- [ ] Inventory all four ADR-0026 axes for document and interaction state;
      split the implementation/task if any live axis disproves the current
      cohesive-owner hypothesis.
- [ ] Preserve the exact outgoing-scene cleanup, lookup disconnect/rebuild,
      registry replacement, selection/readback reset, and document-history
      ordering for new/load/close.
- [ ] Resolve async, world, renderer, render-extraction, input, command, and
      event capabilities through module setup/services without storing
      `Engine&`.
- [ ] Register the existing selection/gizmo/readback work at named frame hooks
      and reset every world-scoped record when the active world changes or its
      world retires.
- [ ] Publish the concrete scene-editing capability needed by asset/editor
      consumers without wrapping each owned state in a forwarding service.
- [ ] Move mesh primitive-view editor controls into the scene-editing
      capability and route visualization bindings directly through the
      existing render-extraction service.
- [ ] Remove Engine state/imports and all scene-document, selection, history,
      stable-lookup, gizmo, refined-primitive, and mesh-view domain facades,
      including `GetScene`; callers resolve the active registry through
      `WorldRegistry`.
- [ ] Remove or privatize the broad standalone `SceneDocument` surface inside
      the module; do not leave an Engine-private compatibility owner.

## Tests
- [ ] Preserve scene save/load/new/close, dirty/history, selection, stable
      lookup, refined readback, gizmo, and mesh-view behavior.
- [ ] Add active-world switch and active/inactive-world destruction coverage
      proving all world-scoped state is cleared/rebound and stale async results
      cannot mutate a replacement document or retired registry.
- [ ] Add module integration coverage for pre-extraction and maintenance work
      during a real `Engine::Run()`.
- [ ] Run focused runtime/editor/scene coverage, strict layering, and the
      complete default CPU-supported gate.

## Docs
- [ ] Update runtime scene/editing ownership docs and the kernel target-state
      with the active-world/document state decision and split trigger.
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Engine owns no scene-document/editor-interaction state, domain import, or
      public facade.
- [ ] Every durable record is either keyed by the active `WorldHandle` or
      deterministically cleared/rebound at the active-world boundary.
- [ ] The present cohesive owner replaces Engine wiring without module-per-
      service ceremony.
- [ ] The implementation inventory proves cohesion across lifecycle, state
      scope, dependency/cancellation/commit ownership, and consumer reactions;
      divergence on any axis produces a split.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'SceneDocument|SceneSerialization|EditorCommandHistory|Selection|StableLookup|Gizmo|MeshPrimitiveView|ECSHierarchy' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Making `SceneDocument` or another scene-editing owner Engine-private.
- Adding one module/service wrapper for each existing class.
- Publishing partial hierarchy results after a `HARDEN-086` query failure.
- Retaining Engine compatibility getters or changing edit semantics during the
  ownership move.

## Maturity
- Target: `Operational`; the composed owner must run through the canonical
  scene/editor runtime path, not only direct contract tests.
