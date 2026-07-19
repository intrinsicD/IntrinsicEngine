---
id: RUNTIME-129
theme: B
depends_on:
  - GRAPHICS-104
  - GRAPHICS-115
  - GRAPHICS-128
  - RUNTIME-137
  - RUNTIME-183
maturity_target: Operational
---
# RUNTIME-129 — Schedule GPU object-space normal bake jobs after import

## Status

- Selectable as of 2026-07-19: all front-matter dependencies are retired.
  Production provider, exact cache-generation contracts, remaining Sandbox
  producer, and capable-host Vulkan smoke remain open inside the accepted
  `AssetWorkflowModule` owner.

## Goal
- After a mesh is imported and its vertex normals are resolved, schedule an asynchronous GPU object-space normal-texture bake, and once the GPU result is `Ready` swap the material's normal binding to the generated texture; until then keep rendering with the vertex-normal attribute.

## Non-goals
- The graphics-owned RHI bake pass, shaders, and `GpuAssetCache` GPU-produced texture residency — owned by GRAPHICS-104 (this task consumes them).
- The graphics-owned nonzero shared-index-slice command contract — owned by
  `GRAPHICS-128` (this task consumes `FirstIndex` from the selected live
  `GpuGeometryRecord`).
- GPU dilation for padded object-space normal bakes — already owned and retired
  by `GRAPHICS-115`; this task consumes the graphics-owned dilation resources
  when runtime scheduling asks for padded bakes.
- Tangent-space normal-map baking or MikkTSpace tangents.
- GPU porting of scalar/label/vector/face/selected-mesh attribute bakes.
- A CPU fallback for GPU bake failure on a non-operational backend (fail closed; keep the existing vertex-normal shading).
- Changing default material assignment (RUNTIME-128) or vertex-normal computation (`ResolveVertexNormals`).

## Context
- Owner/layer: `runtime` for job orchestration, generated `AssetId` selection, stale-generation keys, render-thread submission timing, and material-binding swap; `graphics` owns the bake command recorder (`Extrinsic.Graphics.ObjectSpaceNormalTextureBake`, `RecordObjectSpaceNormalTextureBake(...)` at `src/graphics/renderer/Graphics.ObjectSpaceNormalTextureBake.cppm:168`) and the cache residency path (`GpuAssetCache::BeginGpuProducedTexture(...)` / `SetGpuProducedTextureReadyFrame(...)`).
- `GRAPHICS-104` is retired at `CPUContracted`: zero-padding raster-bake
  planning/recording, shader/material metadata, GPU-produced texture residency,
  and runtime queue/submission/binding helpers exist. `GRAPHICS-115` is retired
  at `Operational`: requested padding is submittable when the runtime provides
  graphics-owned dilation resources, and still fails closed when those resources
  are unavailable. Retired `GRAPHICS-128` now makes the bake plan/record
  descriptors preserve `GpuGeometryRecord::SurfaceFirstIndex` while keeping
  indexed-draw base vertex zero; retired `RUNTIME-183` supplies the required
  AssetWorkflow composition owner.
