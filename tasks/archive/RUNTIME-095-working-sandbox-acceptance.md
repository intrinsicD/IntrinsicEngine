# RUNTIME-095 — Working sandbox app acceptance path

## Status
- State: done.
- Owner/agent: codex.
- Branch: `main`.
- Maturity reached: `Operational` on Vulkan-capable hosts for the scoped working-sandbox acceptance scene; `CPUContracted` in the default CPU/null gate.
- Completion date: 2026-06-04.
- PR/commit: pending.
- Next verification step: none for the scoped acceptance path. Broad asset-format breadth, advanced material/renderer parity, serialization, and legacy deletion remain non-blocking follow-up work outside this task.

## Goal
- Add the end-to-end acceptance task for the promoted `ExtrinsicSandbox`: render at least one mesh, one graph, and one point cloud through the default runtime/graphics path with working camera controls, entity/primitive selection, selection outline, and core UI panels.

## Non-goals
- No implementation of prerequisite renderer/runtime/UI tasks inside this acceptance task.
- No performance benchmark claims; this is functional smoke/acceptance only.
- No legacy graphics fallback path or direct app-layer rendering shortcuts.
- No requirement that every asset format or every visualization mode be complete.
- No mandatory GPU run in the default CPU CI gate; Vulkan acceptance remains opt-in and label-gated.
- No advanced PBR, transparent-selection parity, Gaussian splats, full scene serialization, KTX decode, post-upload material re-resolution, or broad legacy deletion claim.

## Context
- Owner/layer: `runtime` composition and acceptance harness, with dependencies across rendering, assets, runtime adapters, and UI.
- Slices 1 and 2 landed on 2026-06-03 and proved the CPU/null residency, camera, entity-selection, primitive-selection, outline snapshot, and UI panel contracts at `CPUContracted`.
- Slice 3 now executes green on this Vulkan-capable host at `Operational`: the opt-in `RuntimeSandboxAcceptanceGpuSmoke.AcceptanceSceneReachesOperationalDefaultRecipePresent` test drives the runtime `Engine` for bounded frames with one mesh, one graph, one point cloud, and the `SandboxEditorUi` attached like `ExtrinsicSandbox`.
- Required upstreams are retired: default-recipe rendering (`GRAPHICS-072..079`, `GRAPHICS-081`), runtime geometry residency (`RUNTIME-085..088`), selection (`GRAPHICS-074`, `RUNTIME-089`, `RUNTIME-092`, `RUNTIME-093`), asset/UI plumbing (`ASSETIO-001`, `RUNTIME-090`, `UI-001`), and optional visualization/spatial-debug adapters (`RUNTIME-082`, `RUNTIME-083`, `GRAPHICS-077`, `GRAPHICS-078`).
- The sandbox app itself remains policy-light: `src/app/Sandbox` imports runtime only, attaches `SandboxEditorUi`, and does not add app-layer graphics shortcuts.
- The accepted scene is a deterministic in-memory ECS `GeometrySources` scene. File/import command surfaces exist through `ASSETIO-001` and `UI-001`, but broad file-format visual coverage is deliberately not a RUNTIME-095 blocker.

## Slice plan
- **Slice 1 (landed 2026-06-03, `CPUContracted`).** CPU/null end-to-end residency + composition acceptance: one mesh, one graph, and one point cloud authored through promoted ECS `GeometrySources`; one `RenderExtractionCache::ExtractAndSubmit` proving all three residency lanes upload once and bind three distinct `GpuWorld` instance/geometry handles, plus static-scene re-extraction reuse; a runtime camera controller producing a finite/invertible frame camera; runtime whole-entity selection for an entity of each family; and the sandbox editor panel frame enumerating the scene and reporting selection. Test: `Test.RuntimeSandboxAcceptance.cpp` (`integration;runtime;graphics`).
- **Slice 2 (landed 2026-06-03, `CPUContracted`).** Primitive-selection acceptance: drives `RefinePickReadbackResult` with mocked pick readbacks to resolve one primitive domain per family (mesh Face, graph Edge, point-cloud Point) and asserts the `RenderWorld.Selection` outline snapshot is populated for the selected entity via `ExtractAndSubmit(..., &selection)` -> `ExtractRenderWorld`. Tests: `RuntimeSandboxAcceptance.PrimitiveRefinementResolvesOneDomainPerFamily` and `RuntimeSandboxAcceptance.SelectionOutlineSnapshotPopulatedForSelectedEntity`.
- **Slice 3 (completed 2026-06-04, `Operational`).** Opt-in `gpu;vulkan;integration` smoke (`Test.RuntimeSandboxAcceptanceGpuSmoke.cpp`) drives the runtime `Engine` for `kTargetFrames` bounded frames with the acceptance families composed onto the reference camera and `SandboxEditorUi` attached. It asserts the default recipe reaches canonical `"Present"`, no canonical pass falls through `SkippedUnavailable`, each acceptance family resides on its own mesh/graph/point-cloud runtime residency lane, and Vulkan fallback counters stay stable. The first live run exposed that the smoke used an empty bounded app and therefore never produced ImGui draw work; the closing fix attaches the same runtime-owned editor shell as `ExtrinsicSandbox`, making `ImGuiPass` record on the operational path.

