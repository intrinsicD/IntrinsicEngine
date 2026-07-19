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

- Completed and retired at `Operational` on 2026-07-19; owner: Codex team;
  implementation branch:
  `codex/runtime-188-scene-interaction-integrated`.
- Implementation checkpoints: task promotion `b0dfdbe9`; scene-interaction
  implementation `8c8bb5d2`.
- Commit reference: this retirement commit records final tests, documentation,
  generated state, and lifecycle closure.
- One app-composed PImpl now owns the exact one-world selection/lookup/
  readback/gizmo cohort, exact service publication, typed viewport plus
  `BeforeExtraction`/`Maintenance` hooks, strong document-replacement
  participation, copied render snapshots, fail-closed readback correlation,
  and announcement-first teardown. Engine retains only the named
  `RUNTIME-183` implementation-local selection borrow.
- Verification evidence:
  - strict kernel convergence passes at exactly `26/4/2/15`;
  - the four focused CPU producers built 1,116/1,116 steps, the post-review
    focused runtime selector passed 214/214, and direct module behavior passed
    11/11 including registered-hook issue/correlation/refinement/background
    reset and stale-versus-live retry callback proof;
  - `IntrinsicTests` built 1,250/1,250 steps and the post-review canonical
    CPU-supported selector reported 4,225 passed plus one expected GLFW/LSan
    capability skip out of 4,226 selected cases, with zero failures;
  - the narrowed `ci-vulkan` Sandbox/interaction producers built 729/729
    steps. Seventeen functional `gpu;vulkan` Sandbox/selection/gizmo cases
    passed in the first invocation; that invocation exposed only an omitted
    `IntrinsicGlfwLifecycleLsanProcess` producer for the shutdown contract.
    After building the exact helper 77/77, the five-frame shutdown leak-clean
    and synthetic engine-leak control passed 1/1 in 103.97 seconds;
  - strict layering, task policy, documentation links, test layout, root
    hygiene, generated inventory/session brief, and diff checks pass on the
    retirement state.
- 2026-07-19 audit: split from `RUNTIME-172` after the ownership inventory
  showed that selection, stable lookup/readback, and gizmo state share
  viewport/extraction/maintenance hooks and omission behavior that
  document/history state does not.
- 2026-07-19 readiness correction: `RUNTIME-106` made ECS `RenderEdges` /
  `RenderPoints` the authoritative primitive-view controls, and `RUNTIME-172`
  preserves those components through scene serialization. The remaining Engine
  facade and render-extraction settings cache have no production consumer; this
  task deletes that compatibility residue instead of moving or clearing it as
  interaction state.
- 2026-07-19 `RUNTIME-172` landing evidence: the temporary
  `RUNTIME-188.EngineInteractionTransition` captures only the exact
  long-lived selection, selection-readback, stable-lookup, and stable-binding
  objects. It clears/disconnects before document replacement and rebuilds/
  rebinds afterward. This task replaces that callback with its module-owned
  strong participant handle; the corrected mesh-view deletion and narrow
  snapshot contract remain authoritative.
- `RUNTIME-180`, `RUNTIME-182`, and `RUNTIME-172` are retired. The
  scene-document participant contract and retained-handle shutdown lifecycle
  are available on `main`; implementation is unblocked.

## Goal

- Move selection, stable-entity lookup and binding, pick readback/refinement,
  and gizmo interaction out of `Runtime.Engine` into one app-composed
  `SceneInteractionModule` with an exact active-world binding and pointer-free
  render snapshot; delete the obsolete Engine mesh primitive-view facade and
  unused settings cache.

## Non-goals

- No selection, picking, refinement, gizmo transform, gizmo undo, component-
  driven primitive-view extraction, or scene-serialization behavior change.
- No `SceneInteractionModule` ownership of ECS `RenderEdges` / `RenderPoints`;
  those remain scene-authoring components edited through the existing Sandbox
  command/history path.
- No ownership of scene document/file operations, editor command history,
  camera, UI, renderer, render-extraction cache, asset workflow, or graphics
  selection/readback production.
