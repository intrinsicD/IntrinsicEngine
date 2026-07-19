---
id: RUNTIME-188
theme: F
depends_on:
  - RUNTIME-172
  - RUNTIME-180
  - RUNTIME-182
maturity_target: Operational
---
# RUNTIME-188 — Extract the scene-interaction module

## Status

- 2026-07-19 audit: split from `RUNTIME-172` after the ownership inventory
  showed that selection, stable lookup/readback, gizmo, and mesh primitive-view
  state share viewport/extraction/maintenance hooks and omission behavior that
  document/history state does not. All implementation checkboxes remain open.
- Current-state indexes, architecture docs, and `tasks/SESSION-BRIEF.md` are
  intentionally deferred to the atomic integration after `RUNTIME-180`
  retires.

## Goal

- Move selection, stable-entity lookup and binding, pick readback/refinement,
  gizmo interaction, and mesh primitive-view control ownership out of
  `Runtime.Engine` into one app-composed `SceneInteractionModule` with an
  exact active-world binding and pointer-free render snapshot.

## Non-goals

- No selection, picking, refinement, gizmo transform, gizmo undo, or mesh
  primitive-view behavior change.
- No ownership of scene document/file operations, editor command history,
  camera, UI, renderer, render-extraction cache, asset workflow, or graphics
  selection/readback production.
- No generic editor-state facade, seventh generic frame phase, widened
  `RuntimeFrameHookContext`, per-owner wrapper module/service, or Engine
  compatibility surface.
- No unification of `GizmoUndoStack` with `EditorCommandHistory`; they have
  distinct commit/rollback semantics and remain separate owners.
- No per-world interaction map and no resurrection of selection, lookup,
  pending picks, gizmo state, or visualization controls when returning to an
  old world.

## Context

- Owner/layer: `runtime`; the module object has app-global lifetime, while all
  mutable interaction state is bound to exactly one active `WorldHandle` and
  its `ECS::Scene::Registry`.
- The owned cohort is `SelectionController`, `StableEntityLookup`,
  `StableEntityLookupSceneBinding`, `SelectionReadbackState`,
  `GizmoFrameService` (including interaction, undo, scratch selection, and
  packet construction), and mesh primitive-view editor controls. These states
  share one input-to-render lifecycle and must clear together at a world or
  document replacement boundary.
- `RUNTIME-180` provides the typed viewport-input hook and exact
  `CameraControllerRegistry`; `RUNTIME-182` provides the completed
  frame-owned editor-capture snapshot. This task consumes those capabilities
  without adding a generic phase or putting viewport state into the generic
  frame-hook context.
- `RUNTIME-172` provides the synchronous scene-replacement participant
  contract. Interaction is one real participant; it must disconnect and clear
  while the outgoing registry is live, then bind/rebuild against the incoming
  registry.
- Kernel world events are delayed until maintenance, so every input,
  extraction, and readback hook must also compare the cached binding with
  `WorldRegistry::ActiveWorld()` and `WorldRegistry::Get(...)` before use.
- The current selection-readback clear path only clears refined output, leaving
  in-flight sequence contexts behind; an unmatched completed readback can fall
  through as a click. Both behaviors are unsafe across a world/document
  replacement and must fail closed in the new owner.
- Gizmo cancellation currently is not part of every scene-runtime clear path,
  and its undo stack can survive replacement. Both are world-bound interaction
  state and must reset without changing the meaning of gizmo undo.
- Reverse name-sorted module shutdown can leave asset workflow participants
  alive after scene providers. `RuntimeShutdownAnnounced` is therefore the
  early quiescence boundary: detach callbacks and invalidate borrowed state
  while all services are still live, before ordinary reverse shutdown.
- The exact post-`RUNTIME-172` Engine convergence baseline is
  `33` plain imports / `11` domain imports / `2` re-exports /
  `22` public getter names.

## Required changes

