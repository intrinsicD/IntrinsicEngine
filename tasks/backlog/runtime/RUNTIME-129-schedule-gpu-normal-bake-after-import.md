---
id: RUNTIME-129
theme: B
depends_on:
  - GRAPHICS-104
  - GRAPHICS-115
  - RUNTIME-137
  - RUNTIME-183
maturity_target: Operational
---
# RUNTIME-129 — Schedule GPU object-space normal bake jobs after import

## Goal
- After a mesh is imported and its vertex normals are resolved, schedule an asynchronous GPU object-space normal-texture bake, and once the GPU result is `Ready` swap the material's normal binding to the generated texture; until then keep rendering with the vertex-normal attribute.

## Non-goals
- The graphics-owned RHI bake pass, shaders, and `GpuAssetCache` GPU-produced texture residency — owned by GRAPHICS-104 (this task consumes them).
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
  are unavailable.
- Current state: `Extrinsic.Runtime.ObjectSpaceNormalBakeQueue` owns the CPU-contract scheduling metadata for generated texture `AssetId` selection, content-key reuse, stale-key matching, retained pending submissions, and non-operational no-op behavior. `Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission` validates queued stale keys against graphics bake plans, registers cache-owned GPU-produced textures, returns record descriptors for render-thread command recording, and attaches submitted ready-frame values without completing the queue early. `Extrinsic.Runtime.ObjectSpaceNormalBakeBinding` consumes completions only after `GpuAssetCache` exposes a ready generated texture view, rejects stale completions before material mutation, and installs data-only `ObjectSpaceNormal` material bindings through `RenderExtractionCache`. `Extrinsic.Runtime.ObjectSpaceNormalBakeService` privately owns the queue and registers its service-private state as a `JobService` `GpuQueue` participant: CPU/null contract tests inject a deterministic graphics plan through the explicit service test seam and prove pending-submission drain, command recording, cache ready-frame promotion, stale pending discard, and material binding without a standalone queue module. `Engine` composes this service and passes its request queue to model-scene handoff options, direct-mesh post-import services, and scene runtime-state cleanup. `AssetModelSceneHandoff` progressive raw mode can accept a `RuntimeObjectSpaceNormalBakeQueue`; when supplied, generated-normal work uses a dependency-only main-thread scheduling job to enqueue the runtime GPU-bake queue after UV/normal enrichment and leaves the progressive normal slot pending. The Sandbox default direct-mesh post-import processor schedules the same queue after deferred UV/normal materialization when Engine supplies it; on the default Null backend this records the no-CPU-fallback diagnostic and leaves the material normal binding unset. `SelectedMeshTextureBakeContext` can carry the same queue: mesh-vertex normal target commands enqueue object-space normal bake requests with geometry/UV/normal content keys, mark the progressive normal slot pending on operational backends, and return the queue's no-CPU-fallback diagnostic without creating a CPU texture on non-operational backends. The production Vulkan geometry-buffer/pipeline plan provider and opt-in GPU smoke are not wired yet. Callers that do not supply the queue still use CPU compatibility paths. The derived-job graph fail-closes any non-CPU domain: `IsUnsupportedJobDomain(domain) { return domain != ProgressiveJobDomain::Cpu; }` (`Runtime.DerivedJobGraph.cpp:36-47`, rejection at `:348-355`). `ProgressiveJobDomain` already reserves `GpuCompute`/`GpuGraphics`/`Auto` (`Runtime.ProgressiveRenderData.cppm:122`).
- The shader fallback already exists: forward/deferred sample the object-space normal texture only when the material flag is set and `NormalID` is valid, otherwise use the vertex-normal attribute (`ResolveSurfaceNormal`). So "use texture when ready, else attribute" needs no shader work — only the runtime swap of the `Normal` binding once the cache entry is `Ready`.
- Architectural crux (open design decision — see questions below): a GPU graphics bake cannot run on the existing background streaming `Execute` callback (that lane is CPU). It must record commands and submit on the render thread, and promote via the GPU-completion fence already wired into `GpuAssetCache`.
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