- No generic editor-state facade, seventh generic frame phase, widened
  `RuntimeFrameHookContext`, per-owner wrapper module/service, or Engine
  compatibility surface.
- No unification of `GizmoUndoStack` with `EditorCommandHistory`; they have
  distinct commit/rollback semantics and remain separate owners.
- No per-world interaction map and no resurrection of selection, lookup,
  pending picks, or gizmo state when returning to an old world.

## Context

- Owner/layer: `runtime`; the module object has app-global lifetime, while all
  mutable interaction state is bound to exactly one active `WorldHandle` and
  its `ECS::Scene::Registry`.
- The owned cohort is `SelectionController`, `StableEntityLookup`,
  `StableEntityLookupSceneBinding`, `SelectionReadbackState`,
  `GizmoFrameService` (including interaction, undo, scratch selection, and
  packet construction). These states share one input-to-render lifecycle and
  must clear together at a world or document replacement boundary.
- The legacy `Engine::{Set,Clear,Get}MeshPrimitiveViewSettings` methods only
  translate to authoritative ECS `RenderEdges` / `RenderPoints`. The parallel
  `RenderExtractionCache` settings map is not read by production extraction,
  and the Sandbox editor already authors the ECS components directly under
  `EditorCommandHistory`. Moving either compatibility surface would create
  module API with no production consumer and would incorrectly make persistent
  scene authoring state ephemeral interaction state.
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
- Until `RUNTIME-183` moves `AssetImportPipeline`, its production import-
  completed policy still borrows the active `SelectionController` to select a
  newly imported entity. This is one named, implementation-only transition:
  `Runtime.Engine.cpp` resolves the exact published service locally in
  `BindActiveSceneAssetHandoffs` and when registering
  `RUNTIME-183.EngineAssetHandoffTransition`; it is not Engine ownership and
  must not appear in `Runtime.Engine.cppm`, Engine state, frame work, or a
  facade.
- The exact post-`RUNTIME-172` Engine convergence baseline is
  `33` plain imports / `11` domain imports / `2` re-exports /
  `22` public getter names.

## Required changes

- [x] Add
      `src/runtime/Scene/Runtime.SceneInteractionModule.cppm` and matching
      `.cpp` as one concrete `SceneInteractionModule final : IRuntimeModule`
      with a PImpl. Own `SelectionController`, `StableEntityLookup`,
      `StableEntityLookupSceneBinding`, `SelectionReadbackState`,
      and `GizmoFrameService` with its interaction/undo/scratch/packet state.
- [x] Publish the exact `SceneInteractionModule` and exact owned
      `SelectionController` through `ServiceRegistry`. Do not publish raw
      `SelectionReadbackState`, `GizmoFrameService`, or
      `StableEntityLookupSceneBinding`. Expose stable-id resolution and
      read-only lookup diagnostics through the module; publish the raw
      `StableEntityLookup` only if the implementation inventory identifies a
      present production consumer rather than test-only access.
- [x] Replace Engine's temporary
      `RUNTIME-188.EngineInteractionTransition` with the module-owned strong
      `RUNTIME-172` participant handle. Keep
      `RUNTIME-183.EngineAssetHandoffTransition` until `RUNTIME-183`, but source
      its `SelectionController*` and the initial
      `AssetImportPipelineDependencies::Selection` from local
      `ServiceRegistry::Find<SelectionController>()` results at those existing
      wiring call sites. This is the only allowed Engine-side interaction
      borrow: it is non-owning, implementation-only, never cached in an Engine
      member, null when the module is omitted, and owned for removal by
      `RUNTIME-183`.
- [x] Own one interaction binding
      `{WorldHandle, ECS::Scene::Registry*, interaction epoch}`. App-global
      configuration may retain selection-controller and gizmo configuration,
      mode, and orientation; clear all world-bound state on every bind:
      selected/hovered entities and ECS tags, pending/in-flight picks, lookup
      map/binding, refined-result cache, drag state/axis/modifiers, selected
      scratch, gizmo undo, packets, and the published render snapshot.