- Current state: `Extrinsic.Runtime.ObjectSpaceNormalBakeQueue` owns the
  CPU-contract scheduling metadata for generated texture `AssetId` selection,
  content-key reuse, stale-key matching, retained pending submissions, and
  non-operational no-op behavior.
  `Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission` validates queued stale
  keys against graphics bake plans, registers cache-owned GPU-produced
  textures, returns record descriptors for render-thread command recording,
  and attaches submitted ready-frame values without completing the queue
  early. `Extrinsic.Runtime.ObjectSpaceNormalBakeBinding` consumes completions
  only after `GpuAssetCache` exposes a ready generated texture view, rejects
  stale completions before material mutation, and installs data-only
  `ObjectSpaceNormal` material bindings through `RenderExtractionCache`.
  `Extrinsic.Runtime.ObjectSpaceNormalBakeService` privately owns the queue and
  registers its service-private state as a `JobService` `GpuQueue`
  participant: CPU/null contract tests inject a deterministic graphics plan
  through the explicit service test seam and prove pending-submission drain,
  command recording, cache ready-frame promotion, stale pending discard, and
  material binding without a standalone queue module. `AssetWorkflowModule`
  composes this service and passes its request queue to model-scene handoff
  options, direct-mesh post-import services, and scene-state cleanup.
  `AssetModelSceneHandoff` progressive raw mode can accept a
  `RuntimeObjectSpaceNormalBakeQueue`; when supplied, generated-normal work
  uses a dependency-only main-thread scheduling job to enqueue the runtime GPU
  bake after UV/normal enrichment and leaves the progressive normal slot
  pending. The Sandbox default direct-mesh post-import processor schedules the
  same queue after deferred UV/normal materialization when the workflow
  supplies it; on the default Null backend this records the no-CPU-fallback
  diagnostic and leaves the material normal binding unset.
  `SelectedMeshTextureBakeContext` can carry the same queue: mesh-vertex normal
  target commands enqueue requests with geometry/UV/normal content keys, mark
  the progressive normal slot pending on operational backends, and return the
  queue's no-CPU-fallback diagnostic without creating a CPU texture on
  non-operational backends. The production Vulkan geometry-buffer/pipeline
  plan provider and opt-in GPU smoke are not wired yet. Callers that do not
  supply the queue still use CPU compatibility paths. The derived-job graph
  fail-closes any non-CPU domain:
  `IsUnsupportedJobDomain(domain) { return domain != ProgressiveJobDomain::Cpu; }`
  (`Runtime.DerivedJobGraph.cpp:36-47`, rejection at `:348-355`).
  `ProgressiveJobDomain` already reserves `GpuCompute`/`GpuGraphics`/`Auto`
  (`Runtime.ProgressiveRenderData.cppm:122`).
- The shader fallback already exists: forward/deferred sample the object-space normal texture only when the material flag is set and `NormalID` is valid, otherwise use the vertex-normal attribute (`ResolveSurfaceNormal`). So "use texture when ready, else attribute" needs no shader work — only the runtime swap of the `Normal` binding once the cache entry is `Ready`.
- A GPU graphics bake cannot run on the background streaming `Execute`
  callback (that lane is CPU). It records through the settled render-thread
  `JobService` `GpuQueue` participant and promotes through the GPU-completion
  frame already wired into `GpuAssetCache`.
- Requires an operational Vulkan device for `Operational`; `IDevice::IsOperational()` is false under the default Null backend, so on non-operational hosts this path must no-op deterministically and leave vertex-normal shading in place.
- ARCH-013 re-review (2026-07-08): Decision re-gated and re-scoped. Post
  `ARCH-009`, the render-thread/GPU completion lane for Slice B must consume
  the `JobService` `GpuQueue` target/readback substrate owned by `RUNTIME-137`
  instead of adding another engine-resident GPU queue. The existing
  `RuntimeObjectSpaceNormalBakeQueue` may remain as request/stale-key metadata,
  but submission/completion should route through the kernel job seam, and
  "bake finished → material/attribute refresh" should be a standing kernel
  event reaction rather than an `Engine` callback.
- `RUNTIME-183` is the accepted composition owner. The remaining production
  plan provider, request drain, ready publication, material swap, diagnostics,
  and smoke land inside `AssetWorkflowModule`; this task must not restore
  queue/service state or callbacks on `Engine`.

### Readiness findings (contract amendment 2026-07-19)
- **Full bake identity.** The stable identity is the resolved
  geometry/UV/normal content tuple (`GeometryKey`, `TexcoordKey`, `NormalKey`,
  `VertexCount`, `IndexCount`) plus resolved `Width`, `Height`,
  `PaddingTexels`, and `NormalTextureSpace`. Equality, hashing, submission,
  stale matching, reuse, and failure cleanup must all use this full identity;
  the current content-key map omits the resolved options and the stale key
  carries no content key.
