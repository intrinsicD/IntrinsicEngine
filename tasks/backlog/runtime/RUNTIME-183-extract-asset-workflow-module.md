---
id: RUNTIME-183
theme: F
depends_on:
  - RUNTIME-172
  - RUNTIME-179
  - RUNTIME-180
  - RUNTIME-181
  - RUNTIME-188
maturity_target: Operational
---
# RUNTIME-183 — Extract the asset-workflow composition module

## Status

- Selectable as of 2026-07-19: `RUNTIME-172`, `RUNTIME-179`, `RUNTIME-180`,
  `RUNTIME-181`, and `RUNTIME-188` are all retired. Implementation remains
  open; the next gate is the bounded PImpl asset-owner slice below.
- 2026-07-19 contract amendment: asset composition now resolves the audited
  `SceneDocumentModule` and `SceneInteractionModule` split and participates in
  document replacement through `RUNTIME-172`'s synchronous narrow contract.
  Implementation remains open.
- 2026-07-19 composition evidence: `RUNTIME-172` lands the temporary
  `RUNTIME-183.EngineAssetHandoffTransition`. It captures only the exact
  long-lived residency, asset, renderer/device, extraction, bake, import,
  world, selection, async/config, initialized-state, and history objects; its
  registry/world come only from the replacement context. This task must absorb
  that participant and release its retained handle during shutdown
  announcement; document quiescence permits exact-handle detachment before
  provider teardown.
- 2026-07-19 readiness amendment: the retained/released `RUNTIME-172`
  participant handle is on `main`, and retired `RUNTIME-188` supplies the exact
  optional `SelectionController` service while leaving only this task's
  implementation-local Engine borrow. The audited destination uses the existing
  shutdown event, `IAssetFrameHooks`, and `JobServiceGpuQueueBridge`; removes
  stale camera and config-control-wrapper dependencies; and fixes the exact
  post-`RUNTIME-188` convergence target below.
- 2026-07-19 implementation-preflight amendment: persistent PImpl construction
  is distinguished from per-boot provider-dependent construction; direct
  imports and model-scene handoff callbacks receive explicit binding
  validation; rollback is specified as a no-partial-state invariant; provider
  omission diagnostics use real production instances; and the one production
  shutdown order is separated from direct-lifecycle order-safety probes.

## Goal
- Move CPU asset authority, GPU residency, import orchestration, scene
  handoffs, and object-space-normal bake ownership out of `Runtime.Engine`
  into one app-composed `AssetWorkflowModule`.

## Non-goals
- No asset format, import, residency, material-binding, or bake behavior change.
- No new asset facade per constituent service and no second import pipeline.
- No claim that the production Vulkan normal bake is operational; that proof
  remains `RUNTIME-129`.
- No live asset-service traffic in graphics or app ownership of lower layers.
- No new shutdown event/interface, generic frame phase, asset-specific Engine
  lifecycle callback, GPU command hook/bridge, or wrapper around an existing
  capability.
- No `AssetWorkflowModule` ownership of camera controllers, live config-control
  state, scene document/history, scene interaction, render-extraction geometry
  residency, `JobService`, renderer, device, or world registries.

## Context
- Owner/layer: `runtime` owns cross-layer asset composition.
- `AssetService`, GPU cache/residency, import queue, and normal-bake requests
  share one app lifecycle and one import-to-visible-result consumer contract.
- The module object, `AssetImportPipeline`, import queue/terminal records, and
  `ObjectSpaceNormalBakeService` have app-global scope across an Engine
  shutdown/reinitialize cycle. Per-boot `AssetService`, GPU cache/listener, and
  borrowed scene handoffs are rebuilt and withdrawn each boot. The persistent
  pipeline and bake service are constructed dependency-empty with the module
  PImpl; required-provider validation precedes only construction/publication of
  per-boot provider-dependent state. Keeping the pipeline object alive also
  keeps its raw-`this` streaming apply/cancellation callbacks valid until the
  optional async owner finishes its drain.
