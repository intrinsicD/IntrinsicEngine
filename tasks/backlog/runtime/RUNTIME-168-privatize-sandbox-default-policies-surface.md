---
id: RUNTIME-168
theme: F
depends_on:
  - RUNTIME-172
  - RUNTIME-180
  - RUNTIME-181
  - RUNTIME-182
  - RUNTIME-183
  - RUNTIME-188
maturity_target: Operational
---
# RUNTIME-168 — Privatize Sandbox default-policy composition

## Status

- 2026-07-19 contract amendment: Sandbox policy composition now resolves the
  audited `SceneDocumentModule` and `SceneInteractionModule` split rather than
  a combined scene-editing owner. Implementation remains open.
- 2026-07-19 readiness correction: primitive-view controls are authoritative
  ECS `RenderEdges` / `RenderPoints` scene components, not
  `SceneInteractionModule` state. Existing Sandbox commands continue to author
  them through the scene/history path; this task must not resolve or invent an
  interaction primitive-view capability.

## Goal
- Replace the exported, Engine-bound
  `Extrinsic.Runtime.SandboxDefaultPolicies` surface with private Sandbox app
  composition glue over the resolved `SceneDocumentModule`,
  `SceneInteractionModule`, camera, config, UI/editor, and asset module
  services.

## Non-goals
- No default import, input-action, camera-focus, selection, post-import, or
  editor behavior change.
- No new Sandbox policy module, registry, service bundle, or generic
  application framework.
- No movement of asset, ECS, graphics, or renderer ownership into `app`.
- No `Engine&` in the replacement glue.

## Context
- Owner/layer: runtime retains lower-layer policy implementations; the Sandbox
  app visibly chooses and composes those defaults while depending on runtime
  only.
- The current 37-line exported module has one production consumer and forwards
  a broad `Engine&` to install policies across several domain owners.
- After the composition modules land, the defaults can bind their concrete
  existing service surfaces during app composition without an Engine facade or
  a new cross-domain wrapper.
- Document/history policies resolve `SceneDocumentModule` or its exact
  published `EditorCommandHistory`; selection, lookup, and gizmo policies
  resolve `SceneInteractionModule` or its exact published
  `SelectionController`. Primitive-view/render-hint commands author ECS
  `RenderEdges` / `RenderPoints` through the resolved scene and command history,
  not through an interaction service. The app glue must not recombine these
  capabilities into a scene-editing service bundle.
- This task is app policy wiring, not a seventh durable runtime owner.

## Required changes
- [ ] Inventory each default registration/unregistration and map it to the
      owning module service: asset import/postprocess, camera focus,
      `SceneDocumentModule` history/document behavior,
      `SceneInteractionModule` selection/gizmo behavior, ECS scene/history
      render-hint authoring, editor visibility/input, or config.
- [ ] Move the exported policy declarations to private runtime/app composition
      glue, or fold each registration next to the Sandbox composition call
      that owns it.
- [ ] Replace `Engine&` with explicit resolved existing capabilities; do not
      introduce a catch-all dependency struct or service locator facade.
- [ ] Preserve registration order, returned-handle lifetime, reverse-order
      unregister, and shutdown behavior.
- [ ] Remove the `.cppm` module surface and CMake module-file entry when the
      final consumer is migrated; leave no compatibility import.
- [ ] Record before/after module/import/consumer counts.

## Tests
- [ ] Preserve Sandbox default import formats, post-import processing, camera
      focus, selection, input actions, and unregister/shutdown coverage.
- [ ] Add a source/contract check proving the app glue contains no `Engine&`
      and app still imports runtime only.
- [ ] Run focused Sandbox policy/editor/import coverage, strict layering, and
      the complete default CPU-supported gate.

## Docs
- [ ] Update Sandbox and runtime composition documentation with the private
      app-policy ownership.
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Sandbox default behavior and handle cleanup are unchanged.
- [ ] No exported `SandboxDefaultPolicies` module, Engine facade, replacement
      policy module, or catch-all service bundle remains.
- [ ] App composition selects the defaults while all lower-layer behavior and
      state stays in runtime-owned modules.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|RuntimeInputActions|AssetImportFormatCoverage|RuntimeSandboxAcceptance|CameraFocus' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding a public compatibility module, Sandbox policy module, service bundle,
  or `Engine&` adapter.
- Reintroducing app ownership of assets, ECS, graphics, or renderer state.
- Changing default behavior while changing composition shape.

## Maturity
- Target: `Operational`; the private policy composition must remain exercised
  through the canonical Sandbox runtime path.
