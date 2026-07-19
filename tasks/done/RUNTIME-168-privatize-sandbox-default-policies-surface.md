---
id: RUNTIME-168
theme: F
depends_on:
  - RUNTIME-183
  - RUNTIME-188
maturity_target: Operational
---
# RUNTIME-168 — Privatize Sandbox default-policy composition

## Status

- Completed and retired on 2026-07-19 at `Operational`; owner: Codex team;
  implementation branch:
  `codex/runtime-168-policy-surface`.
- Implementation checkpoints: task promotion `f45e3441`; policy
  privatization and acceptance coverage `842de246`; final review test-fidelity
  closure `bd405e5c`. Commit reference: this retirement commit records final
  lifecycle state, dependency unblocking, and generated task inventory.
- Verification evidence:
  - the focused CPU selector passed 64/64;
  - the post-review real-app composition selector passed 6/6 after pinning
    the exact `App::OnShutdown` uninstall invocation;
  - the direct-mesh slow regression passed 1/1;
  - the full CPU-supported selector completed 4,247 selected cases with zero
    failures and one expected GLFW/LSan capability skip;
  - the ASan+UBSan `gpu` + `vulkan` Sandbox acceptance selector passed 16/16
    with zero skips on an NVIDIA GeForce RTX 3050, driver 590.48.01, Vulkan
    1.4.325;
  - exact Engine convergence remains `22/0/2/10`, the public module inventory
    drops from 391 to 390 modules, old direct imports drop from 12 to zero, and
    the old production lifecycle consumer drops from one to zero;
  - strict task/state, docs-link, layering, test-layout, root-hygiene,
    generated-inventory, ARA YAML, and whitespace checks pass. The
    clean-workshop automated rows pass; manual row 3 passes and rows 4–6 are
    not applicable because no renderer member, pass, or recipe edge changed.
- `RUNTIME-188` and `RUNTIME-183` are retired. The bounded implementation
  deletes the one-consumer exported policy module, retains its `.cpp` as a
  private implementation unit of the existing
  `Extrinsic.Runtime.SandboxEditorFacades` module, and lets Sandbox own only
  the exact provider borrows and typed registration handles described below.
- 2026-07-19 implementation-preflight correction: Engine shutdown runs the
  announcement boundary, then the generic GPU-participant shutdown/idle
  boundary, then application shutdown, followed by reverse module/provider
  teardown. Policy unregister therefore precedes async/AssetWorkflow provider
  destruction while both exact registries remain live, but does not precede
  GPU-participant shutdown.
- 2026-07-19 testability correction: the fixed production factories and live
  registries deterministically return valid handles and expose no registration
  failure injection or unregister observer. Invalid-handle fail-closed cleanup
  and exact rollback/uninstall order are pinned structurally; behavioral tests
  cover the valid missing-pipeline composition, optional-provider combinations,
  repeated shutdown, and reinitialize without adding test-only production
  seams. `RuntimeInputActionRegistry` is an Engine-owned built-in and cannot be
  absent from a valid initialized Engine; its missing-required preflight branch
  is therefore pinned structurally rather than violated through an owner-only
  `ServiceRegistry::Withdraw(...)` call from a test.
- 2026-07-19 build correction: move the existing `Sandbox.cppm` and
  `Sandbox.cpp` into the already-built `ExtrinsicSandboxEditor` library while
  leaving `main.cpp` on the optional executable. This makes the real app
  lifecycle available to canonical CPU integration tests without introducing
  a new target or changing app-to-runtime layering.
- Retired `RUNTIME-188` publishes the exact optional `SelectionController`, keeps
  it out of generic `RuntimeInputActionServices`, and leaves generic action
  dispatch operational when interaction is omitted. This task can therefore
  capture camera/selection only in the optional `F` descriptor as specified.
- The corrected post-`RUNTIME-183` Engine target is `22` plain imports / `0`
  domain imports / `2` re-exports / `10` public getter names. This task changes
  none of those counts.

## Goal

- Remove the exported, Engine-bound
  `Extrinsic.Runtime.SandboxDefaultPolicies` surface and replace it with four
  plain descriptor factories on the existing
  `Extrinsic.Runtime.SandboxEditorFacades` surface plus private, transactional
  install/uninstall wiring in `src/app/Sandbox/Sandbox.cpp`.

## Non-goals

- No default import-authoring, import-completed auto-selection/autofocus,
  direct-mesh postprocessing, or `F` focus behavior change.
