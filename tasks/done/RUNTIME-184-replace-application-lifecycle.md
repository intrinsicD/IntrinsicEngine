---
id: RUNTIME-184
theme: F
depends_on:
  - RUNTIME-168
maturity_target: Operational
---
# RUNTIME-184 — Replace IApplication with explicit app composition

## Status

- Implemented on 2026-07-23 on
  `codex/arch-014-kernel-convergence-program` as the first dependency in the
  `ARCH-014` convergence chain. Engine now has config-only construction and
  two-stage shutdown; Sandbox owns a concrete session and composes every
  production module explicitly in `main()`. Tests use only a test-support
  module bridge.
- Retired at `Operational` after focused runtime/app coverage, the complete
  UBSan CPU selector (2,937/2,937), the complete ASan selector plus a clean
  rebuild/rerun of its four whitespace-sensitive source-shape assertions, and
  the 21/21 Sandbox/object-space-normal Vulkan cohort including shutdown LSan.
- Commit reference: this retirement commit records the implementation, final
  evidence, and generated task/module inventories.
- The implementation inventory confirms one production behavior owner
  (Sandbox), one production frame-pacing wrapper in the app root, and the
  remaining callback implementations are test fixtures. Sandbox fixed and
  variable ticks were no-ops and were deleted; the frame-pacing wrapper is now
  an ordinary app-composed frame-hook module, while real fixture behavior moved
  to a test-only runtime module bridge.
- 2026-07-19 contract amendment: explicit app composition names
  `SceneDocumentModule` and `SceneInteractionModule` as separate capabilities;
  it must not recreate the rejected combined scene-editing owner.
  The explicit Sandbox composition preserves the separate owners.
- 2026-07-19 composition evidence: Sandbox now owns separate persistent
  `SceneDocumentModule` and `SceneInteractionModule` values and registers both
  through the ordinary module list. Interaction omission is supported without
  an Engine facade. `RUNTIME-188` retired that exact interaction owner at
  `Operational`; this task must preserve those exact references when it removes
  `IApplication`.

## Goal
- Remove the Engine-owned `IApplication` callback lifecycle and make each app
  explicitly compose modules, initial content, defaults, run, and teardown
  without passing `Engine&` through an app behavior surface.

## Non-goals
- No residual getter/re-export decision or caller migration; `RUNTIME-186`
  owns that semantic API leaf after this lifecycle migration.
- No Engine PImpl, implementation-import relocation, or exact checker/policy
  ratchet; `RUNTIME-187` owns that final representation-only leaf.
- No new application framework, `InlineModule`, experiment template, service
  locator, or catch-all parts registry.
- No frame order, fixed-step, renderer, input, or Sandbox behavior change.
- No new runtime domain owner.

## Context
- Owner/layer: `app` visibly chooses composition; `runtime` owns Engine and
  module lifecycle. App continues to depend on runtime only.
- Production Sandbox `OnSimTick` and `OnVariableTick` are no-ops, while tests
  use the unrestricted callbacks heavily. Keeping the production interface for
  test convenience violates ADR-0027's present-consumer test.
- `RUNTIME-168` leaves Sandbox defaults as app-owned typed handles over the
  exact published `AssetImportPipeline` and `RuntimeInputActionRegistry`, with
  optional exact camera/selection only for focus behavior. It does not retain
  document, interaction, editor, config, asset-service, GPU-cache, renderer,
  or gizmo owners. Normal-bake Vulkan operation does not need to precede this
  callback-interface removal. `RUNTIME-129` and this task may land in either
  order; whichever lands second migrates and reruns their shared Vulkan
  fixtures, and `RUNTIME-185` waits for both before auditing final production
  mechanism consumers.
- The source/test/doc inventory for `IApplication`, `OnSimTick`, and
  `OnVariableTick` spans roughly 55 files. This dedicated slice keeps that
  migration separate from the final Engine PImpl and exact policy ratchet.

## Slice plan

1. **Callback-behavior preparation.** Inventory every implementation; move
   real test/app tick behavior into test-only or production composed modules
   while the existing interface remains. Retain only existing minimal no-op
   fixtures needed by the old constructor. This slice changes no Engine public
   declaration and must build/run focused lifecycle coverage.
