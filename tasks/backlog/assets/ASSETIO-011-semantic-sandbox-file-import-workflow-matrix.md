---
id: ASSETIO-011
theme: F
depends_on: [ASSETIO-010, BUG-098, BUG-099, BUG-100]
maturity_target: Operational
---
# ASSETIO-011 — Semantic Sandbox File / Import workflow matrix

## Goal

- Add a deterministic real-widget integration matrix that enters every
  checked-in `assets/models` route through the Sandbox File / Import controls,
  proves prerequisite gating and disabled reasons, dispatches supported
  payloads, and observes terminal materialization/selection/focus state.

## Non-goals

- No public editor automation API, ImGui Test Engine dependency, coordinate-
  based CI driver, screenshot golden, or production test-only widget.
- No CI dependency on untracked/local `M16.xyz` or `child.obj`; equivalent
  generated fixtures may cover their XYZ/OBJ route classes.
- No duplicate import validation in `app`; the test consumes runtime-owned
  readiness and command results through the real controls.
- No replacement for focused decoder, runtime contract, or Vulkan visibility
  tests.

## Context

- Dependencies: `ASSETIO-010` supplies primary/companion preview;
  `BUG-098` restores production hover timing; `BUG-099` closes the binary PLY
  PointCloud route; `BUG-100` makes every File / Import payload queued and
  responsive.
- The current integration suite opens the real window and can observe a
  disabled hover only after forcing `DelayNone`; it never types the real path,
  selects a real combo row, clicks Import, or covers the checked-in model
  directory as a matrix.
- Existing seams are sufficient and remain test-local: semantic ImGui IDs,
  `SetEditorWindowOpen()`, an after-draw registered observer,
  `NullWindow::QueueEvent/QueueCursor/QueueMouseButton`, `GetLastFrame()`, and
  ingest queue/event snapshots.
- Live 2026-07-16 evidence covered every current file: Duck GLB/GLTF imported
  visibly; Duck0.bin gated; both endian PLY files required a hint and succeeded
  as Mesh but failed as PointCloud; the instanced GLTF produced two visible
  primitive leaves; local XYZ/OBJ imported visibly but blocked the UI; default
  disabled-hover tooltips did not appear because of `BUG-098`.

## Required changes

- [ ] Build a test-local semantic driver that opens File / Import, activates
      the `Path` input by ID, queues characters through `NullWindow`, selects a
      payload combo row by semantic label, and activates `Import asset` by ID.
- [ ] Start each matrix row with a fresh Engine/editor session so path-buffer,
      selection, queue, and ImGui ID state are deterministic.
- [ ] For disabled rows, hold the real pointer over the disabled control with
      production tooltip flags, assert the exact runtime-owned reason, and
      prove no ingest request/callback is dispatched.
- [ ] For supported rows, wait with a bounded state predicate for queued
      completion and assert payload, entity count, selection, camera-focus
      request, asset queue terminal state, and relevant warnings.
- [ ] Keep operational pixel visibility/click-pick evidence in the existing
      focused Vulkan smokes; link those tests from this matrix rather than
      recreating GPU assertions in every row.

## Tests

- [ ] `Duck.glb`: automatic ModelScene, self-contained companion preview,
      queued completion, one primitive, selected/focused.
- [ ] `Duck.gltf`: automatic ModelScene, required `Duck0.bin` ready, missing
      adjacent optional `DuckCM.png` warning, queued completion.
- [ ] `Duck0.bin`: payload chooser/import disabled with the exact unsupported-
      extension reason and no dispatch.
- [ ] `__test_bug094_instanced_triangle.gltf`: queued ModelScene completion,
      two primitive leaves, standard selected/focused completion semantics.
- [ ] Both binary PLY endian fixtures: Unknown disables import with explicit-
      payload reason; explicit Mesh and PointCloud each queue and complete with
      the exact vertex/primitive result.
- [ ] Generated small OBJ and XYZ rows cover automatic Mesh/PointCloud routing
      without relying on local datasets and prove the command returns Pending.
- [ ] Run the matrix repeatedly without pointer-coordinate constants, sleeps
      as readiness oracles, `/tmp` model fixtures, or ImGui style overrides.

## Docs

- [ ] Document the checked-in model workflow matrix and the separation between
      Null-window control coverage and focused Vulkan visibility/click-pick
      smokes in `tests/README.md` and the Sandbox README.
- [ ] Refresh task indexes/session brief and retirement records on closure.

## Acceptance criteria

- [ ] Every tracked file in `assets/models` has an explicit route/prerequisite/
      terminal matrix row, including every valid payload for ambiguous PLY.
- [ ] Disabled widgets expose the exact reason through production hover timing
      and cannot dispatch; prerequisites become enabled in linear order.
- [ ] Supported imports traverse the actual Path/combo/Import controls and
      finish through the same queued runtime path used by the application.
- [ ] Generated route-class substitutes cover local XYZ/OBJ without committing
      or assuming provenance for user datasets.
- [ ] The real-control matrix, focused Vulkan visibility smokes, and default
      CPU-supported gate pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure \
  -R '^SandboxEditorPresentation\.FileImportWorkflowMatrix/' \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180

cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '^RuntimeSandboxAcceptanceGpuSmoke\.(ImportedOffOriginObjTriangleAutoFramesAtCenter|ImportedObjWithoutAuthoredUvsSamplesGeneratedAlbedoTexture|ImportedModelSceneIsVisibleAndClickPickable)$' \
  -L gpu -L vulkan --timeout 120

cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Coordinate-based production automation, screenshot-only assertions, or a
  public setter that bypasses the real widgets.
- Treating local untracked datasets as repository fixtures.
- Calling synchronous geometry import from the real control matrix.
- Recomputing route/readiness/companion rules in the test or app.

## Maturity

- Target: `Operational` through the app-linked Null-window real-control matrix;
  existing opt-in Vulkan smokes remain the operational visibility evidence for
  renderer behavior and are cited, not duplicated.