- No new module, owner, registry, service, bundle, interface, dependency
  aggregate, lifecycle abstraction, or compatibility facade.
- No `Runtime.Engine` declaration, implementation, state, service publication,
  getter, re-export, or convergence-policy change.
- No Sandbox ownership of runtime, asset, ECS, graphics, renderer, GPU-cache,
  editor, camera, selection, or gizmo state.
- No production Vulkan normal-bake provider work; `RUNTIME-129` retains that
  operational scope.

## Context

- Owner/layer: runtime retains the policy callback implementations; `app`
  explicitly chooses the Sandbox defaults and retains only their registration
  handles. App continues to import runtime only.
- The current exported module has one production consumer and exists chiefly
  to own a registration aggregate and forward a broad `Engine&`. That is
  one-consumer composition ceremony, not a durable runtime capability.
- The simplest replacement reuses the already-public
  `Extrinsic.Runtime.SandboxEditorFacades` surface for four plain factories:
  one fixed collection of exactly three import-authoring descriptors, one
  import-completed descriptor, one direct-mesh postprocessor descriptor, and
  one `F` focus-action descriptor. The existing
  `Runtime.SandboxDefaultPolicies.cpp` keeps the non-trivial callback bodies as
  a private implementation unit of that module; its `.cppm` and public CMake
  module-file entry disappear.
- `RUNTIME-183` publishes the exact persistent `AssetImportPipeline`;
  `RuntimeInputActionRegistry` is already a persistent built-in service. Those
  are the only required services this composition resolves directly.
- `RUNTIME-188` publishes the exact optional `SelectionController` and removes
  `SelectionController*` from generic `RuntimeInputActionServices`. The `F`
  descriptor therefore captures exact `CameraControllerRegistry&` and
  `SelectionController&` dependencies and is registered only when both
  optional services exist.
- Import auto-selection remains pipeline-provided. The import-completed
  factory must not capture or resolve a selection controller: its callback
  consumes `RuntimeImportCompletedServices::Selection` supplied by
  `AssetImportPipeline`, preserves auto-selection when that service is present,
  and permits materialization when it is absent. It may capture the optional
  camera registry solely for the existing post-import autofocus.
- The app-private aggregate retains non-owning pointers to the required
  pipeline and input registry only so it can unregister typed handles. It is
  not a service/dependency bundle and is never exported.
- The shutdown announcement established by `RUNTIME-183` first cancels imports
  and detaches provider borrows. The generic GPU-participant bridge then drains
  participants and performs any required device-idle wait. Sandbox unregisters
  its handles during application shutdown while the persistent pipeline and
  input registry are still live; reverse AsyncWork/AssetWorkflow module and
  provider teardown occurs afterward.
- Public-factory reuse by another caller is not a reason to recreate a
  lifecycle owner. Reconsider a standalone policy owner only if a second
  independent production application needs the same shared registration
  lifetime and teardown contract.

## Required changes

- [x] Delete `src/runtime/Runtime.SandboxDefaultPolicies.cppm`, remove it from
      the public `CXX_MODULES` file set in `src/runtime/CMakeLists.txt`, and
      leave no compatibility module or import. Keep
      `src/runtime/Runtime.SandboxDefaultPolicies.cpp` as a private target
      source, changing it to an implementation unit of
      `Extrinsic.Runtime.SandboxEditorFacades`.
- [x] Export exactly these four factory capabilities through the existing
      `Runtime.SandboxEditorFacades.cppm`; add no registration/lifecycle
      helper:
      1. a fixed `std::array` of exactly three
         `RuntimeImportEntityAuthoringPolicyDesc` values in Mesh, Graph,
         PointCloud order;
      2. one `RuntimeImportCompletedHandlerDesc`, parameterized only by a
         nullable camera registry for the existing autofocus and otherwise
         driven by pipeline-provided callback services;
      3. one `RuntimePostImportProcessorDesc` for the existing direct-mesh
         generated-normal/normal-bake behavior; and
      4. one `RuntimeInputActionDesc` for `F` focus that captures exact
         `CameraControllerRegistry&` and `SelectionController&`.
- [x] Preserve the current descriptor debug names, format/domain coverage,
      authoring payloads, callback priorities, selection/autofocus semantics,
      async direct-mesh processing, diagnostics, and normal-bake fallback
      behavior. This is a composition migration, not a policy rewrite.