- **Immutable generated identity.** When the full stable identity is available,
  mint or select a collision-safe generated `AssetId` that is immutable for
  that identity. Do not reuse one entity-scoped `AssetId` for distinct stable
  content versions. The entity-scoped generated asset remains only the
  fail-closed fallback when a stable identity is unavailable.
- **Pending versus proven-ready reuse.** `Schedule(...)` currently publishes a
  content-key mapping before any cache allocation or successful record, and
  `RecordFrameCommands(...)` calls `TryBindReadyObjectSpaceNormalBake(...)` for
  every selection kind. Replace that shortcut with explicit provenance:
  inserted/fallback/pending identities are not ready-reuse candidates; only a
  mapping proven `Ready` for the same full identity and cache generation may
  bind without recording.
- **Cache-generation correctness.** `GpuAssetCache::GetView(...)` deliberately
  exposes the previous current view while a replacement is pending. Therefore
  an in-flight or reused bake may bind only when the texture view generation
  equals the ticket/proven-ready generation. Ready-frame publication must not
  affect a different pending generation; add a generation-aware publication
  operation or otherwise make that exact-generation invariant testable.
  Record failure, ready-publication failure, stale discard, and shutdown must
  invalidate the matching pending reuse provenance as well as fail the pending
  cache generation.
- **Real source generations.** `AssetModelSceneHandoff` and
  `SandboxDefaultPolicies` currently hardcode geometry/texcoord/normal
  generations to `1`; selected-mesh paths derive property generations but
  still use stable identity as entity generation. Producers must derive source
  and entity-lifetime generations from content/dirty/lifetime facts so a new
  version of the same stable entity cannot reproduce the prior stale key.
- **Production provider inventory.** After `RUNTIME-183`, extend the private
  `ObjectSpaceNormalBakeService` dependencies with the resolved
  `Graphics::IRenderer`; build the production plan lambda in
  `Impl::ConfigureDependencies()` rather than exporting another provider
  interface or restoring an `Engine` callback. At record time, resolve
  `RenderExtractionCache::FindGpuRenderableAvailability(stableEntityId)`,
  require a resident `Surface.Geometry`, re-resolve it through
  `renderer.GetGpuWorld().TryGetGeometryRecord(...)`, validate the managed
  index buffer, `SurfaceIndexCount`, `VertexCount`, local texcoord/normal BDAs,
  and the full latest identity, then build the graphics plan with
  `FirstIndex = SurfaceFirstIndex`, `IndexCount = SurfaceIndexCount`, and base
  vertex zero. Call `GpuWorld::SubmitPendingUploadBarriers(commandContext)`
  before recording the bake.
- **Provider resource lifetime.** Lazily create the raster pipeline on the
  render thread only after the device is operational, retaining it through
  `renderer.GetPipelineManager()`. Retain graphics-owned dilation resources by
  resolved extent instead of creating/destroying them per frame. On shutdown,
  detach the GPU participant/hook, wait for device idle when work or retained
  bake resources exist, clear tickets/provenance, release dilation and pipeline
  leases, and only then allow cache/renderer/device teardown.
- **Remaining producer.**
  `ApplySandboxEditorTextureBakeCommand(...)` constructs a
  `SelectedMeshTextureBakeContext` without the normal-bake queue or
  operational flag, while `SandboxEditorContext` exposes neither and rejects a
  missing `AssetService` before the queue-backed normal path can run. Wire the
  `AssetWorkflowModule` queue and device-operational fact into this facade; a
  queue-backed mesh-vertex normal request must not require a CPU
  `AssetService`, while non-normal compatibility bakes still do.
- **Implementation order.** Retired `GRAPHICS-128` closed the graphics
  prerequisite and retired `RUNTIME-183` supplied the AssetWorkflow owner.
  Land full identity/stale/cache-generation CPU contracts before the private
  production provider, remaining editor producer, and final GPU smoke; do not
  put the provider back on `Engine`.
