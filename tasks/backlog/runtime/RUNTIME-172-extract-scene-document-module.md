---
id: RUNTIME-172
theme: F
depends_on:
  - HARDEN-086
  - RUNTIME-179
maturity_target: Operational
---
# RUNTIME-172 — Extract the scene-document module

## Status

- 2026-07-19 audit amendment: the implementation inventory disproved the
  proposed single `SceneEditingModule` owner. Document/history and editor
  interaction differ in dependencies, frame hooks, cancellation, published
  state, and omission behavior, so this task now owns only
  `SceneDocumentModule`; `RUNTIME-188` owns interaction. All implementation
  checkboxes remain open.
- Current-state indexes, architecture docs, and `tasks/SESSION-BRIEF.md` are
  intentionally deferred to the atomic integration after `RUNTIME-180`
  retires.

## Goal

- Move scene-document, command-history, and scene-file operation ownership out
  of `Runtime.Engine` into one app-composed `SceneDocumentModule` with an
  exact active-world binding and a synchronous replacement-participant
  contract.

## Non-goals

- No scene serialization format, editor-command vocabulary, or save/load/new/
  close behavior change.
- No ownership of selection, stable lookup, selection readback, gizmo state,
  mesh primitive-view controls, camera, UI, render extraction, GPU residency,
  or object-space-normal bake state.
- No generic editor-state facade, replacement event framework, per-owner
  wrapper service, or Engine compatibility surface.
- No per-world document/history map and no resurrection of state when an old
  world becomes active again.

## Context

- Owner/layer: `runtime`; the module object has app-global lifetime, while
  document path/event identity and command history are bound to exactly one
  live active `WorldHandle` plus its `ECS::Scene::Registry`.
- The 2026-07-19 audit found two real scene-replacement consumers with
  different ownership: `RUNTIME-188` must reset/rebind editor interaction and
  `RUNTIME-183` must release/rebind asset scene handoffs. That evidence earns
  one small synchronous replacement-participant descriptor and strong handle;
  it does not earn an interface hierarchy, queue, bridge, or generic event
  framework.
- `WorldRegistry::RequestDestroyWorld(active)` returns `ResourceBusy`, so an
  "active-world destruction" test is not a valid lifecycle case. The relevant
  cases are active-world switch, destruction of the former world after the
  switch, unrelated inactive-world destruction, and shutdown/reinitialize
  even when handle bits are recycled.
- Kernel events are pumped after world maintenance. Event subscriptions alone
  therefore cannot protect an operation in the interval after
  `ActiveWorld()` changes. Every operation must compare its cached handle and
  registry pointer with `WorldRegistry::ActiveWorld()` and
  `WorldRegistry::Get(...)` before use.
- Current queued scene operations can retain raw module state across shutdown
  and replacement. Completion must instead be guarded by owned operation
  state, binding epoch, target world, and target registry identity.
- `HARDEN-086` first replaces the two runtime-local hierarchy walks used by
  document/history state with the promoted all-or-nothing ECS query contract.
- Queued scene save/load consumes the async capability from `RUNTIME-179`;
  it does not own an executor and remains optional.
- The exact post-`RUNTIME-180` Engine convergence baseline is
  `35` plain imports / `13` domain imports / `2` re-exports /
  `25` public getter names.

## Required changes

- [ ] Add
      `src/runtime/Scene/Runtime.SceneDocumentModule.cppm` and matching `.cpp`
      as one concrete `SceneDocumentModule final : IRuntimeModule` with a
      PImpl. Fold the existing `Runtime.SceneDocument.cppm/.cpp` API and
      implementation into it and delete that broad standalone module surface.
- [ ] Own one active binding
      `{WorldHandle, ECS::Scene::Registry*, binding epoch}` plus document path,
      last file event/sequence, queued-operation state, and one
      `EditorCommandHistory`. Bind the current active world at registration;
      on any world mismatch reset history, path, and last event, advance the
      epoch, and bind only the new live registry. Never retain a per-world map.
