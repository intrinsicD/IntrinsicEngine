---
id: RUNTIME-144
theme: F
depends_on: []
maturity_target: Operational
completed: 2026-07-06
---
# RUNTIME-144 — Post-import processor and import UX-policy seam

## Status
- Retired on 2026-07-06 at `Operational` on local `main`; PR not opened.
- PR/commit: this retirement commit.
- Slice A implemented the behavior-preserving post-import processor registry,
  invokes it from geometry materialization, and routes direct-mesh
  generated-normal work through that registry.
- Slice B implemented the import-authoring policy registry, import-completed
  handler registry, default-behavior preservation test, and no-registration
  minimal-materialization test.
- Slice C implemented the runtime input-action registry and routed the `F`
  focus-on-selection command through that registry.
- Slice D moved the generated-normal, import-authoring, import-completed, and
  input-action default registrations out of `Engine` and into
  `Extrinsic.Runtime.SandboxDefaultPolicies`; the sandbox app installs and
  unregisters that default bundle explicitly.

## Slice plan

- **Slice A — post-import processor registry.** Add ordered post-import
  processor registration to `Engine`, invoke it from geometry materialization,
  and register the existing direct-mesh generated-normal processor through the
  new registry. Behavior remains unchanged; `Engine` may still own the default
  registration in this slice so the seam can land independently.
- **Slice B — import UX/default authoring policy.** Move focus-on-import,
  auto-select, and default render/selection/visualization components behind
  registered policy callbacks. Add no-registration tests for minimal
  materialization; `Engine` remains the temporary default registrant until
  Slice D moves ownership to sandbox/default composition.
- **Slice C — input-action command seam.** Route the existing `F` focus
  command through a registered input action while keeping RunFrame responsible
  only for generic dispatch.
- **Slice D — retirement.** Remove remaining method-specific/default policy
  hardcoding from `Runtime.Engine`, update docs, run the default CPU gate, and
  retire the task.

## Goal
- Add an extension point to the import pipeline so post-import processing
  and import-time UX policy are registered by features/apps instead of
  hardcoded in `Runtime.Engine`: the direct-mesh normal-bake step, the
  camera-focus/auto-select behavior, entity authoring defaults, and the
  `F`-key focus binding all move behind registrable seams.

## Non-goals
- No change to the normal-bake algorithm or its scheduling domain — the
  CPU→GPU re-domaining is owned by `RUNTIME-129`; this task changes *who
  registers* the step, not what it does. Coordinate: if `RUNTIME-129` lands
  first, its GPU bake request enqueues through this seam; if this lands
  first, `RUNTIME-129` re-domains the registered processor.
- No import format/decode changes (`RUNTIME-142` owns async IO routing).
- No keybinding system beyond routing the existing `F` command through a
  registrable input-action/command seam.

## Context
- Owner/layer: `runtime` (import materialization, frame-loop input
  handling); registrations move to the feature/app side (today the sandbox
  editor; `ARCH-006` may relocate the registrant later).
- Hardcoded today:
  - `QueueDirectMeshPostProcess` runs for every direct mesh import and
    bakes a generated normal texture with fixed options
    (`options.SourcePropertyName = "v:normal"`, 64×64) and hardcoded
    object-space material bindings
    (`src/runtime/Runtime.Engine.cpp:1293-1373, 1435-1560, 1720-1728`).
  - Import materialization applies editor UX policy: camera focus +
    auto-select on every import (`:3866-3873, 4266-4272`) and authoring
    defaults (`SelectableTag`, `RenderSurface`, white `VisualizationConfig`,
    `:1696-1728`).
  - `RunFrame` hardcodes the `F`-key focus-on-selection edge
    (`:2938-2945, 653-680`).
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  findings R4, R8.

## Required changes
- [x] Add a post-import processor registry: ordered processors registered
      per geometry family, invoked from the materialization path with the
      decoded payload/entity, executing over the existing
      `StreamingExecutor` deferral (the current post-process already runs
      there — the seam formalizes registration, not a new lane).
- [x] Move the direct-mesh generated-normal step into a registered
      processor owned by the feature; default sandbox composition registers
      it so behavior is unchanged.
- [x] Add an import-completed event/callback carrying the created entities;
      move camera-focus/auto-select into a registered handler; make authoring
      defaults a registrable policy with the current values as default policy
      registrations.
- [x] Move the default generated-normal, import-authoring, and
      import-completed policy registrations out of `Engine` into the
      sandbox/default composition owner.
- [x] Route the `F` focus command through a registered input-action/command
      binding (sandbox registers `F` → `FocusCameraOnSelection`); `RunFrame`
      keeps only the generic dispatch.

## Tests
- [x] Contract: with default registrations, import behavior is unchanged
      (normal bake queued, focus/selection applied, defaults present) —
      pinned against existing BUG-044/BUG-048/BUG-050 regressions.
- [x] Contract: with no registrations, an import materializes geometry with
      no bake, no focus/selection mutation, and minimal authoring defaults.
- [x] Contract: processor ordering is deterministic and a failing processor
      fail-closes its own step without corrupting the import.