## Required changes
- [x] Define a deterministic sandbox acceptance scene provider or fixture that creates/loads one mesh, one graph, and one point cloud using promoted runtime/ECS/asset seams. _(Slice 1: `Test.RuntimeSandboxAcceptance.cpp` authors them via promoted ECS `GeometrySources`.)_
- [x] Ensure each fixture entity has camera-visible transforms, bounds, render hints, selectable state, and valid `GpuWorld` residency via the appropriate runtime bridge. _(Slice 1.)_
- [x] Add an opt-in `gpu;vulkan;integration` smoke that launches or drives `ExtrinsicSandbox`/runtime for bounded frames and asserts default-recipe command recording reaches present with no canonical pass falling through the unavailable branch. _(Slice 3: `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`.)_
- [x] Add CPU/null integration coverage that verifies extraction/submission state for the same scene without requiring Vulkan. _(Slice 1.)_
- [x] Assert camera controller updates produce finite/invertible frame cameras for the acceptance scene. _(Slice 1.)_
- [x] Assert selection flow can select an entity and at least one primitive domain per geometry family where supported. _(Slice 1 whole-entity selection per family; Slice 2 resolves mesh Face, graph Edge, and point-cloud Point.)_
- [x] Assert outline snapshot state is populated for selected/hovered entities. _(Slice 2 proves selected-entity outline snapshot; Slice 3 proves the canonical default-recipe pass path on Vulkan.)_
- [x] Assert core UI panels register and produce deterministic enabled/disabled states for the acceptance scene. _(Slice 1 panel-model contract; Slice 3 attaches `SandboxEditorUi` and records `ImGuiPass`.)_
- [x] Record unsupported but non-blocking features explicitly so the acceptance stop-state is reviewable. _(See `Non-goals` and `Context`: broad asset formats, KTX, post-upload material re-resolution, advanced PBR, transparency, Gaussian splats, serialization, broad visualization breadth, and legacy deletion are out of scope.)_

## Tests
- [x] Add `integration;runtime` CPU/null acceptance test for mesh/graph/point-cloud extraction and residency sidecars. _(Slice 1, `RuntimeSandboxAcceptance.MeshGraphPointCloudAllResideThroughOneExtraction`.)_
- [x] Add `integration;runtime` selection acceptance test using mocked pick results and runtime selection/refinement APIs. _(Slice 2, `PrimitiveRefinementResolvesOneDomainPerFamily` + `SelectionOutlineSnapshotPopulatedForSelectedEntity`.)_
- [x] Add `integration;ui` or `contract;runtime` UI callback acceptance for core panels over the acceptance scene. _(Slice 1, `RuntimeSandboxAcceptance.EditorPanelFrameEnumeratesAcceptanceScene`; Slice 3 live ImGui route.)_
- [x] Add opt-in `gpu;vulkan;integration` smoke for default-recipe visible rendering on Vulkan-capable hosts. _(Slice 3, `RuntimeSandboxAcceptanceGpuSmoke.AcceptanceSceneReachesOperationalDefaultRecipePresent`, executable `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`, labels `gpu;vulkan;integration;runtime;graphics`.)_
- [x] Keep slow/GPU tests out of the default CPU gate with labels documented in `tests/README.md` and `tests/CMakeLists.txt`.

## Docs
- [x] Update `src/app/Sandbox/README.md` with current build/run instructions and scoped expected capabilities.
- [x] Update `tasks/backlog/README.md` Theme A status when this task retires.
- [x] Update `docs/reviews/2026-05-11-sandbox-graphics-gap-analysis.md` or add/update a newer review note marking the old visible-triangle gap analysis as superseded. _(The newer `docs/reviews/2026-05-30-sandbox-app-remaining-gates.md` is updated to mark RUNTIME-095 complete.)_
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` for sandbox-visible mesh/graph/point-cloud rendering and interaction parity.

## Acceptance criteria
- [x] `ExtrinsicSandbox` can render the acceptance mesh, graph, and point cloud through the default recipe on a Vulkan-capable host. _(Operationally proven by the RUNTIME-095 smoke driving the same runtime/editor composition path with the acceptance scene.)_
- [x] Runtime camera controls work for the acceptance scene. _(CPU/null contract proves finite/invertible controller output; Vulkan smoke drives the reference camera.)_
- [x] Entity selection, primitive selection, and selection outline work for the supported geometry domains. _(CPU/null selection/refinement/outline snapshot contracts plus Vulkan default-recipe pass recording.)_
- [x] Core UI panels are present and wired to runtime/editor command surfaces. _(CPU/null panel-model contract plus Vulkan `ImGuiPass` recording with `SandboxEditorUi` attached.)_
- [x] CPU/null tests prove extraction/residency/selection/UI contracts without GPU.
- [x] Opt-in GPU/Vulkan smoke proves the operational default-recipe path without reintroducing the retired bootstrap recipe scaffold.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .

# Optional Vulkan acceptance on capable hosts:
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan|integration' -LE 'slow|flaky-quarantine' --timeout 120
```

