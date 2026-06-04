# RUNTIME-095 — Working sandbox app acceptance path

## Goal
- Add the end-to-end acceptance task for the promoted `ExtrinsicSandbox`: render at least one mesh, one graph, and one point cloud through the default runtime/graphics path with working camera controls, entity/primitive selection, selection outline, and core UI panels.

## Non-goals
- No implementation of prerequisite renderer/runtime/UI tasks inside this acceptance task.
- No performance benchmark claims; this is functional smoke/acceptance only.
- No legacy graphics fallback path or direct app-layer rendering shortcuts.
- No requirement that every asset format or every visualization mode be complete.
- No mandatory GPU run in the default CPU CI gate; Vulkan acceptance remains opt-in and label-gated.

## Context
- Status: Slices 1 and 2 landed (CPU/null residency + camera + entity & primitive
  selection + outline snapshot + UI panel acceptance, `CPUContracted`). Slice 3's
  opt-in `gpu;vulkan;integration` default-recipe present smoke is now authored
  (`tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp`,
  executable `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`) and self-skips on
  non-Vulkan hosts. The smoke has only been compile-verified and observed to
  self-skip on a non-Vulkan host; it has **not** yet been run green on a
  Vulkan-capable host. Per `docs/agent/task-maturity.md`, `Operational` requires
  that labeled run to have actually executed, so the task stays in backlog until
  Slice 3 retires it on a Vulkan host.
- Owner/layer: `runtime` composition and acceptance harness, with dependencies across rendering, assets, runtime adapters, and UI.
- This task is the discoverable path from the current default-recipe visible-triangle baseline to the working sandbox described in Theme A of `tasks/backlog/README.md`.
- Required upstreams include default-recipe rendering (`GRAPHICS-072..079`, `GRAPHICS-081`), runtime geometry residency (`RUNTIME-085..088`), selection (`GRAPHICS-074`, `RUNTIME-089`, `RUNTIME-092`, `RUNTIME-093`), asset/UI plumbing (`ASSETIO-001` — which subsumed the retired `RUNTIME-080` texture bridge —, `RUNTIME-090`, `UI-001`), and optional visualization/spatial-debug adapters (`RUNTIME-082`, `RUNTIME-083`, `GRAPHICS-077`, `GRAPHICS-078`).
- The sandbox app itself must remain policy-light: composition and test scene setup live in runtime/reference-scene/editor seams, not direct graphics imports from `src/app`.

## Slice plan
- **Slice 1 (landed 2026-06-03, `CPUContracted`).** CPU/null end-to-end
  residency + composition acceptance: a deterministic scene of one mesh, one
  graph, and one point cloud authored through promoted ECS `GeometrySources`;
  one `RenderExtractionCache::ExtractAndSubmit` proving all three residency lanes
  upload once and bind three distinct `GpuWorld` instance/geometry handles (plus
  static-scene re-extraction reuse); a runtime camera controller producing a
  finite/invertible frame camera; runtime whole-entity selection for an entity of
  each family; and the sandbox editor panel frame enumerating the scene and
  reporting selection. Test: `Test.RuntimeSandboxAcceptance.cpp`
  (`integration;runtime;graphics`). Defers primitive-domain selection/outline and
  the GPU smoke to Slices 2/3.
- **Slice 2 (landed 2026-06-03, `CPUContracted`).** Primitive-selection
  acceptance: drives `RefinePickReadbackResult` with mocked pick readbacks to
  resolve one primitive domain per family (mesh Face, graph Edge, point-cloud
  Point) and asserts the `RenderWorld.Selection` outline snapshot is populated
  for the selected entity via `ExtractAndSubmit(..., &selection)` →
  `ExtractRenderWorld`. Tests:
  `RuntimeSandboxAcceptance.{PrimitiveRefinementResolvesOneDomainPerFamily,SelectionOutlineSnapshotPopulatedForSelectedEntity}`.