- **Right-sizing verdict.** Extending the existing
  `ObjectSpaceNormalBakeServiceDependencies` is justified by the runtime →
  graphics composition seam and render-thread/resource-lifetime correctness.
  The simplest implementation is one borrowed renderer dependency, one private
  plan lambda, and private retained resource state in the existing service;
  no new `*Provider`, facade, module, queue, binding, or submission unit is
  warranted. The blast radius is limited to the existing
  `ObjectSpaceNormalBake{Queue,Submission,Binding,Service}` modules, their
  producers/composition, and focused tests. Reconsider a provider interface
  only when a second production plan source with different policy exists.

### Settled policy
- **GPU job lane.** Keep `DerivedJobGraph` CPU-only and route render-thread GPU
  bake submission/completion through the `JobService` `GpuQueue` target owned
  by `RUNTIME-137`, while preserving stale-key/generation bookkeeping as bake
  request state.
- **Bake trigger scope.** Default: schedule a bake only for primitives that currently receive a generated object-space normal (i.e. the cases the CPU path bakes today), not for every imported mesh with authored tangent-space normals.
- **Content-key reuse.** Key completed bakes by the full stable bake identity
  above, with an entity-scoped generated `AssetId` fallback only when no stable
  identity exists.

## Required changes
- [ ] Add a runtime render-thread GPU bake submission step that drains queued object-space normal bake requests through the `JobService` `GpuQueue` target from `RUNTIME-137`, calls the graphics-owned bake API to record/submit, and registers the GPU-produced texture with `GpuAssetCache` for fence-driven `Ready` promotion. Gate the whole step on `RHI::IDevice::IsOperational()`; no-op deterministically otherwise.
- [ ] Replace partial content-key reuse with the full resolved bake identity;
      carry it through request, submission, stale/latest matching, cache
      provenance, completion, and failure cleanup.
- [ ] Select an immutable collision-safe generated `AssetId` per full stable
      identity; use an entity-scoped fallback only when no stable identity is
      available.
- [ ] Track pending and proven-ready reuse separately, including the exact
      `GpuAssetCache` generation. Do not fast-bind inserted, fallback, or
      pending-reuse submissions, and require the observed texture view
      generation to equal the submitted/proven-ready generation before
      material mutation.
- [ ] Make ready-frame publication generation-aware (or enforce an equivalently
      testable exact-generation invariant) and purge matching pending reuse
      provenance on record failure, ready-publication failure, stale discard,
      and shutdown.
- [ ] Replace hardcoded/reused producer source generations with content/dirty
      and entity-lifetime generations for model-scene, direct-mesh, and
      selected-mesh scheduling.
- [ ] Implement the production Vulkan geometry-buffer/pipeline/dilation plan
      provider and completion reaction as private `ObjectSpaceNormalBakeService`
      state inside `AssetWorkflowModule`: resolve the live extraction surface
      and `GpuWorld` record immediately before recording, consume
      `GRAPHICS-128`'s `SurfaceFirstIndex`, submit pending managed-upload
      barriers, retain render-thread-created pipeline/dilation resources, and
      add no `Engine` callback, exported provider interface, or test facade.
- [x] Slice B partial: add service-private `ObjectSpaceNormalBakeService` state as the `JobService` `GpuQueue` participant owner, retain pending queue submissions, record injected graphics bake plans through `RecordObjectSpaceNormalTextureBake(...)`, stamp `GpuAssetCache` ready frames, drain ready completions into material bindings, and discard superseded pending submissions before recording.
- [x] Add an `AssetModelSceneHandoff` queue option so progressive model-scene generated-normal work schedules `RuntimeObjectSpaceNormalBakeQueue` through a dependency-only main-thread apply job instead of marking a fake CPU normal texture ready when the queue is supplied.
- [x] Wire the then-Engine-owned queue services into model-scene handoff options and direct-mesh post-import processors; `RUNTIME-183` moves that unchanged wiring into `AssetWorkflowModule`. Direct mesh post-process schedules a queue request from resolved ECS geometry/UV/normal properties instead of registering the CPU-generated texture when the queue is supplied.
- [x] Add a `SelectedMeshTextureBake` queue option so selected mesh-vertex normal outputs schedule `RuntimeObjectSpaceNormalBakeQueue` requests instead of creating CPU-generated normal textures when the queue is supplied.
- [ ] Wire `ApplySandboxEditorTextureBakeCommand(...)` through
      `SandboxEditorContext` to the `AssetWorkflowModule` queue and operational
      flag; queue-backed mesh-vertex normal requests must not require
      `AssetService`, while other CPU compatibility bakes retain that
      requirement.