### Open questions (non-blocking — defaults chosen; revisit before Slice C)
- **GPU job lane.** Default after ARCH-013: keep `DerivedJobGraph` CPU-only and
  route render-thread GPU bake submission/completion through the `JobService`
  `GpuQueue` target owned by `RUNTIME-137`, while preserving the existing
  stale-key/generation bookkeeping as bake request state. This avoids creating
  another bespoke render-thread scheduler.
- **Bake trigger scope.** Default: schedule a bake only for primitives that currently receive a generated object-space normal (i.e. the cases the CPU path bakes today), not for every imported mesh with authored tangent-space normals.
- **Content-key reuse.** Default: key completed bakes by resolved geometry/UV/normal content hash where available, with an entity-scoped generated `AssetId` fallback when no stable key exists (mirrors GRAPHICS-104 line 52).

## Required changes
- [ ] Add a runtime render-thread GPU bake submission step that drains queued object-space normal bake requests through the `JobService` `GpuQueue` target from `RUNTIME-137`, calls the graphics-owned bake API to record/submit, and registers the GPU-produced texture with `GpuAssetCache` for fence-driven `Ready` promotion. Gate the whole step on `RHI::IDevice::IsOperational()`; no-op deterministically otherwise.
- [ ] Implement the production Vulkan geometry-buffer/pipeline/dilation plan
      provider and completion reaction inside `AssetWorkflowModule`, using its
      owned residency/import/normal-bake state and no `Engine` callback or
      test facade.
- [x] Slice B partial: add service-private `ObjectSpaceNormalBakeService` state as the `JobService` `GpuQueue` participant owner, retain pending queue submissions, record injected graphics bake plans through `RecordObjectSpaceNormalTextureBake(...)`, stamp `GpuAssetCache` ready frames, drain ready completions into material bindings, and discard superseded pending submissions before recording.
- [x] Add an `AssetModelSceneHandoff` queue option so progressive model-scene generated-normal work schedules `RuntimeObjectSpaceNormalBakeQueue` through a dependency-only main-thread apply job instead of marking a fake CPU normal texture ready when the queue is supplied.
- [x] Wire the then-Engine-owned queue services into model-scene handoff options and direct-mesh post-import processors; `RUNTIME-183` moves that unchanged wiring into `AssetWorkflowModule`. Direct mesh post-process schedules a queue request from resolved ECS geometry/UV/normal properties instead of registering the CPU-generated texture when the queue is supplied.
- [x] Add a `SelectedMeshTextureBake` queue option so selected mesh-vertex normal outputs schedule `RuntimeObjectSpaceNormalBakeQueue` requests instead of creating CPU-generated normal textures when the queue is supplied.
- [ ] Wire any remaining generated-normal producers/callers to supply the queue by default where appropriate, recording stale keys for entity, geometry/UV/normal generation, resolution, padding, and normal-map type.
- [ ] On bake completion (`GpuAssetCache` entry `Ready`), swap the material's `Normal` binding to the generated `AssetId` and set the `ObjectSpaceNormalMap` material flag; discard stale completions whose recorded keys no longer match.
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
- [ ] Opt-in `gpu;vulkan` smoke on a Vulkan-capable host: an imported mesh schedules a bake, the cache entry promotes to `Ready`, the material `Normal` binding swaps to the generated texture, and selected texels match expected encoded object-space normals.

## Docs
- [x] Update `src/runtime/README.md` for queue-level GPU bake scheduling metadata, stale-key lifecycle, and the non-operational no-op contract.
- [x] Update `src/runtime/README.md` for optional `AssetModelSceneHandoff` queue scheduling and the deferred binding/submission boundary.
- [x] Update `src/runtime/README.md` for the pre-`RUNTIME-183` queue services and direct-mesh post-import queue scheduling.
- [x] Update `src/runtime/README.md` for optional `SelectedMeshTextureBake` queue scheduling and the non-operational no-CPU-fallback contract.
- [x] Update `src/runtime/README.md` for the CPU-contracted `JobService` GPU-queue participant, pending-submission retention, command-recording plan-provider seam, ready-frame promotion, and material-binding drain.
- [ ] Update `src/runtime/README.md` for production Vulkan geometry-buffer plan-provider wiring once landed.
- [ ] Update `src/graphics/renderer/README.md` / `src/graphics/assets/README.md` only if the consumed graphics bake API surface changes.
- [x] Regenerate `docs/api/generated/module_inventory.md` if any `.cppm` module surfaces are added or changed.