- [x] Keep the pick sequence monotonically increasing across world and
      document replacements and across binding epochs. Never reuse an old
      sequence in a way that can let a completed GPU readback collide with a
      request in the new world.
- [x] Resolve and retain a strong `RUNTIME-172` scene-replacement participant
      handle. In `BeforeReplace`, while the outgoing registry is live, cancel
      any gizmo drag, clear selection/hover tags and all pick/readback/refined/
      undo/scratch/packet state, and disconnect stable lookup tracking. In
      `AfterReplace`, bind the current registry, rebuild/connect stable lookup,
      attach it to `SelectionController`, and publish an empty render
      snapshot.
- [x] Subscribe to active-world and world-retirement signals for prompt
      cleanup, but before every input, extraction, and maintenance action also
      validate the cached handle and registry pointer against
      `WorldRegistry`. A mismatch advances the interaction epoch and performs
      the same fail-closed reset/rebind; an invalid or missing world publishes
      empty state.
- [x] Register on `RUNTIME-180`'s typed viewport-input hook after camera
      population and completed capture. Preserve the current ordering:
      completed UI capture gates gizmo and pick input; valid camera/render
      input drives gizmo first and click handling afterward. Missing camera or
      UI remains a supported omission, produces no unsafe dereference, and
      leaves viewport capture unclaimed when no interaction consumes it.
- [x] Preserve the pre-render transform flush before constructing gizmo
      packets. In `BeforeExtraction`, after input actions and the flush, drain
      one pending pick into the final `Graphics::RenderFrameInput`, build gizmo
      packets, and submit a copied interaction render snapshot. In
      `Maintenance`, after asset/async work, drain completed readbacks. Use the
      typed viewport hook plus the existing `BeforeExtraction` and
      `Maintenance` phases; add neither a seventh `FramePhase` nor viewport
      fields to `RuntimeFrameHookContext`.
- [x] Add a pointer-free
      `RuntimeSceneInteractionRenderSnapshot` on
      `Extrinsic.Runtime.RenderExtraction` containing only its `WorldHandle`,
      copied selected render IDs, copied hover presence/render ID, and copied
      gizmo draw packets. Have `RenderExtractionCache` copy the submitted value,
      validate its world, and default to empty interaction data when the module
      is omitted or the world mismatches. Primitive-refinement output remains
      editor-facing state on the exact interaction module; no refinement result,
      pick context, or other zero-consumer payload belongs in the render
      snapshot.
- [x] Remove `SelectionController*`, gizmo spans, and every other borrowed
      interaction pointer from `ExtractAndSubmit` and the Engine frame hook.
      Render extraction must not retain a module-owned pointer beyond the
      snapshot submission call.
- [x] Remove `SelectionController*` from generic
      `RuntimeInputActionServices` and generic input-action dispatch.
      Selection-specific Sandbox actions resolve the exact published
      `SelectionController` once and capture that narrow pointer in their
      registered callback, following the camera-action shape from
      `RUNTIME-180`. Generic actions remain operational when this module is
      omitted.
- [x] Extend each in-flight pick record to include
      `{sequence, WorldHandle, interaction epoch, optional
      PickReadbackContext}` even when the issuing frame has no valid camera.
      On production drain, discard unknown or zero sequence, wrong-world, and
      wrong-epoch results before calling `SelectionController` or refinement.
      Remove the unmatched-readback fallback that currently mutates selection,
      and clear every context record at reset.
- [x] Delete the obsolete Engine mesh primitive-view `Set`, `Clear`, and `Get`
      facade; the zero-consumer `Runtime.MeshPrimitiveViewControls` translation
      module and CMake entries; and the unused `RenderExtractionCache`
      `Set`/`Clear`/`Get` settings API, backing map, and private-service
      forwarding. Do not remove `MeshPrimitiveViewPacker`, component-driven
      extraction/sidecars, ECS `RenderEdges` / `RenderPoints`, Sandbox
      component authoring/history, or `RUNTIME-172` serialization coverage.