- [ ] On bake completion (`GpuAssetCache` entry `Ready` at the exact expected
      generation), swap the material's `Normal` binding to the generated
      `AssetId` and set the `ObjectSpaceNormalMap` material flag; discard stale
      completions whose full identity or recorded generations no longer match.
- [x] Route the chosen GPU lane through `JobService` `GpuQueue` without
      regressing CPU job fail-closed behavior for unimplemented
      `DerivedJobGraph` domains.
- [ ] Retain the CPU generated-normal path only as legacy compatibility behind the operational check until the GPU path is proven, then route import/enrichment through the GPU path.

## Tests
- [x] CPU/null contract: queue-level scheduling on a non-operational backend no-ops deterministically and emits a no-CPU-fallback diagnostic.
- [x] CPU/null contract: queue-level stale-key lifecycle rejects a completion whose recorded entity/source generation, resolution, padding, or normal-map type no longer matches.
- [x] CPU/null contract: generated `AssetId` selection and content-key reuse return the same id for identical resolved geometry/UV/normal inputs.
- [x] CPU/null contract: model-scene handoff queue scheduling replaces the fake CPU normal-bake readiness job when configured and leaves the progressive normal slot pending.
- [x] CPU/null contract: model-scene handoff queue scheduling on a non-operational backend leaves the material `Normal` binding unset and records the no-CPU-fallback diagnostic.
- [x] CPU/null contract: composed non-operational direct-mesh import scheduling leaves the vertex-normal binding and no `ObjectSpaceNormalMap` flag in place.
- [x] CPU/null contract: selected-mesh normal texture command schedules an object-space normal bake queue request without creating a CPU texture when a queue is supplied.
- [x] CPU/null contract: selected-mesh normal texture command on a non-operational backend records the no-CPU-fallback diagnostic and leaves the progressive normal binding unchanged.
- [x] CPU/null contract: `JobService` `GpuQueue` participant records an injected object-space normal bake plan, promotes the generated cache entry by ready frame, drains the ready completion, and installs the generated normal binding.
- [x] CPU/null contract: superseded pending object-space normal bake submissions are discarded before command recording and do not allocate GPU-produced texture cache entries.
- [x] CPU/null contract: content-key reuse binds an already-ready generated normal texture without recording a duplicate bake or allocating the unused entity-scoped fallback texture.
- [ ] CPU/null contract: module-level stale completion does not mutate the material binding.
- [ ] CPU/null contract: the same resolved content with different width,
      height, padding, or normal space does not alias one bake identity or
      generated `AssetId`.
- [ ] CPU/null contract: changed content for the same stable entity with an
      older `Ready` cache view does not fast-bind the old pixels as the new
      bake.
- [ ] CPU/null contract: a pending replacement exposes the old current cache
      view but remains unbound until `GpuAssetView::Generation` equals the
      ticket generation; ready publication for a different generation is
      rejected.
- [ ] CPU/null contract: inserted/pending reuse becomes proven-ready only after
      the matching cache generation promotes; record/ready failure, stale
      completion, and shutdown remove its pending provenance without mutating
      material binding or a newer identity.
- [ ] CPU/null contract: a proven-ready full-identity reuse binds without a
      duplicate bake, while entity fallback and first insertion never take
      that shortcut.