- [ ] Add
      `src/runtime/Scene/Runtime.SceneInteractionModule.cppm` and matching
      `.cpp` as one concrete `SceneInteractionModule final : IRuntimeModule`
      with a PImpl. Own `SelectionController`, `StableEntityLookup`,
      `StableEntityLookupSceneBinding`, `SelectionReadbackState`,
      `GizmoFrameService` and its interaction/undo/scratch/packet state, and
      mesh primitive-view controls.
- [ ] Publish the exact `SceneInteractionModule` and exact owned
      `SelectionController` through `ServiceRegistry`. Do not publish raw
      `SelectionReadbackState`, `GizmoFrameService`, or
      `StableEntityLookupSceneBinding`. Expose stable-id resolution and
      read-only lookup diagnostics through the module; publish the raw
      `StableEntityLookup` only if the implementation inventory identifies a
      present production consumer rather than test-only access.
- [ ] Own one interaction binding
      `{WorldHandle, ECS::Scene::Registry*, interaction epoch}`. App-global
      configuration may retain selection-controller and gizmo configuration,
      mode, and orientation; clear all world-bound state on every bind:
      selected/hovered entities and ECS tags, pending/in-flight picks, lookup
      map/binding, refined-result cache, drag state/axis/modifiers, selected
      scratch, gizmo undo, packets, and the published render snapshot.
- [ ] Keep the pick sequence monotonically increasing across world and
      document replacements and across binding epochs. Never reuse an old
      sequence in a way that can let a completed GPU readback collide with a
      request in the new world.
- [ ] Resolve and retain a strong `RUNTIME-172` scene-replacement participant
      handle. In `BeforeReplace`, while the outgoing registry is live, cancel
      any gizmo drag, clear selection/hover tags and all pick/readback/refined/
      undo/scratch/packet state, and disconnect stable lookup tracking. In
      `AfterReplace`, bind the current registry, rebuild/connect stable lookup,
      attach it to `SelectionController`, and publish an empty render
      snapshot.
- [ ] Subscribe to active-world and world-retirement signals for prompt
      cleanup, but before every input, extraction, and maintenance action also
      validate the cached handle and registry pointer against
      `WorldRegistry`. A mismatch advances the interaction epoch and performs
      the same fail-closed reset/rebind; an invalid or missing world publishes
      empty state.
- [ ] Register on `RUNTIME-180`'s typed viewport-input hook after camera
      population and completed capture. Preserve the current ordering:
      completed UI capture gates gizmo and pick input; valid camera/render
      input drives gizmo first and click handling afterward. Missing camera or
      UI remains a supported omission, produces no unsafe dereference, and
      leaves viewport capture unclaimed when no interaction consumes it.
- [ ] Preserve the pre-render transform flush before constructing gizmo
      packets. In `BeforeExtraction`, after input actions and the flush, drain
      one pending pick into the final `Graphics::RenderFrameInput`, build gizmo
      packets, and submit a copied interaction render snapshot. In
      `Maintenance`, after asset/async work, drain completed readbacks. Use the
      typed viewport hook plus the existing `BeforeExtraction` and
      `Maintenance` phases; add neither a seventh `FramePhase` nor viewport
      fields to `RuntimeFrameHookContext`.
- [ ] Add a pointer-free
      `RuntimeSceneInteractionRenderSnapshot` containing its `WorldHandle`,
      copied selected stable/render identities, copied hover/refinement data
      needed by extraction, and copied gizmo draw packets. Have
      `RenderExtractionCache` copy and validate the snapshot's world and
      default to empty interaction data when the module is omitted or the
      world mismatches.
- [ ] Remove `SelectionController*`, gizmo spans, and every other borrowed
      interaction pointer from `ExtractAndSubmit` and the Engine frame hook.
      Render extraction must not retain a module-owned pointer beyond the
      snapshot submission call.