- [x] On `RuntimeShutdownAnnounced`, cancel drag/input, invalidate the binding
      epoch, clear readback/context/snapshot state, unregister the document
      participant, and detach every borrowed service while providers are still
      live. Engine must already have cancelled active imports, then detach the
      transitional `AssetImportPipeline` selection dependency in
      `AnnounceAndShutdownRuntimeModules()` after the announcement pump returns
      and before reverse `OnShutdown` can destroy the controller. Document
      quiescence prevents the retained
      `RUNTIME-183.EngineAssetHandoffTransition` callback from running after
      announcement; its provider-owned capture is discarded without
      dereferencing the controller. Ordinary shutdown then unsubscribes
      hooks/events, withdraws exact services, and destroys state. Partial
      registration and reinitialize with recycled handle bits must start empty
      and leak no live callback.
- [x] Compose the module in Sandbox as an optional interaction capability.
      Omission produces an empty render snapshot and no selection, pick,
      stable lookup, or gizmo behavior while scene document, camera, rendering,
      generic input actions, component-driven `RenderEdges` / `RenderPoints`,
      and Engine remain operational.
- [x] Remove Engine interaction state, initialization, hooks, imports, and
      facades: `GetSelectionController`,
      `GetStableEntityLookupDiagnostics`, `ResolveEntityByStableId`,
      `GetGizmoInteraction`, `GetGizmoUndoStack`,
      `GetLastRefinedPrimitiveSelection`,
      `GetLastRefinedPrimitiveSelectionGeneration`,
      `SetMeshPrimitiveViewSettings`, `ClearMeshPrimitiveViewSettings`, and
      `GetMeshPrimitiveViewSettings`. Remove all owned interaction members and
      helpers; the named `RUNTIME-183` implementation-only selection borrow
      above is the sole temporary exception.
- [x] Ratchet the exact final Engine snapshot to
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

- [x] Add focused module contract coverage for exact publication/withdrawal,
      duplicate-publication conflict, partial-register rollback,
      shutdown/reinitialize, optional omission, and an Operational real
      `Engine::Run()`.
- [x] Cover the typed viewport hook and completed-capture ordering: capture
      gates selection/gizmo input, camera population precedes gizmo/picking,
      selection input actions precede
      `BeforeExtraction`, transform flush precedes packet construction, and
      completed readbacks drain in `Maintenance`.
- [x] Cover active-world switch, destruction of the former world after the
      switch, unrelated inactive-world destruction, away/back with no state
      resurrection, direct mismatch detection before delayed events, and
      shutdown/reinitialize with recycled handle bits.
- [x] Cover scene new/load/close through the real document participant: drag
      cancellation occurs while the old registry is live; selection/hover
      tags, pending/in-flight picks, contexts, refined cache, gizmo undo/
      scratch/packets clear; lookup disconnects before replacement and
      rebuilds/reattaches afterward. Prove serialized ECS `RenderEdges` /
      `RenderPoints` survive the document round trip independently of
      interaction reset.
- [x] Cover stale-readback rejection for zero, unknown, wrong-world, and
      wrong-epoch sequences; prove an old GPU result cannot select or refine in
      a replacement world and prove the issue sequence remains monotonic
      across replacement.
- [x] Cover missing camera, missing UI/capture adapter, missing document
      participant during boot, and full module omission. Prove empty/mismatched
      render snapshots fail closed and generic input/rendering plus component-
      driven primitive views continue.
- [x] Cover the `RUNTIME-183` transition explicitly: the Engine implementation
      resolves the published controller for initial and replacement asset
      dependencies, omission supplies null, active imports cancel before the
      borrow detaches on shutdown announcement, and no callback dereferences it
      during reverse teardown.
- [x] Migrate existing selection, stable-lookup, selection-readback,
      primitive-refinement, gizmo, render-extraction, input-action, Sandbox
      acceptance, and GPU-smoke fixtures. Replace Engine compatibility tests
      with structural deletion checks while preserving existing component-
      driven mesh primitive-view extraction and serialization fixtures. Build
      the existing Vulkan/GPU smoke callers against the new composition and
      run the opt-in cohort on a capable host without adding a new GPU feature.