- **Slice 3 (smoke authored; `Operational` run pending a Vulkan host).** Opt-in
  `gpu;vulkan;integration` smoke
  (`Test.RuntimeSandboxAcceptanceGpuSmoke.cpp`) that drives the runtime `Engine`
  for `kTargetFrames` bounded frames with the acceptance families (one mesh, one
  graph, one point cloud) composed onto the reference camera, and asserts the
  default recipe reaches the canonical `"Present"` pass with no canonical pass
  falling through the `SkippedUnavailable` branch, each acceptance family
  resides on its own runtime residency lane (mesh/graph/point-cloud, asserted
  per lane so the reference triangle's separate `Procedural` lane cannot mask a
  broken family), and the Vulkan fallback counters stay stable. It
  self-skips when GLFW cannot initialize or the promoted Vulkan device does not
  reach operational readiness. The test source + CMake wiring (a dedicated
  `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests` executable labeled
  `gpu;vulkan;integration;runtime;graphics`) have landed and compile-verify under
  `ci`; running the smoke green on a Vulkan-capable host is what lets the task
  retire at `Operational` and update Theme A status. That run was not possible in
  the authoring session (non-Vulkan host) and is the remaining retirement gate.

## Required changes
- [x] Define a deterministic sandbox acceptance scene provider or fixture that creates/loads one mesh, one graph, and one point cloud using promoted runtime/ECS/asset seams. _(Slice 1: `Test.RuntimeSandboxAcceptance.cpp` authors them via promoted ECS `GeometrySources`.)_
- [x] Ensure each fixture entity has camera-visible transforms, bounds, render hints, selectable state, and valid `GpuWorld` residency via the appropriate runtime bridge. _(Slice 1: transform/world-matrix, render hints, `SelectableTag`, `StableId`, and per-lane residency asserted.)_
- [x] **(Slice 3 — artifact landed; operational run pending a Vulkan host)** Add an opt-in `gpu;vulkan;integration` smoke that launches or drives `ExtrinsicSandbox`/runtime for bounded frames and asserts default-recipe command recording reaches present with no canonical pass falling through the unavailable branch. _(Slice 3: `tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` drives `Engine::Run()` for `kTargetFrames` over the acceptance families on the reference camera and asserts `"Present"` is `Recorded`, no pass is `SkippedUnavailable`, each acceptance family resides on its own mesh/graph/point-cloud residency lane (asserted per lane so the reference triangle's `Procedural` lane cannot mask a broken family), and the Vulkan fallback counters stay stable. Authored + compile-verified under `ci` and observed to self-skip on a non-Vulkan host; the assertion has **not** yet executed green on a Vulkan device — that operational run is the remaining retirement gate, tracked in `Acceptance criteria` and `Verification`.)_
- [x] Add CPU/null integration coverage that verifies extraction/submission state for the same scene without requiring Vulkan. _(Slice 1.)_
- [x] Assert camera controller updates produce finite/invertible frame cameras for the acceptance scene. _(Slice 1.)_
- [x] Assert selection flow can select an entity and at least one primitive domain per geometry family where supported: mesh face/edge/vertex, graph edge/node, point-cloud point. _(Slice 1 whole-entity selection per family; Slice 2 resolves mesh Face / graph Edge / point-cloud Point via `RefinePickReadbackResult`.)_
- [x] Assert outline snapshot state is populated for selected/hovered entities. _(Slice 2: `RenderWorld.Selection.SelectedStableIds` populated; the graphics command path recording selection/outline passes when **operational** is the Slice 3 `gpu;vulkan` proof.)_
- [x] Assert core UI panels register and produce deterministic enabled/disabled states for the acceptance scene. _(Slice 1: `BuildSandboxEditorPanelFrame` enumerates the 3-entity scene and reports selection.)_
- [ ] **(Retirement)** Record unsupported but non-blocking features explicitly (for example advanced PBR, transparent selection, Gaussian splats, full scene serialization) so the acceptance stop-state is reviewable.

## Tests
- [x] Add `integration;runtime` CPU/null acceptance test for mesh/graph/point-cloud extraction and residency sidecars. _(Slice 1, `RuntimeSandboxAcceptance.MeshGraphPointCloudAllResideThroughOneExtraction`.)_
- [x] Add `integration;runtime` selection acceptance test using mocked pick results and runtime selection/refinement APIs. _(Slice 2, `PrimitiveRefinementResolvesOneDomainPerFamily` + `SelectionOutlineSnapshotPopulatedForSelectedEntity`.)_
- [x] Add `integration;ui` or `contract;runtime` UI callback acceptance for core panels over the acceptance scene. _(Slice 1, `RuntimeSandboxAcceptance.EditorPanelFrameEnumeratesAcceptanceScene`.)_
- [x] **(Slice 3 — artifact landed; operational run pending a Vulkan host)** Add opt-in `gpu;vulkan;integration` smoke for default-recipe visible rendering on Vulkan-capable hosts. _(Slice 3, `RuntimeSandboxAcceptanceGpuSmoke.AcceptanceSceneReachesOperationalDefaultRecipePresent`, executable `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`, labels `gpu;vulkan;integration;runtime;graphics`. Compile-verified + self-skips on non-Vulkan hosts; the operational green run is the retirement gate.)_
- [x] Keep slow/GPU tests out of the default CPU gate with labels documented in `tests/README.md` and `tests/CMakeLists.txt`. _(Slice 1 test is `integration;runtime;graphics`, CPU-only; no GPU label added yet.)_

## Docs
- [ ] Update `README.md` or sandbox-specific docs with current run instructions and expected capabilities once acceptance passes.
- [ ] Update `tasks/backlog/README.md` Theme A status when this task retires.
- [ ] Update `docs/reviews/2026-05-11-sandbox-graphics-gap-analysis.md` or add a newer review note marking the old visible-triangle gap analysis as superseded.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` for sandbox-visible mesh/graph/point-cloud rendering and interaction parity.

## Acceptance criteria
- [ ] `ExtrinsicSandbox` can render the acceptance mesh, graph, and point cloud through the default recipe on a Vulkan-capable host.
- [ ] Runtime camera controls work for the acceptance scene.
- [ ] Entity selection, primitive selection, and selection outline work for the supported geometry domains.
- [ ] Core UI panels are present and wired to runtime/editor command surfaces.
- [ ] CPU/null tests prove extraction/residency/selection/UI contracts without GPU.
- [ ] Opt-in GPU/Vulkan smoke proves the operational default-recipe path without reintroducing the retired bootstrap recipe scaffold.

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

### Slice 3 authoring session record (2026-06-04, non-Vulkan host)

Ran on a host without a Vulkan driver (the `ci` preset still resolved to the
GLFW + promoted-Vulkan build stack, so the gpu/vulkan targets compile here):

- `cmake --preset ci` + `cmake --build --preset ci --target IntrinsicTests` — built clean.
- Default CPU gate `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`:
  2669/2669 non-benchmark tests passed, including the six Slice 1/2
  `RuntimeSandboxAcceptance.*` cases. The benchmark-smoke pair reported
  `***Not Run` only because `--target IntrinsicTests` does not build the separate
  `IntrinsicBenchmarkSmoke` binary; after building that target,
  `ctest -R IntrinsicBenchmarkSmoke` passes 2/2, so the gate is green.
- Slice 3 smoke compile-verified: reconfigure + `cmake --build --preset ci --target
  IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests` compiles and links the new
  `Test.RuntimeSandboxAcceptanceGpuSmoke.cpp`.
- `ctest --test-dir build/ci -R RuntimeSandboxAcceptanceGpuSmoke` reports the test
  with labels `gpu;vulkan;integration;runtime;graphics` and **Skipped** (no
  operational Vulkan device on this host) — the intended self-skip, not a failure.
- Structural checks all clean: `check_test_layout.py --strict`,
  `check_layering.py --root src --strict`, `check_doc_links.py`,
  `check_task_policy.py --strict` (incl. maturity follow-ups), and
  `check_task_state_links.py --strict`.

**Not run (the remaining `Operational`/retirement gate):** the optional
`ci-vulkan` GPU acceptance leg — the smoke's assertions executing green on a
Vulkan-capable host. Per `docs/agent/task-maturity.md`, that labeled run must
actually execute before this task may claim `Operational` and retire; it could
not run on this non-Vulkan host.

## Forbidden changes
- Reintroducing legacy render orchestration or graphics-owned ECS access to make the sandbox pass.
- Adding app-layer shortcuts that bypass runtime composition or renderer snapshots.
- Treating the retired bootstrap recipe scaffold as final acceptance for the working sandbox.
- Making GPU/Vulkan tests mandatory in the default CPU gate.
- Claiming broad performance, PBR, transparency, or serialization parity from this functional acceptance task.

## Maturity
- Target: `Operational` on Vulkan-capable hosts for the scoped acceptance scene; `CPUContracted` everywhere else.
- Slice 1 closes the CPU/null residency + camera + entity-selection + UI
  acceptance at `CPUContracted`. Slice 2 extends CPU coverage to primitive
  selection/outline. Slice 3 owns the `Operational` opt-in `gpu;vulkan` present
  smoke and the retirement (Theme A status + parity-matrix + gap-analysis docs).
- `GRAPHICS-081` has retired the visible-triangle bootstrap scaffold; this task treats the default recipe as the only rendering acceptance path.