- [ ] CPU/null contract: model-scene, direct-mesh, and selected-mesh producers
      change source/entity-lifetime generations when their actual input version
      changes.
- [ ] CPU/null contract: the Sandbox editor mesh-vertex normal command supplies
      the queue and operational flag without requiring `AssetService`; a
      non-normal CPU bake still fails without `AssetService`.
- [ ] CPU/source contract: runtime frame-command hooks remain on a
      graphics-capable final command context suitable for the offscreen raster
      bake, with no second frame acquire/present path.
- [ ] Opt-in `gpu;vulkan` smoke on a Vulkan-capable host: import a decoy geometry
      before the target, assert the target surface has nonzero
      `SurfaceFirstIndex`, schedule its normal bake through the production
      provider, wait beyond `FramesInFlight`, and prove the expected cache
      generation is `Ready`, the material binding/normal-space flag swapped,
      and covered plus dilation-gutter texels encode the target object-space
      normal rather than the decoy.

## Docs
- [x] Update `src/runtime/README.md` for queue-level GPU bake scheduling metadata, stale-key lifecycle, and the non-operational no-op contract.
- [x] Update `src/runtime/README.md` for optional `AssetModelSceneHandoff` queue scheduling and the deferred binding/submission boundary.
- [x] Update `src/runtime/README.md` for the pre-`RUNTIME-183` queue services and direct-mesh post-import queue scheduling.
- [x] Update `src/runtime/README.md` for optional `SelectedMeshTextureBake` queue scheduling and the non-operational no-CPU-fallback contract.
- [x] Update `src/runtime/README.md` for the CPU-contracted `JobService` GPU-queue participant, pending-submission retention, command-recording plan-provider seam, ready-frame promotion, and material-binding drain.
- [ ] Update `src/runtime/README.md` for production Vulkan geometry-buffer plan-provider wiring once landed.
- [ ] Update `src/runtime/README.md` for full bake identity, immutable generated
      IDs, pending/proven-ready reuse, exact cache-generation binding, failure
      cleanup, and the Sandbox editor producer.
- [ ] Update `src/graphics/renderer/README.md` / `src/graphics/assets/README.md` only if the consumed graphics bake API surface changes.
- [x] Regenerate `docs/api/generated/module_inventory.md` if any `.cppm` module surfaces are added or changed.

## Acceptance criteria
- [ ] Imported meshes that take the generated object-space normal path schedule a GPU bake after normal resolution and continue rendering with vertex normals until it completes.
- [ ] On completion the material binds the GPU-resident generated texture and sets the object-space normal flag; stale completions are discarded.
- [ ] Distinct full bake identities cannot alias one stable generated asset, and
      no old/pending cache generation can be bound as a newer bake.
- [ ] The production provider consumes a live nonzero shared-index slice with
      local texcoord/normal BDAs and zero base vertex.
- [ ] The Sandbox editor's queue-backed normal command reaches the same runtime
      path without requiring a CPU texture payload.
- [ ] Non-operational graphics backends run no CPU fallback and keep vertex-normal shading with a deterministic diagnostic.
- [ ] No layering violations (graphics-owned bake stays free of live ECS/runtime/AssetService knowledge; `Vk*` types do not cross RHI/renderer/runtime APIs).
- [ ] `Operational` cited by an actually-run `gpu;vulkan` smoke; CPU contract gate green for the orchestration logic.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational (Vulkan-capable host only):
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes
- Adding a CPU bake fallback for GPU bake failure or non-operational backends.
- Blocking asset import on GPU bake completion.
- Passing `Vk*` types through RHI/renderer/runtime/cache public APIs.
- Adding live ECS/runtime/AssetService knowledge to graphics-owned bake modules.
- Treating ordinary glTF tangent-space normal textures as object-space normal maps.
- Mixing this orchestration with unrelated renderer/runtime/asset features.
- Restoring object-space-normal bake ownership, diagnostics, or callbacks on
  `Engine` after `AssetWorkflowModule` owns the workflow.