## Acceptance criteria
- [ ] Imported meshes that take the generated object-space normal path schedule a GPU bake after normal resolution and continue rendering with vertex normals until it completes.
- [ ] On completion the material binds the GPU-resident generated texture and sets the object-space normal flag; stale completions are discarded.
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

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for the scheduling/stale-key/fail-closed orchestration contract on CPU/null.
- Slice A.1 (CPUContracted, landed in `GRAPHICS-104`): queue-level scheduling decision, generated-`AssetId`/content-key selection, stale-key lifecycle, and non-operational no-op — all CPU/null tested with bake submission deferred behind the operational check.
- Slice A.2 (CPUContracted, in progress): wire import-generated-normal producers to enqueue GPU bake requests instead of the CPU-domain hardcode, while still deferring render-thread submission to Slice B. The model-scene progressive handoff queue option, pre-`RUNTIME-183` direct-mesh queue scheduling, and selected-mesh command queue option are landed; default wiring for any remaining generated-normal callers remains open.
- Slice B (CPUContracted): render-thread GPU submission step + cache registration wired behind `IsOperational()`, plus the material-binding swap-on-`Ready` logic; CPU contract proves the swap given a faked `Ready` promotion.
- Current Slice B state (2026-07-16): `CPUContracted` participant substrate landed and is owned by `ObjectSpaceNormalBakeService` private state. It registers through `JobService` `GpuQueue`, records injected graphics bake plans in the renderer command context, stamps `GpuAssetCache` ready frames, drains ready cache entries into `ObjectSpaceNormalBakeBinding`, and discards superseded pending submissions. The production Vulkan plan provider that resolves live geometry buffers/pipeline/dilation resources for imported entities remains open, so this task is not yet `Operational`.
- Slice C (Operational): real Vulkan submission + `gpu;vulkan` smoke proving an actual baked texture promotes and binds; route import/enrichment off the CPU legacy path.

## Slice plan
- **Slice A.1 (landed).** Add `Runtime.ObjectSpaceNormalBakeQueue` for generated-`AssetId`/content-key selection, stale-key records, stale-completion discard, and non-operational no-op diagnostics. Preserves CPU gate and does not mutate materials.
- **Slice A.2.** Replace the CPU-domain hardcode with a GPU bake request for generated-normal primitives. Preserves CPU gate. Defers all render-thread submission to Slice B. Landed so far: optional `AssetModelSceneHandoff` queue scheduling, the pre-`RUNTIME-183` queue wiring, direct-mesh post-import queue scheduling, and selected-mesh command queue scheduling plus CPU/null tests for queued and non-operational backends.
- **Slice B.** Wire `AssetWorkflowModule` import scheduling to the GRAPHICS-104 submission and binding helpers through the `JobService` `GpuQueue` target from `RUNTIME-137`: drain queued work on the render thread, record/submit through the graphics bake API, feed the submitted ready frame into `GpuAssetCache`, and swap the material `Normal` binding on `Ready` with stale-completion discard. CPU contract fakes the `Ready` promotion. Landed so far: the `ObjectSpaceNormalBakeService` private participant substrate and CPU/null service tests. Remaining: production Vulkan geometry-buffer/pipeline/dilation plan provider for imported entities.
- **Slice C.** Operational Vulkan submission + opt-in `gpu;vulkan` smoke; switch import/enrichment generated-normal use cases off the CPU legacy path. Cites an actually-run Vulkan smoke.