- Scene handoffs are borrowed active-world attachments. Active-world events are
  delayed until the next pump, so handoff/import operations also validate the
  cached `{WorldHandle, Registry*, binding epoch}` directly against
  `WorldRegistry` before use. `AssetService` may flush a model-scene event
  outside `IAssetFrameHooks::TickAssets()`, so module-only pre-tick validation
  is insufficient: the existing model-scene handoff options receive one
  optional narrow binding-validity predicate, and the handoff checks it before
  every callback/materialize/resolve use. The direct import path performs the
  same current-target check before scene mutation; queued apply retains its
  existing frozen-epoch check. Document new/load/close is a different,
  same-registry replacement boundary and uses `RUNTIME-172`'s synchronous
  participant contract.
- Required Engine-lifetime construction capabilities are `JobService`,
  `WorldRegistry`, the active `EngineConfig` pointer already carried by
  `RuntimeRenderRecipeActivationKernel`, and one new read-only initialization-
  state pointer/reference on `EngineSetup`. Required typed services are
  `RHI::IDevice`, `Graphics::IRenderer`, `RenderExtractionCache`,
  `SceneDocumentModule`, and its exact `EditorCommandHistory`.
- Optional typed services are `StreamingExecutor` and the exact
  `SelectionController` published by `RUNTIME-188`. Their concrete owner modules
  are never dependencies: no `AsyncWorkModule`, `SceneInteractionModule`,
  `CameraModule`, `EngineConfigControl`, or invented `ConfigControlModule`
  pointer is retained. Missing streaming disables queued imports; missing
  selection disables import-completion selection UX without blocking
  materialization.
- `RUNTIME-188` removed Engine interaction ownership and left only the named
  `RUNTIME-183` transition. This task removes that transition, resolves the
  optional exact selection service, and must not recreate an Engine-side
  interaction borrow.
- Reverse name-sorted module teardown is not a safe lifetime dependency. Split
  the current announce-and-reverse-shutdown helper so the existing
  `RuntimeShutdownAnnounced` event is pumped after command discard and before
  application, GPU-participant, async, document, interaction, renderer, or
  device teardown. The AssetWorkflow listener first stops/cancels imports, then
  invalidates scene bindings, releases its document participant, and detaches
  provider borrows. It does not destroy bake/cache state at announcement.
  With the fixed production names, ascending registration order places
  `Runtime.AssetWorkflowModule` before `Runtime.AsyncWorkModule`, so the one
  real Engine reverse-shutdown order is AsyncWork then AssetWorkflow. Safety in
  the opposite ordinary-callback order is a direct lifecycle-harness proof
  after the same announcement/GPU-participant boundary, not a second claimed
  Engine order and not something app insertion order can change.
- `ObjectSpaceNormalBakeService` remains one `JobService` `GpuQueue`
  participant. The module owns registration and callback state; the existing
  Engine/kernel `JobServiceGpuQueueBridge` remains the sole renderer command
  hook, completion drain, reverse participant shutdown, and conditional
  device-idle coordinator.
- The exact post-`RUNTIME-188` Engine baseline is `26` plain imports / `4`
  domain imports / `2` re-exports / `15` public getter names.

## Required changes
- [ ] Add only `src/runtime/Runtime.AssetWorkflowModule.cppm` and matching
      `.cpp` as one concrete
      `AssetWorkflowModule final : IRuntimeModule, Core::IAssetFrameHooks` with
      a PImpl. Fold `Runtime.AssetResidencyService.Internal.hpp` into that
      private implementation and delete it; add no exported residency service,
      constituent wrapper, or third workflow file.
- [ ] Keep one persistent `AssetImportPipeline` and
      `ObjectSpaceNormalBakeService` in the module PImpl. Recreate per-boot
      `AssetService`, GPU cache/listener, and model texture/scene handoffs;
      preserve cancelled import records and keep the pipeline object alive
      through optional `StreamingExecutor::ShutdownAndDrain()` independent of
      ordinary callback order. Construct the persistent objects dependency-empty
      with the PImpl; do not defer their object lifetime to per-boot provider
      discovery.
