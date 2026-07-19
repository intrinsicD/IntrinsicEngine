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

- 2026-07-19 contract amendment: asset composition now resolves the audited
  `SceneDocumentModule` and `SceneInteractionModule` split and participates in
  document replacement through `RUNTIME-172`'s synchronous narrow contract.
  Implementation remains open.

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

## Context
- Owner/layer: `runtime` owns cross-layer asset composition.
- `AssetService`, GPU cache/residency, import queue, and normal-bake requests
  share one app lifecycle and one import-to-visible-result consumer contract.
- Durable asset, residency, import-queue, and bake-request state is global.
  Scene handoffs are borrowed active-world attachments and must be destroyed
  before a world leaves service, then rebound to the new active world.
- The module resolves `AsyncWorkModule`, `SceneDocumentModule`,
  `SceneInteractionModule`, `CameraModule`, and `ConfigControlModule`.
  `SceneDocumentModule` exposes the synchronous replacement-participant
  contract; interaction services are resolved separately and never repackaged
  with document/history state.
- Reverse name-sorted module teardown is not a safe lifetime dependency. On
  `RuntimeShutdownAnnounced`, this module releases its document replacement
  participant and detaches scene-interaction borrows while providers are still
  live, before ordinary reverse shutdown.

## Required changes
- [ ] Add one concrete `AssetWorkflowModule` owning `AssetService`, private
      asset residency state, `AssetImportPipeline`, and
      `ObjectSpaceNormalBakeService`.
- [ ] Resolve async, scene-document, scene-interaction, camera, config, device,
      renderer, render-extraction, and `JobService` capabilities during module
      boot.
- [ ] For each cross-owner dependency, choose either an explicit narrow
      non-owning construction capability with a declared lifetime or typed
      service discovery. If `Require`/`OnResolve` is used, add a real
      missing-provider boot test; otherwise leave those zero-consumer methods
      for `RUNTIME-185` to remove.
- [ ] Publish the existing `AssetService`, `AssetImportPipeline`, and GPU-cache
      capabilities directly through `ServiceRegistry`; do not wrap them in
      one-line forwarding services.
- [ ] Move asset ticks, handoff maintenance, import cancellation, GPU
      participant registration/drain, and teardown into module hooks/lifecycle.
- [ ] Register one narrow `SceneDocumentModule` replacement participant that
      destroys asset/render-extraction/bake handoffs while the outgoing
      registry is live and rebinds them after successful replacement. Release
      the strong participant handle during shutdown announcement; do not rely
      on delayed replacement events or module-name teardown order.
- [ ] Remove Engine asset/import/cache/normal-bake state and
      `GetAssetService`, `GetGpuAssetCache`, `GetAssetImportPipeline`, and
      object-space-normal diagnostic test getters.
- [ ] Leave the production Vulkan plan provider and smoke explicitly to
      `RUNTIME-129`, now owned inside this module.

## Tests
- [ ] Preserve asset import, residency, model handoff, generated-texture,
      non-operational normal-bake, stale-generation, shutdown, and reinitialize
      coverage through the module.
- [ ] Add active-world replacement coverage proving old handoffs are destroyed
      before new ones bind.
- [ ] Cover the chosen cross-owner wiring failure contract without making
      module registration order load-bearing.
- [ ] Run focused asset/runtime/graphics CPU coverage, strict layering, and the
      complete default CPU-supported gate.

## Docs
- [ ] Update runtime, assets, and graphics ownership documentation and the
      kernel target-state state-scope row.
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Engine owns no asset/import/residency/normal-bake domain state, import, or
      facade.
- [ ] Global owners and borrowed per-world handoffs have explicit, tested
      lifetimes.
- [ ] Existing import-to-render behavior remains Operational; normal-bake GPU
      `Operational` is still owned by `RUNTIME-129`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicAssetUnitTests IntrinsicGraphicsAssetsUnitTests IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'AssetImport|AssetResidency|AssetModel|ObjectSpaceNormalBake|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Adding wrappers for `AssetService`, `AssetImportPipeline`, GPU cache, or the
  normal-bake service solely to satisfy module naming.
- Moving runtime composition into graphics/assets or app.
- Claiming Vulkan normal-bake operation without the `RUNTIME-129` smoke.

## Maturity
- Target: `Operational` for the existing asset/import/runtime path.
- Object-space normal-bake GPU `Operational` owned by `RUNTIME-129`.