- [x] In `Sandbox.cpp`, resolve all required services before registering
      anything: exact `AssetImportPipeline` and exact
      `RuntimeInputActionRegistry`. Resolve optional exact
      `CameraControllerRegistry` and `SelectionController` separately. Missing
      either required service fails closed with no installed policy; missing
      either optional service omits only `F`, while the import-completed
      descriptor still installs and uses its pipeline-provided selection.
- [x] Add one file-local private handle aggregate containing only the two
      required non-owning provider pointers, the fixed three authoring handles,
      one import-completed handle, one direct-mesh postprocessor handle, and an
      optional input-action handle. Do not export it, move it into runtime, or
      turn it into an owner/service/dependency bundle.
- [x] Install in this exact order: Mesh/Graph/PointCloud authoring policies,
      import-completed handler, direct-mesh postprocessor, then optional `F`
      action. Treat every invalid registration handle as failure, roll back
      only what was installed in exact reverse order, clear every handle and
      provider borrow, and leave the aggregate reusable. Pin the fail-closed
      branches and exact order through source-contract coverage; do not add
      failure injection to fixed registries solely to force an unreachable
      production registration failure.
- [x] Uninstall once in the exact reverse order: optional `F`, direct-mesh
      postprocessor, import-completed handler, then PointCloud/Graph/Mesh
      authoring policies. Clear the aggregate afterward. Repeated shutdown is a
      no-op; initialize → shutdown → initialize registers each descriptor
      exactly once without stale callbacks or handles.
- [x] Preserve the `RUNTIME-183` lifetime boundary: shutdown announcement
      cancels imports and detaches pipeline provider borrows first; the generic
      GPU-participant bridge drains next; application shutdown unregisters
      every policy/action handle while the persistent `AssetImportPipeline` and
      `RuntimeInputActionRegistry` are live; reverse async/AssetWorkflow module
      and provider teardown follows.
- [x] Move the existing `Sandbox.cppm` and `Sandbox.cpp` into
      `ExtrinsicSandboxEditor`'s public/private source sets so the real
      `CreateSandboxApp()` lifecycle is built under ordinary `ci`; remove those
      two sources from `ExtrinsicSandbox` and leave only `main.cpp` on the
      optional executable. Do not rename or add a target.
- [x] Migrate every remaining import of
      `Extrinsic.Runtime.SandboxDefaultPolicies`: production Sandbox; contract
      tests for asset-import format coverage (fast and slow), runtime input
      actions, clustering methods, mesh methods, editor models, scene commands,
      session lifecycle, and visualization; and integration tests for Sandbox
      acceptance GPU smoke and editor presentation. Record the before/after
      module, import, and production-consumer counts.
- [x] Keep the exact post-`RUNTIME-183` Engine snapshot at `22/0/2/10`; the
      implementation must not touch `Runtime.Engine.cppm`,
      `Runtime.Engine.cpp`, or its convergence policy.

## Tests

- [x] Extend descriptor-level coverage to prove the authoring factory has
      exactly three entries in the required order and that all four factories
      preserve the existing debug names, priorities, payloads, callbacks, and
      format/domain behavior.
- [x] Preserve
      `DefaultImportPoliciesApplyAuthoringUxAndPostProcess`,
      `UnregisteredImportPoliciesMaterializeMinimalGeometry`,
      `PostImportProcessorsRunInOrderAndCanUnregister`, direct-mesh
      postprocessing coverage, and
      `ModelSceneCompletionSelectsAndFramesCreatedPrimitives` through the new
      factories and app wiring.
- [x] Split import-completed omission coverage precisely: pipeline-provided
      selection still auto-selects without a camera; absent pipeline selection
      does not block materialization; autofocus occurs only with a camera. The
      completed descriptor must not capture Sandbox's optional selection
      service.
- [x] Update `DefaultFocusKeyDispatchesRegisteredAction` and
      `NoDefaultInputActionsLeaveFocusKeyNoOp` to prove `F` registers only when
      both optional exact services exist, captures both, and does not consume a
      generic selection action service after `RUNTIME-188`.
- [x] Add real-app composition regressions for the valid missing required
      service (`AssetImportPipeline` through omitted `AssetWorkflowModule`),
      each missing optional service, repeated shutdown, and initialize →
      shutdown → initialize exactly-once registration. Pin the impossible
      missing built-in input-registry branch, every invalid-handle cleanup
      branch, and exact reverse rollback/unregister order structurally because
      the fixed descriptors have no inducible registration failure; do not
      violate owner-only service withdrawal or add a test-only registry seam.
- [x] Extend the blocked-import shutdown regression to prove announcement
      cancellation/provider detachment and GPU-participant drain precede app
      handle unregister, that both persistent registries remain live during
      unregister, and that no callback runs during later reverse
      module/provider teardown.