- [ ] Publish the exact `SceneDocumentModule` and its exact owned
      `EditorCommandHistory` through `ServiceRegistry`. Do not add a document
      service wrapper, history wrapper, or forwarding facade. Omission means
      document/history/file-operation services are unavailable while Engine
      and the active world remain operational.
- [ ] Preserve the existing synchronous and queued save/load plus new/close
      operations and the existing last-file-event result shape. During
      `OnResolve`, optionally find the `StreamingExecutor`; synchronous
      operations remain available without it and queued operations return
      `InvalidState`.
- [ ] Before every public operation, validate the cached binding against both
      `WorldRegistry::ActiveWorld()` and `WorldRegistry::Get(handle)`. Fail
      closed or rebind/reset as appropriate before touching the registry;
      delayed lifecycle events must never be the sole validity guard.
- [ ] Replace the current cross-owner cleanup aggregate with these plain
      synchronous declarations owned by the document module:

      ```cpp
      enum class SceneReplacementKind : std::uint8_t
      {
          New,
          Load,
          Close,
      };

      struct SceneReplacementContext
      {
          SceneReplacementKind Kind;
          WorldHandle World;
          ECS::Scene::Registry& Registry;
          std::uint64_t BindingEpoch;
      };

      struct SceneReplacementParticipantDesc
      {
          std::string Name;
          std::function<void(const SceneReplacementContext&)> BeforeReplace;
          std::function<void(const SceneReplacementContext&)> AfterReplace;
      };
      ```

      Return a strong registration handle whose lifetime controls
      participation. Reject empty/duplicate names, order callbacks
      deterministically by name then registration sequence, and invoke them
      synchronously rather than publishing queued replacement events.
- [ ] Parse a load into temporary state before invoking any participant or
      mutating the active registry. On parse failure invoke no callback and
      leave registry, path, event, and history unchanged. On successful new,
      load, or close, run every `BeforeReplace` callback while the outgoing
      registry is still live, perform the replacement, run every
      `AfterReplace` callback against the rebound registry, and reset history
      only after the complete replacement succeeds.
- [ ] Have queued completions capture weak/shared operation state rather than
      raw `this`. Commit only when module generation, binding epoch,
      `WorldHandle`, and registry identity still match. On
      `RuntimeShutdownAnnounced`, invalidate the generation/epoch, cancel all
      owned task handles, and prevent every late callback from committing
      before reverse module shutdown begins.
- [ ] Subscribe to active-world and retirement events for prompt cleanup, but
      retain the direct binding validation above. Unsubscribe, withdraw
      services/participants, cancel tasks, and destroy state safely on partial
      registration failure and shutdown; reinitialize starts empty even when
      recreated handle bits match a prior run.
- [ ] Remove Engine document/history state, the
      `Extrinsic.Runtime.SceneDocument` and
      `Extrinsic.Runtime.EditorCommandHistory` imports, and
      `GetSceneDocument`, `GetEditorCommandHistory`, and `GetScene`.
      Callers needing the active registry resolve `WorldRegistry`; callers
      needing document/history behavior resolve the published exact services.
- [ ] Ratchet the exact post-task Engine snapshot to
      `33` plain imports / `11` domain imports / `2` re-exports /
      `22` public getter names. The two removed imports are
      `Extrinsic.Runtime.SceneDocument` and
      `Extrinsic.Runtime.EditorCommandHistory`; the three counted getter
      removals are `GetScene`, `GetSceneDocument`, and
      `GetEditorCommandHistory`.
- [ ] Support an independent landing by having Engine/app composition register
      narrow participant adapters for (a) the still-Engine-owned interaction
      cleanup, removed by `RUNTIME-188`, and (b) the current asset,
      render-extraction, and object-space-normal-bake handoff cleanup, removed
      by `RUNTIME-183`. Each adapter may call only the existing narrow cleanup
      operations needed at the replacement boundary and must be retired by its
      named owner. Do not create or retain a broad cleanup aggregate,
      compatibility owner, back-reference, or untracked migration surface.
      Pairing these implementations atomically remains valid.