### Slice 3 completion session record (2026-06-04, Vulkan-capable host)

Ran on this host with Clang 20.1.2, `ci-vulkan`, GLFW, promoted Vulkan, and offline dependencies:

- `cmake --preset ci-vulkan` — configured clean.
- `cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests ExtrinsicSandbox` — built clean.
- Initial focused smoke run failed because the bounded smoke app did not attach `SandboxEditorUi`, leaving `ImGuiPass=SkippedUnavailable` while the rest of the default recipe reached `Present`.
- After attaching the runtime-owned `SandboxEditorUi` in the bounded smoke app, `ctest --test-dir build/ci-vulkan --output-on-failure -V -R 'RuntimeSandboxAcceptanceGpuSmoke' --timeout 120` passed 1/1. The test executed, not skipped, and carried labels `gpu;vulkan;integration;runtime;graphics`.
- The default CPU gate initially exposed two unrelated module-interface compatibility hazards that would have made this task unreviewable: `AssetFileFormat::Unknown` had shifted all existing format enum ordinals, and `CameraViewInput` / `CameraViewSnapshot` had inserted `ExplicitCameraTransition` before existing validity fields. The closing patch pins the promoted asset-format ordinals (`OBJ=0` ... `KTX=17`, `Unknown=0xff`) and appends the camera-transition flag after the existing fields.
- Local rebuilds had to run with `CCACHE_DISABLE=1` after those `.cppm` changes because unchanged implementation units were otherwise reused from ccache with stale C++ module-interface assumptions.
- Final focused filters passed after cache-safe rebuild:
  - `IntrinsicAssetUnitTests --gtest_filter=AssetImportRouter.RoutesObjMeshImportFromPath:AssetImportRouter.PublishesDeterministicDebugNamesAndFormatTable:AssetGeometryIOBridge.RoutesTypedImportCallbacksByResolvedRoute`.
  - `IntrinsicRuntimeContractTests --gtest_filter=RuntimeAssetGeometryIO.RegistersAllPromotedGeometryRoutes:ReferenceCameraBuildInput.ZeroViewportFallsBackToUnitAspectAndStaysValid`.
- Final default CPU-supported gate passed after rebuilding `IntrinsicTests` and `IntrinsicBenchmarkSmoke` with ccache disabled:
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — 2719/2719 enabled tests passed.
- Final focused Vulkan acceptance passed after rebuilding `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests` and `ExtrinsicSandbox` with ccache disabled:
  - `ctest --test-dir build/ci-vulkan --output-on-failure -V -R 'RuntimeSandboxAcceptanceGpuSmoke' --timeout 120` — 1/1 test passed.
- Final structural/doc checks passed:
  - `python3 tools/agents/check_task_policy.py --root . --strict`.
  - `python3 tools/agents/check_task_state_links.py --root . --strict`.
  - `python3 tools/docs/check_doc_links.py --root .`.
  - `python3 tools/repo/check_test_layout.py --root . --strict`.
  - `python3 tools/repo/check_layering.py --root src --strict`.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` regenerated 472 modules with no inventory diff.
  - `git diff --check`.

### Prior Slice 3 authoring session record (2026-06-04, non-Vulkan host)

The authoring session compiled the smoke under `ci`, observed a deterministic non-Vulkan self-skip, ran the default CPU gate, built the benchmark-smoke binary required by the current CTest inventory, and ran structural checks (`check_test_layout.py`, `check_layering.py`, `check_doc_links.py`, `check_task_policy.py`, `check_task_state_links.py`). That session could not retire the task because the opt-in Vulkan smoke did not execute.

## Forbidden changes
- Reintroducing legacy render orchestration or graphics-owned ECS access to make the sandbox pass.
- Adding app-layer shortcuts that bypass runtime composition or renderer snapshots.
- Treating the retired bootstrap recipe scaffold as final acceptance for the working sandbox.
- Making GPU/Vulkan tests mandatory in the default CPU gate.
- Claiming broad performance, PBR, transparency, serialization, broad file-format, or legacy-deletion parity from this functional acceptance task.

## Maturity
- Target reached: `Operational` on Vulkan-capable hosts for the scoped acceptance scene; `CPUContracted` everywhere else.
- `GRAPHICS-081` has retired the visible-triangle bootstrap scaffold; this task treats the default recipe as the only rendering acceptance path.
- No additional `Operational` follow-up is owed for the scoped mesh/graph/point-cloud + camera + selection + outline + UI acceptance path.