2. **Atomic interface cutover.** In one bounded, buildable change, update
   Engine construction, Sandbox startup, and every remaining construction site;
   delete `IApplication` and its four callback invocations; remove the now
   no-op fixtures. Touch only runtime lifecycle, app composition, and their
   direct tests—no unrelated owner or frame-order changes.
3. **Structural ratchet and broad evidence.** Add the absence checks, update
   lifecycle/app docs and module inventory, then run the focused, full CPU,
   ASan, UBSan, and Vulkan-smoke gates. Do not defer behavior tests or docs
   required to explain the cutover.

## Required changes
- [x] Inventory every production and test `IApplication` implementation and
      classify initialize, fixed-step, variable-frame, and shutdown behavior.
- [x] Remove `std::unique_ptr<IApplication>` from Engine construction and
      delete the `IApplication` interface.
- [x] Make Sandbox construct Engine, compose concrete modules before
      initialization—including separate `SceneDocumentModule` and
      `SceneInteractionModule` instances—retain the narrow module references
      needed for their own app composition, install initial-world state, and
      preserve `RUNTIME-168`'s separate private default-policy provider/handle
      aggregate over exact published services; then explicitly run and tear
      down in the documented order.
- [x] Move any real per-frame production behavior to the owning module's
      smallest existing frame hook/system; delete the no-op Sandbox tick
      callbacks rather than replacing them.
- [x] Replace test callback fixtures with small test-only runtime modules or
      direct narrow lifecycle helpers. Do not add a production inline builder
      solely to shorten tests.
- [x] Preserve module registration/finalization, initial-content timing,
      shutdown announcement, reverse module shutdown, and device-idle teardown
      order.
- [x] Remove `Engine&` from every app lifecycle callback or stored app
      behavior object; `main()` may own a concrete Engine value and narrow
      module references as the composition root.

## Tests
- [x] Preserve fixed-step count/alpha, frame-hook order, exit, minimized,
      close-before-render, initialization failure, and reverse-shutdown
      coverage through test modules/narrow fixtures.
- [x] Preserve the full Sandbox acceptance path with explicit module/default
      composition and no application callbacks.
- [x] Add structural coverage proving `IApplication`, `OnSimTick`, and
      `OnVariableTick` are absent from production source and no app lifecycle
      surface accepts `Engine&`.
- [x] Run focused runtime/app/module coverage and the complete default
      CPU-supported gate.
- [x] Run ASan and UBSan CPU variants because Engine no longer owns the app
      callback object and teardown ownership changes.
- [x] Configure/build `ci-vulkan` and rerun the Sandbox/object-space-normal
      smoke cohort because Vulkan smoke fixtures also migrate off
      `IApplication`. If `RUNTIME-129` is already retired, preserve its
      operational proof; otherwise leave the fixtures on the settled explicit
      lifecycle for that task to extend.

## Docs
- [x] Update runtime lifecycle, Sandbox startup, app examples, ADR-0024/0027
      current-state notes, and source READMEs.
- [x] Regenerate the module inventory if module surfaces change.

## Acceptance criteria
- [x] Engine owns no application callback object and production source
      contains no `IApplication`, `OnSimTick`, or `OnVariableTick`.
- [x] Sandbox composition is explicit at the app root and behaviorally
      equivalent through a real Engine run.
- [x] No module, handler, setup, or app lifecycle surface receives `Engine&`.
- [x] Test convenience does not create a production builder/framework.
- [x] CPU, ASan, and UBSan lifecycle/teardown gates pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeModule|RuntimeApplication|RuntimeSandboxAcceptance|FrameLoop|WindowClose' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Sandbox|ObjectSpaceNormal.*Bake' --no-tests=error --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_kernel_convergence.py --root . --strict
```

## Forbidden changes
- Replacing `IApplication` with another production callback interface,
  builder, registry, or `Engine&` adapter.
- Moving app policy or initial content into Engine.
- Mixing residual Engine API/caller cleanup or the final PImpl/checker ratchet
  into this lifecycle slice.
- Changing frame order or app-visible behavior while moving ownership.

## Maturity
- Target: `Operational`; explicit Sandbox composition must run through the
  canonical Engine lifecycle, with CPU, sanitizer, and Vulkan-smoke evidence.