- [ ] Remove `SelectionController*` from generic
      `RuntimeInputActionServices` and generic input-action dispatch.
      Selection-specific Sandbox actions resolve the exact published
      `SelectionController` once and capture that narrow pointer in their
      registered callback, following the camera-action shape from
      `RUNTIME-180`. Generic actions remain operational when this module is
      omitted.
- [ ] Extend each in-flight pick record to include
      `{sequence, WorldHandle, interaction epoch, optional
      PickReadbackContext}` even when the issuing frame has no valid camera.
      On production drain, discard unknown or zero sequence, wrong-world, and
      wrong-epoch results before calling `SelectionController` or refinement.
      Remove the unmatched-readback fallback that currently mutates selection,
      and clear every context record at reset.
- [ ] Move mesh primitive-view `Set`, `Clear`, and `Get` behavior to the exact
      interaction capability. Route invalidation through the existing
      render-extraction service and clear world-bound controls at replacement;
      do not introduce a visualization service wrapper.
- [ ] On `RuntimeShutdownAnnounced`, cancel drag/input, invalidate the binding
      epoch, clear readback/context/snapshot state, unregister the document
      participant, and detach every borrowed service while providers are still
      live. Ordinary shutdown then unsubscribes hooks/events, withdraws exact
      services, and destroys state. Partial registration and reinitialize with
      recycled handle bits must start empty and leak no callback.
- [ ] Compose the module in Sandbox as an optional interaction capability.
      Omission produces an empty render snapshot and no selection, pick,
      stable lookup, gizmo, or mesh primitive-view behavior while scene
      document, camera, rendering, input actions, and Engine remain
      operational.
- [ ] Remove Engine interaction state, initialization, hooks, imports, and
      facades: `GetSelectionController`,
      `GetStableEntityLookupDiagnostics`, `ResolveEntityByStableId`,
      `GetGizmoInteraction`, `GetGizmoUndoStack`,
      `GetLastRefinedPrimitiveSelection`,
      `GetLastRefinedPrimitiveSelectionGeneration`,
      `SetMeshPrimitiveViewSettings`, `ClearMeshPrimitiveViewSettings`, and
      `GetMeshPrimitiveViewSettings`.
- [ ] Ratchet the exact final Engine snapshot to
      `26` plain imports / `4` domain imports / `2` re-exports /
      `15` public getter names. The seven removed imports are
      `Extrinsic.ECS.Component.StableId`,
      `Extrinsic.Runtime.GizmoFrameService`,
      `Extrinsic.Runtime.MeshPrimitiveViewPacker`,
      `Extrinsic.Runtime.PrimitiveSelectionRefinement`,
      `Extrinsic.Runtime.SelectionController`,
      `Extrinsic.Runtime.SelectionReadback`, and
      `Extrinsic.Runtime.StableEntityLookup`; the seven counted getter
      removals are `GetSelectionController`,
      `GetStableEntityLookupDiagnostics`, `GetGizmoInteraction`,
      `GetGizmoUndoStack`, `GetLastRefinedPrimitiveSelection`,
      `GetLastRefinedPrimitiveSelectionGeneration`, and
      `GetMeshPrimitiveViewSettings`.

## Tests

- [ ] Add focused module contract coverage for exact publication/withdrawal,
      duplicate-publication conflict, partial-register rollback,
      shutdown/reinitialize, optional omission, and an Operational real
      `Engine::Run()`.
- [ ] Cover the typed viewport hook and completed-capture ordering: capture
      gates selection/gizmo input, camera population precedes gizmo/picking,
      selection input actions precede
      `BeforeExtraction`, transform flush precedes packet construction, and
      completed readbacks drain in `Maintenance`.
- [ ] Cover active-world switch, destruction of the former world after the
      switch, unrelated inactive-world destruction, away/back with no state
      resurrection, direct mismatch detection before delayed events, and
      shutdown/reinitialize with recycled handle bits.
- [ ] Cover scene new/load/close through the real document participant: drag
      cancellation occurs while the old registry is live; selection/hover
      tags, pending/in-flight picks, contexts, refined cache, gizmo undo/
      scratch/packets, and primitive-view state clear; lookup disconnects
      before replacement and rebuilds/reattaches afterward.