- Treating a content-key insertion or entity fallback as proof that a generated
  cache view is ready for the requested full bake identity.
- Binding a `GpuAssetView` whose generation differs from the submitted or
  proven-ready bake generation.
- Creating/destroying raster or dilation resources per frame or releasing them
  while GPU work may still reference them.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for the scheduling/stale-key/fail-closed orchestration contract on CPU/null.
- Slice A.1 (CPUContracted, landed in `GRAPHICS-104`): queue-level scheduling decision, generated-`AssetId`/content-key selection, stale-key lifecycle, and non-operational no-op — all CPU/null tested with bake submission deferred behind the operational check.
- Slice A.2 (CPUContracted, in progress): wire import-generated-normal producers to enqueue GPU bake requests instead of the CPU-domain hardcode, while still deferring render-thread submission to Slice B. The model-scene progressive handoff queue option, pre-`RUNTIME-183` direct-mesh queue scheduling, and selected-mesh command queue option are landed; the Sandbox editor facade remains open.
- Slice B.1 (CPUContracted): correct full bake identity, generated-asset
  immutability, stale/source generations, pending-versus-ready reuse, and exact
  cache-generation binding/failure cleanup before production Vulkan resources
  can make the latent old-view alias observable.
- Slice B.2 (CPUContracted): render-thread production plan provider + cache registration wired behind `IsOperational()`, with retained pipeline/dilation resources and material-binding swap-on-exact-`Ready` generation; CPU contract proves the provider and swap with deterministic resources.
- Current Slice B state (2026-07-16): `CPUContracted` participant substrate landed and is owned by `ObjectSpaceNormalBakeService` private state. It registers through `JobService` `GpuQueue`, records injected graphics bake plans in the renderer command context, stamps `GpuAssetCache` ready frames, drains ready cache entries into `ObjectSpaceNormalBakeBinding`, and discards superseded pending submissions. The production Vulkan plan provider that resolves live geometry buffers/pipeline/dilation resources for imported entities remains open, so this task is not yet `Operational`.
- Slice C (Operational): real Vulkan submission + nonzero shared-index-slice
  `gpu;vulkan` smoke proving an actual baked texture promotes and binds; route
  import/enrichment off the CPU legacy path.

## Slice plan
- **Slice A.1 (landed).** Add `Runtime.ObjectSpaceNormalBakeQueue` for generated-`AssetId`/content-key selection, stale-key records, stale-completion discard, and non-operational no-op diagnostics. Preserves CPU gate and does not mutate materials.
- **Prerequisite order.** Retired `GRAPHICS-128` and `RUNTIME-183` are
  satisfied. Add production provider state only inside the private
  AssetWorkflow bake service, never on `Engine`.
- **Slice A.2.** Replace the CPU-domain hardcode with a GPU bake request for generated-normal primitives. Preserves CPU gate. Defers all render-thread submission to Slice B. Landed so far: optional `AssetModelSceneHandoff` queue scheduling, the pre-`RUNTIME-183` queue wiring, direct-mesh post-import queue scheduling, and selected-mesh command queue scheduling plus CPU/null tests for queued and non-operational backends. Remaining: Sandbox editor facade queue/operational wiring.
- **Slice B.1.** Make full bake identity and exact cache-generation provenance
  authoritative across scheduling, stale matching, submission, completion,
  failure, and proven-ready reuse. Replace hardcoded producer generations.
- **Slice B.2.** Inside `AssetWorkflowModule`, wire the private production plan
  provider to live render-extraction/`GpuWorld` surface records and
  `GRAPHICS-128` `FirstIndex`; retain raster/dilation resources, submit managed
  upload barriers, register the pending cache generation, and bind only that
  generation after `Ready`.
- **Slice C.** Operational Vulkan submission + opt-in `gpu;vulkan` smoke with
  a preceding decoy geometry and target nonzero shared-index slice; switch
  import/enrichment generated-normal use cases off the CPU legacy path. Cites
  an actually-run Vulkan smoke.