- [x] Add structural coverage proving the old `.cppm`, module declaration,
      imports, and public CMake entry are absent; the retained `.cpp` is private
      to `SandboxEditorFacades`; no replacement module/owner/bundle exists; and
      the default-policy path contains no `Engine&` or forbidden direct
      dependency.
- [x] Migrate/build the current GPU acceptance importer as well as the CPU
      contract/integration callers; preserve the real Sandbox acceptance and
      session-lifecycle paths rather than replacing them with factory-only
      tests.

## Docs

- [x] Update `src/runtime/README.md`,
      `docs/architecture/runtime.md`, `src/app/Sandbox/README.md`,
      `docs/architecture/kernel-target-state.md`, and ADR-0027 current-state
      evidence with the existing-facade factories, app-private handles, exact
      provider set, pipeline-provided auto-selection, optional `F` rule, and
      announcement/unregister/teardown order.
- [x] Update current task/index wording that names the removed public module.
      In particular,
      `tasks/backlog/runtime/RUNTIME-129-schedule-gpu-normal-bake-after-import.md`
      currently says `AssetModelSceneHandoff` and `SandboxDefaultPolicies`
      hardcode source generations; after privatization it must name the
      retained private default-policy implementation/direct-mesh descriptor
      path instead of the deleted module.
- [x] Regenerate `docs/api/generated/module_inventory.md` and record the
      removal of one public module/BMI with no replacement module.

## Acceptance criteria

- [x] `Runtime.SandboxDefaultPolicies.cppm` and
      `Extrinsic.Runtime.SandboxDefaultPolicies` are gone;
      `Runtime.SandboxDefaultPolicies.cpp` remains a private implementation
      unit of the existing `Extrinsic.Runtime.SandboxEditorFacades`.
- [x] The existing surface exports only the four plain descriptor factories;
      Sandbox owns the private provider/handle aggregate and performs
      transactional install plus strict reverse uninstall.
- [x] The only direct required services are `AssetImportPipeline` and
      `RuntimeInputActionRegistry`; camera and selection are optional, `F`
      requires both, and import auto-selection remains pipeline-provided.
- [x] There is no direct default-policy ownership or dependency on
      `SceneDocumentModule`, `EditorCommandHistory`,
      `SceneInteractionModule`, `EngineConfigControl`, an editor/UI host,
      `AssetService`, `GpuAssetCache`, renderer/RHI, or gizmo state.
- [x] Partial failure, repeated shutdown, reinitialize, blocked-import
      shutdown, and canonical Sandbox acceptance are covered; callbacks cannot
      outlive their providers.
- [x] Engine remains at the exact post-`RUNTIME-183` convergence target:
      `22` plain imports / `0` domain imports / `2` re-exports / `10` public
      getter names.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py
python3 tools/docs/check_doc_links.py --root .
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeContractSlowTests IntrinsicRuntimeGraphicsCpuTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests -j 2
ctest --test-dir build/ci --output-on-failure -R 'SandboxDefault|SandboxAppComposition|RuntimeInputActions|AssetImportFormatCoverage|RuntimeSandboxAcceptance|SandboxEditor(SessionLifecycle|Presentation)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -L slow -R '^RuntimeAssetImportFormatCoverage\.DirectMeshEnrichmentCloseDrainsGeneratedGridAndCompletesDeterministically$' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests -j 2
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptance' -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
git diff --check
```

## Forbidden changes

- Adding a public compatibility module, replacement policy module, lifecycle
  owner, registry, service, bundle, interface, generic factory framework,
  dependency struct, or `Engine&` adapter.
- Directly resolving or owning `SceneDocumentModule`,
  `EditorCommandHistory`, `SceneInteractionModule`, `EngineConfigControl`, an
  editor/UI host, `AssetService`, `GpuAssetCache`, renderer/RHI, or gizmo
  state.
- Capturing Sandbox's optional selection service in the import-completed
  descriptor, restoring selection to generic input-action services, or
  installing `F` with only one of camera/selection.
- Relying on reverse module-name order, unregistering after a provider is
  destroyed, leaving partial registrations installed, or changing default
  behavior while changing composition shape.

## Maturity

- Target: `Operational`; the private policy composition remains exercised
  through canonical Sandbox startup, shutdown, reinitialize, and acceptance
  paths.
- Production Vulkan object-space normal-bake operation remains owned by
  `RUNTIME-129`.