- [ ] Publish the exact existing `RHI::IDevice` as an Engine built-in service
      before module registration, alongside the existing renderer and
      render-extraction services. Consume `JobService`, `WorldRegistry`, active
      config, and initialization state through declared Engine-lifetime
      `EngineSetup` construction borrows; add no wrapper service for them.
- [ ] During registration, validate the required built-in device, renderer, and
      render-extraction services before constructing/publishing per-boot
      provider-dependent state. During order-independent `OnResolve`, `Require`
      the exact `SceneDocumentModule` and `EditorCommandHistory`. Cover omission
      diagnostics through three direct module boot-lifecycle cases: omit the
      device while using the real remaining built-ins; omit
      `SceneDocumentModule`; then register/publish the real
      `SceneDocumentModule` and exact-withdraw its real
      `EditorCommandHistory` instance before `AssetWorkflowModule::OnResolve`
      to exercise the independent missing-history diagnostic. Do not add a
      test-only provider, Engine suppression switch, or wrapper service.
- [ ] During `OnResolve`, optionally `Find<StreamingExecutor>()` and
      `Find<SelectionController>()`. Direct imports remain Operational without
      streaming; queued imports fail closed. Import materialization remains
      Operational without selection/camera, while selection/focus policy
      callbacks degrade through their existing null-service contract.
- [ ] Extend `EngineSetup` with the smallest read-only Engine-lifetime
      initialized-state construction borrow. Continue using
      `RuntimeRenderRecipeActivationKernel::ActiveConfig` for the canonical
      config pointer. Preserve the current gate: import rejects during
      `IApplication::OnInitialize`, succeeds after `Engine::Initialize()`
      completes, and rejects after shutdown announcement.
- [ ] Publish the existing `AssetService`, `AssetImportPipeline`,
      `GpuAssetCache`, and `Core::IAssetFrameHooks` capabilities directly
      through `ServiceRegistry`; withdraw the same exact instances on rollback
      and shutdown. Duplicate-conflict preflight and every later
      registration/resolution failure must leave no partial publication,
      subscription, document participant, or GPU participant. Keep reverse
      exact-withdraw/unregister cleanup for unexpected late failures; do not
      manufacture a mid-`Provide` race that cannot occur during the current
      single-threaded boot merely to exercise a branch. Do not publish the
      private normal-bake service/queue or the module itself solely for tests or
      future `RUNTIME-129` callers.
- [ ] Implement `Core::IAssetFrameHooks::TickAssets()` with the existing
      `AssetService::Tick`, GPU-cache retirement, and pending material-texture
      binding work. Engine resolves that hook in the existing
      `ExecuteMaintenanceContract` asset slot; its local generic composite runs
      the unrelated render-extraction geometry-retirement ticks immediately
      afterward. Do not move asset work to the later generic
      `FramePhase::Maintenance`, and keep transfer/streaming ordering unchanged
      when either optional hook is absent.
- [ ] Register one narrow `SceneDocumentModule` replacement participant that
      destroys asset/render-extraction/bake handoffs while the outgoing
      registry is live and rebinds them after successful replacement. Release
      the retained strong participant handle explicitly during shutdown
      announcement; remove
      `RUNTIME-183.EngineAssetHandoffTransition` completely. Do not rely on
      delayed replacement events, discarded handle values, or module-name
      teardown order.
- [ ] Subscribe to active-world/retirement events and perform the same
      destroy/rebind operation for a real world switch, but retain direct
      world/registry/epoch validation before every tick, handoff callback, and
      direct-import or queued-import apply. Extend only the existing
      `AssetModelSceneHandoffOptions` with the optional narrow validity
      predicate needed to guard direct `AssetService` flushes; add no handoff
      wrapper or third workflow file. Do not route active-world switching
      through the document replacement participant.