- [x] Add structural checks for the exact Engine import/getter ratchet, no
      interaction pointer in extraction/input aggregates, no seventh generic
      frame phase/context widening, no obsolete Engine facade/settings cache,
      and no Engine interaction borrow beyond the exact `RUNTIME-183`
      implementation-only transition.

## Docs

- [x] Update the runtime README, runtime architecture, ADR-0027 current-state
      notes, kernel target-state, and Sandbox README with
      `SceneInteractionModule`, its one-world state scope, hook order,
      replacement participant, omission behavior, and pointer-free render
      snapshot. Record ECS `RenderEdges` / `RenderPoints` as persistent
      scene-authoring state, not interaction-module state.
- [x] Update task graph/indexes and the `RUNTIME-172`, `RUNTIME-168`,
      `RUNTIME-183`, `RUNTIME-184`, and `REVIEW-003` composition descriptions;
      regenerate `tasks/SESSION-BRIEF.md`.
- [x] Regenerate the module inventory and update the exact Engine convergence
      policy snapshot.

## Acceptance criteria

- [x] `Runtime.Engine.cppm` owns no selection/lookup/readback/gizmo/mesh-view
      state, import, initialization, teardown, frame work, borrowed extraction
      pointer, or public facade. `Runtime.Engine.cpp` contains only the named
      non-owning `RUNTIME-183` asset-import transition, detached on shutdown
      announcement and removed by `RUNTIME-183`.
- [x] Every interaction record belongs to exactly one validated active-world
      binding and clears on world/document replacement, retirement, shutdown,
      and recycled-handle reinitialize without state resurrection.
- [x] Old, unknown, or mismatched GPU readbacks cannot mutate selection or
      refined output in the current world; pick sequencing remains monotonic.
- [x] Render extraction consumes only a copied, world-tagged, pointer-free
      snapshot of selected render IDs, hover, and gizmo packets; it behaves as
      empty when the module is absent or mismatched and receives no refinement
      payload.
- [x] No Engine/interaction mesh primitive-view compatibility facade, settings
      cache, or translation module remains. ECS `RenderEdges` / `RenderPoints`,
      their component-driven extraction, Sandbox authoring/history, and
      `RUNTIME-172` serialization remain authoritative and operational.
- [x] Sandbox interaction remains Operational through the canonical Engine
      path, while omission leaves document, camera, generic input, and
      rendering operational.
- [x] The exact final convergence snapshot is `26/4/2/15`.

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

- Retaining an Engine compatibility getter, Engine back-reference, or borrowed
  interaction pointer other than the exact implementation-only
  `RUNTIME-183.EngineAssetHandoffTransition` / `AssetImportPipeline` selection
  dependency named above.
- Recombining document/history and interaction into `SceneEditingModule`, or
  adding one module/service wrapper per owned class.
- Adding a seventh generic frame phase, widening
  `RuntimeFrameHookContext`, or introducing a generic viewport/editor context,
  event framework, interface hierarchy, bridge, queue, or service bundle.
- Retaining a per-world interaction map, resurrecting state on away/back, or
  accepting unmatched/wrong-world/wrong-epoch readbacks.
- Publishing raw mutable readback, gizmo, or lookup-binding internals without
  a demonstrated production consumer.
- Unifying gizmo undo with editor command history, moving persistent
  `RenderEdges` / `RenderPoints` into interaction state, or changing selection,
  refinement, gizmo, mesh-view, input, serialization, or rendering semantics.
- Moving live ECS interaction ownership into graphics, app, or
  `SceneDocumentModule`.

## Maturity

- Target: `Operational`; selection, picking, stable lookup, gizmo, and render-
  snapshot composition must run through the canonical Engine/Sandbox path, not
  only direct class or module contract tests. Existing component-driven
  primitive views remain independently Operational.