- [x] Contract: with default input actions, pressing `F` through the Null-window
      `RunFrame` path focuses the selected entity; a bare engine with no
      registered default bundle leaves the same key edge as a no-op.

## Docs
- [x] Update `src/runtime/README.md` import-pipeline extension contract.
- [x] Update `docs/architecture/runtime.md` import apply policy ordering.
- [x] Update `docs/architecture/runtime.md` (frame-order step 4 wording for
      the focus command becomes "dispatch registered input actions").
- [x] Update `src/app/Sandbox/README.md` to document the sandbox-owned default
      policy composition.

## Acceptance criteria
- [x] `Runtime.Engine` import materialization contains no method-specific
      bake options, no editor UX policy, and no hardcoded key checks
      (grep-verified for the moved identifiers).
- [x] Sandbox behavior byte-identical for the fixture imports.
- [x] Default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

Slice A local verification (passed):
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeAssetImportFormatCoverage.PostImportProcessorsRunInOrderAndCanUnregister:RuntimeAssetImportFormatCoverage.DirectObjImportBakesGeneratedNormalTextureFromAuthoredVertexNormals:RuntimeAssetImportFormatCoverage.DirectObjImportComputesAndBakesGeneratedNormalTextureWhenMissingNormals'
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
```

Slice B focused verification (passed):
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeAssetImportFormatCoverage.DefaultImportPoliciesApplyAuthoringUxAndPostProcess:RuntimeAssetImportFormatCoverage.UnregisteredImportPoliciesMaterializeMinimalGeometry:RuntimeAssetImportFormatCoverage.PostImportProcessorsRunInOrderAndCanUnregister:RuntimeAssetImportFormatCoverage.DirectObjImportBakesGeneratedNormalTextureFromAuthoredVertexNormals:RuntimeAssetImportFormatCoverage.DirectObjImportComputesAndBakesGeneratedNormalTextureWhenMissingNormals:RuntimeAssetImportFormatCoverage.RepresentativePromotedFormatsMaterializeDeterministically'
```

Slice C focused verification (passed):
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeInputActions.*:RuntimeCameraFocusCommand.*:ImGuiAdapterEngineWiring.UiCaptureSuppressesRuntimeInputConsumers'
```

Slice D focused verification (passed):
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeAssetImportFormatCoverage.DefaultImportPoliciesApplyAuthoringUxAndPostProcess:RuntimeAssetImportFormatCoverage.UnregisteredImportPoliciesMaterializeMinimalGeometry:RuntimeAssetImportFormatCoverage.PostImportProcessorsRunInOrderAndCanUnregister:RuntimeAssetImportFormatCoverage.DirectObjImportPreservesVertexNormalsInGeometrySources:RuntimeAssetImportFormatCoverage.DirectObjImportDefaultsToMaterialDrivenShading:RuntimeAssetImportFormatCoverage.DirectObjImportBakesGeneratedNormalTextureFromAuthoredVertexNormals:RuntimeAssetImportFormatCoverage.DirectObjImportComputesVertexNormalsWhenMissing:RuntimeAssetImportFormatCoverage.DirectObjImportComputesAndBakesGeneratedNormalTextureWhenMissingNormals:RuntimeAssetImportFormatCoverage.RepresentativePromotedFormatsMaterializeDeterministically:RuntimeInputActions.*'
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='SandboxEditorUi.MeshVertexNormalsCommandSurvivesPendingDirectMeshPostProcess:SandboxEditorUi.EngineImportFacadeMaterializesStandaloneGeometryDomains:SandboxEditorUi.EngineImportFacadeMaterializesNonManifoldObjAsRenderableMesh:SandboxEditorUi.EngineImportFacadeMaterializesObjWithoutAuthoredTexcoordsAsRenderableMesh:SandboxEditorUi.DroppedFilePathsRouteAmbiguousPlyThroughRuntimeImportFacade:SandboxEditorUi.DuplicateDroppedGeometryImportUsesSingleIngestRecord:SandboxEditorUi.DroppedFileQueuePreservesOrderDiagnosticsAndClearCompleted:SandboxEditorUi.DroppedGeometryAssetReimportReloadsSameAssetWithoutDuplicateEntity:SandboxEditorUi.PlatformDropEventImportsObjMeshSelectsItAndEnablesRenderComponents:SandboxEditorUi.PlatformDropNoUvObjUploadsRawSurfaceBeforeDeferredPostProcess:SandboxEditorUi.PlatformDropEventImportsOffMesh'
rg -n 'QueueDirectMeshPostProcess|BakeDirectMeshGeneratedNormalTexturePayload|generated-direct-mesh-normal|Sandbox\.Default|DefaultFocusCameraOnSelection|DefaultMeshImportAuthoring|DefaultImportCompletedUx|Platform::Input::Key::F|RegisterDefaultImportPolicies|RegisterDefaultInputActions|UnregisterDefault' src/runtime/Runtime.Engine.cpp src/runtime/Runtime.Engine.cppm
```

Slice D broad verification (passed):
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
git diff --check
build/ci/bin/IntrinsicRuntimeContractTests
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Changing bake outputs, focus math, or default component values while
  moving them.
- Blocking import on processor completion (deferral semantics preserved).
- Inventing a general keybinding/config system beyond the single routed
  command.