- [ ] Split shutdown announcement from ordinary reverse module shutdown. Pump
      the existing `RuntimeShutdownAnnounced` before application/provider/GPU
      teardown. In the AssetWorkflow listener, cancel every active import before
      app policy unregister/provider teardown, advance the pipeline target
      epoch, release the document participant, and detach selection/history/
      scene/streaming/config borrows. Keep the pipeline object alive, and defer
      owned bake/cache/asset destruction to ordinary module shutdown after the
      generic GPU-participant shutdown contract has run.
- [ ] Register `ObjectSpaceNormalBakeService` once per boot with the existing
      `JobService` `GpuQueue`. Keep `JobServiceGpuQueueBridge` Engine-owned as
      the global recorder/drainer/shutdown driver; module teardown only clears
      its participant/dependencies after that driver has unregistered the
      participant and waited idle when required. Add no second render hook,
      queue bridge, or GPU lifecycle.
- [ ] Migrate platform-drop routing, render extraction, Sandbox policies/editor
      facades, and tests to local typed service discovery or already-resolved
      narrow pointers. Engine may resolve the published hook/cache/pipeline at
      the exact generic call site, but stores no asset-domain member or facade.
- [ ] Remove Engine asset/import/cache/normal-bake state and
      `GetAssetService`, `GetGpuAssetCache`, `GetAssetImportPipeline`, and
      object-space-normal diagnostic test getters.
- [ ] Compose the module explicitly in Sandbox after the hard `RUNTIME-188`
      prerequisite. Module omission leaves generic Engine, world, rendering,
      transfer, async, and render-extraction geometry maintenance Operational;
      asset services are absent and platform drops fail closed.
- [ ] Ratchet the exact final Engine snapshot to `22` plain imports / `0`
      domain imports / `2` re-exports / `10` public getter names. Relative to
      the post-`RUNTIME-188` baseline, remove
      `Extrinsic.Asset.Service`, `Extrinsic.Graphics.GpuAssetCache`,
      `Extrinsic.Runtime.AssetImportPipeline`, and
      `Extrinsic.Runtime.ObjectSpaceNormalBakeService`, plus the five counted
      getter names `GetAssetService`, `GetGpuAssetCache`,
      `GetAssetImportPipeline`,
      `GetObjectSpaceNormalBakeQueueDiagnosticsForTest`, and
      `GetPendingObjectSpaceNormalBakeCountForTest`.
- [ ] Leave the production Vulkan plan provider and smoke explicitly to
      `RUNTIME-129`, now owned inside this module.

## Tests
- [ ] Add `tests/contract/runtime/Test.AssetWorkflowModule.cpp` for exact
      publication/withdrawal, duplicate conflicts, failure cleanup with no
      partial boot state, real-provider omission diagnostics,
      registration-order independence, optional streaming/selection omission,
      initialized-state gating, module omission, one real `Engine::Run()`,
      shutdown, and reinitialize.
- [ ] Preserve asset import, residency, model handoff, generated-texture,
      non-operational normal-bake, stale-generation, platform-drop,
      import-selection/history, GPU-cache fallback, shutdown, and reinitialize
      behavior through direct published services. Do not replace removed Engine
      normal-bake diagnostics with another production facade; assert composed
      effects and keep exact service diagnostics in the existing service tests.
- [ ] Add synchronous document new/load/close coverage proving `BeforeReplace`
      destroys handoffs while the outgoing registry is live and `AfterReplace`
      binds the replacement; parse failure invokes neither callback. Prove the
      retained participant is explicitly released during announcement and
      recycled handle bits do not revive it after reinitialize.
- [ ] Separately cover deferred active-world switch, away-and-back binding-epoch
      rejection, and destruction of the former world after the switch. Prove
      handoffs detach/rebind before the former registry leaves service; do not
      describe this as document replacement or attempt to destroy the active
      world.
- [ ] Extend maintenance coverage for
      transfer → streaming drain/apply → asset/cache tick → streaming
      submit/pump, followed by JobService GPU drain and generic module
      maintenance. Prove render-extraction geometry retirement still runs when
      AssetWorkflow or AsyncWork is omitted.