## Tests

- [ ] Add focused module contract coverage for exact publication and
      withdrawal of `SceneDocumentModule` and `EditorCommandHistory`,
      duplicate-publication conflict, partial-registration rollback,
      shutdown/reinitialize, optional omission, and a real Operational
      `Engine::Run()`.
- [ ] Preserve synchronous save/load/new/close, dirty/history, last-event, and
      queued save/load behavior. Prove missing async service leaves
      synchronous operations operational while queued operations return
      `InvalidState`.
- [ ] Cover replacement-participant registration lifetime, deterministic
      order, exact before/after order, and proof that parse failure invokes no
      callbacks and mutates no registry/path/event/history state.
- [ ] Cover active-world switch, destruction of the former world after the
      switch, unrelated inactive-world destruction, away/back without state
      resurrection, and shutdown/reinitialize with recycled handle bits.
      Do not encode active-world destruction as a reachable success case;
      assert the existing `ResourceBusy` contract where relevant.
- [ ] Prove a world switch resets history, path, and last event, and that a
      stale queued callback targeting the old world, old registry, old epoch,
      or shut-down module cannot mutate the replacement or a retired world.
      Include a lifetime test that completes a queued callback after the
      module object has been destroyed.
- [ ] Migrate existing scene-document, scene-serialization,
      editor-command-history, hierarchy, runtime-module, and Sandbox callers
      to exact service resolution and run the focused and complete
      CPU-supported gates.

## Docs

- [ ] Update the runtime README, runtime architecture, ADR-0027 current-state
      notes, kernel target-state, task graph/indexes, and Sandbox documentation
      with `SceneDocumentModule`, its one-world binding, optional async
      capability, and replacement-participant contract.
- [ ] Update the `RUNTIME-188` and `RUNTIME-183` composition descriptions when
      the participant seam lands; regenerate `tasks/SESSION-BRIEF.md`.
- [ ] Regenerate the module inventory.

## Acceptance criteria

- [ ] Engine owns no scene-document/history state, import, initialization,
      teardown, callback, or public facade.
- [ ] The module owns exactly one validated active-world binding; every world
      switch, replacement, retirement, shutdown, and recycled-handle
      reinitialize deterministically clears or rebinds all durable state.
- [ ] New/load/close coordinate the two real external owners through one plain,
      synchronous, lifetime-safe participant contract with no queued-event
      ordering gap.
- [ ] Synchronous document behavior remains Operational without async;
      queued work cannot outlive or target a superseded module/world binding.
- [ ] The exact convergence snapshot is `33/11/2/22`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'SceneDocument|SceneSerialization|EditorCommandHistory|RuntimeModule|RuntimeSandboxAcceptance|ECSHierarchy' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes

- Retaining `SceneDocument`, `EditorCommandHistory`, or a replacement cleanup
  aggregate as Engine-private ownership or compatibility getters.
- Adding a combined `SceneEditingModule`, generic replacement event bus,
  interface hierarchy, bridge, queue, service bundle, or one wrapper per
  existing state owner.
- Depending on camera, UI/capture, selection/readback, gizmo, render
  extraction, asset/GPU state, `RUNTIME-180`, or `RUNTIME-182`.
- Using delayed event delivery as the only world-validity check, retaining
  raw-`this` async callbacks, or allowing an old task to commit after
  replacement/shutdown.
- Retaining a per-world document/history map or resurrecting old state.
- Publishing partial hierarchy results after a `HARDEN-086` query failure.
- Changing serialization, scene edit, or command-history semantics during the
  ownership move.

## Maturity

- Target: `Operational`; the composed owner must run through the canonical
  Engine scene-document path, not only direct service/contract tests.