- [ ] Cover stale-readback rejection for zero, unknown, wrong-world, and
      wrong-epoch sequences; prove an old GPU result cannot select or refine in
      a replacement world and prove the issue sequence remains monotonic
      across replacement.
- [ ] Cover missing camera, missing UI/capture adapter, missing document
      participant during boot, and full module omission. Prove empty/mismatched
      render snapshots fail closed and generic input/rendering continue.
- [ ] Migrate existing selection, stable-lookup, selection-readback,
      primitive-refinement, gizmo, mesh primitive-view, render-extraction,
      input-action, Sandbox acceptance, and GPU-smoke fixtures. Build the
      existing Vulkan/GPU smoke callers against the new composition and run
      the opt-in cohort on a capable host without adding a new GPU feature.
- [ ] Add structural checks for the exact Engine import/getter ratchet, no
      interaction pointer in extraction/input aggregates, no seventh generic
      frame phase/context widening, and no obsolete Engine facade.

## Docs

- [ ] Update the runtime README, runtime architecture, ADR-0027 current-state
      notes, kernel target-state, and Sandbox README with
      `SceneInteractionModule`, its one-world state scope, hook order,
      replacement participant, omission behavior, and pointer-free render
      snapshot.
- [ ] Update task graph/indexes and the `RUNTIME-172`, `RUNTIME-168`,
      `RUNTIME-183`, `RUNTIME-184`, and `REVIEW-003` composition descriptions;
      regenerate `tasks/SESSION-BRIEF.md`.
- [ ] Regenerate the module inventory and update the exact Engine convergence
      policy snapshot.

## Acceptance criteria

- [ ] Engine owns no selection/lookup/readback/gizmo/mesh-view state, import,
      initialization, teardown, frame work, borrowed extraction pointer, or
      public facade.
- [ ] Every interaction record belongs to exactly one validated active-world
      binding and clears on world/document replacement, retirement, shutdown,
      and recycled-handle reinitialize without state resurrection.
- [ ] Old, unknown, or mismatched GPU readbacks cannot mutate selection or
      refined output in the current world; pick sequencing remains monotonic.
- [ ] Render extraction consumes only a copied, world-tagged, pointer-free
      snapshot and behaves as empty when the module is absent or mismatched.
- [ ] Sandbox interaction remains Operational through the canonical Engine
      path, while omission leaves document, camera, generic input, and
      rendering operational.
- [ ] The exact final convergence snapshot is `26/4/2/15`.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicRuntimeGraphicsCpuTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'Selection|StableLookup|PrimitiveSelection|Gizmo|MeshPrimitiveView|RenderExtraction|RuntimeInputActions|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Sandbox|Selection|Gizmo' --no-tests=error --timeout 180
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes

- Retaining an Engine compatibility getter, borrowed interaction pointer, or
  Engine back-reference while migrating ownership.
- Recombining document/history and interaction into `SceneEditingModule`, or
  adding one module/service wrapper per owned class.
- Adding a seventh generic frame phase, widening
  `RuntimeFrameHookContext`, or introducing a generic viewport/editor context,
  event framework, interface hierarchy, bridge, queue, or service bundle.
- Retaining a per-world interaction map, resurrecting state on away/back, or
  accepting unmatched/wrong-world/wrong-epoch readbacks.
- Publishing raw mutable readback, gizmo, or lookup-binding internals without
  a demonstrated production consumer.
- Unifying gizmo undo with editor command history or changing selection,
  refinement, gizmo, mesh-view, input, or rendering semantics.
- Moving live ECS interaction ownership into graphics, app, or
  `SceneDocumentModule`.

## Maturity

- Target: `Operational`; selection, picking, stable lookup, gizmo, mesh-view,
  and render-snapshot composition must run through the canonical Engine/
  Sandbox path, not only direct class or module contract tests.