- [ ] Extend the blocked-import shutdown regression so cancellation occurs
      before app policy unregister and every provider teardown, no late apply
      mutates assets/scene/history/selection, the persistent pipeline remains
      valid through async drain, and the real reverse-sorted Engine shutdown
      proves AsyncWork-before-AssetWorkflow. After the same shutdown
      announcement and GPU-participant boundary, use a direct lifecycle harness
      to invoke ordinary `OnShutdown` in both callback orders and prove the
      opposite AssetWorkflow-before-AsyncWork order is also safe; do not present
      app insertion order as changing production order.
- [ ] Preserve `RuntimeJobService` participant record/drain/reverse-shutdown
      tests and add composed registration-exactly-once/reinitialize coverage for
      normal bake. Prove idle wait and service-private cleanup precede cache,
      renderer, and device destruction when work exists.
- [ ] Update the owning regressions rather than creating parallel fixtures:
      `Test.AssetImportFormatCoverage.cpp`,
      `Test.GpuAssetCacheFallbackBootstrap.cpp`,
      `Test.RuntimeWorldRegistry.cpp`,
      `Test.RuntimeFrameLoopContract.cpp`,
      `Test.RuntimeJobService.cpp`,
      `Test.SandboxEditorSceneCommands.cpp`,
      `Test.RuntimeEngineLayering.cpp`, and
      `tests/integration/runtime/Test.RuntimeSandboxAcceptance.cpp`.
      Migrate every current Engine asset/cache/pipeline getter caller to exact
      service discovery and add a source-contract assertion that only the two
      planned AssetWorkflow source files own the extracted composition.
- [ ] Run focused asset/runtime/graphics CPU coverage, strict layering, and the
      complete default CPU-supported gate.

## Docs
- [ ] Update runtime ownership/lifecycle documentation, Sandbox composition,
      ADR-0027 current-state evidence, and the kernel target-state state-scope
      row. Update assets/graphics documentation only where current ownership
      prose becomes false; do not imply a graphics API change owned by
      `RUNTIME-129`.
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Engine owns no asset/import/residency/normal-bake domain state, public
      import, getter, diagnostic, or facade; its exact generic service lookups
      and the global JobService GPU bridge retain no asset ownership.
- [ ] Global owners and borrowed per-world handoffs have explicit, tested
      lifetimes across document replacement, world switch, early shutdown
      announcement, the production reverse-sorted shutdown order, both direct
      lifecycle callback-order probes, and reinitialize.
- [ ] Existing import-to-render behavior remains Operational; normal-bake GPU
      `Operational` is still owned by `RUNTIME-129`.
- [ ] Exact Engine convergence is `22` plain imports / `0` domain imports / `2`
      re-exports / `10` public getter names.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root .
python3 tools/agents/generate_session_brief.py
python3 tools/docs/check_doc_links.py --root .
cmake --preset ci
cmake --build --preset ci --target IntrinsicAssetUnitTests IntrinsicGraphicsAssetsUnitTests IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'AssetWorkflow|AssetImport|GpuAssetCache|AssetModel|ObjectSpaceNormalBake|RuntimeWorldRegistry|RuntimeFrameLoop|RuntimeJobService|Shutdown|ReInitialize|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
git diff --check
```

## Forbidden changes
- Adding wrappers for `AssetService`, `AssetImportPipeline`, GPU cache, or the
  normal-bake service solely to satisfy module naming.
- Moving runtime composition into graphics/assets or app.
- Claiming Vulkan normal-bake operation without the `RUNTIME-129` smoke.
- Requiring camera or live config-control modules for asset import, publishing
  the private normal-bake queue/service for tests or roadmap callers, or adding
  another shutdown/frame/GPU hook.
- Destroying `AssetImportPipeline` while its optional async callbacks can still
  run, relying on reverse lexical module order, or leaving the document
  participant registered after shutdown announcement.

## Maturity
- Target: `Operational` for the existing asset/import/runtime path.
- Object-space normal-bake GPU `Operational` owned by `RUNTIME-129`.
