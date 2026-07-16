---
id: BUG-093
theme: G
depends_on: []
maturity_target: Operational
---
# BUG-093 — File / Import prerequisite gating and disabled-reason tooltips

## Status

- Completed on 2026-07-16 at maturity `Operational`; owner: Codex; branch:
  `agent/bug-093-file-import-prerequisites`; implementation commit:
  `d97cd893`.
- Runtime and app presentation now share fail-closed route, promoted-importer,
  and payload-hint readiness. Focused coverage passed 6/6, including the real
  File / Import window and disabled-hover tooltip path inside Null-window
  `Engine::Run()`.

## Goal

- Make the Sandbox `File / Import` workflow fail closed on invalid route,
  payload-hint, and promoted-importer states, while every disabled hint/import
  affordance explains the next prerequisite through a deterministic hover
  tooltip.

## Non-goals

- No primary-file or GLTF companion-file read/parse preview; asynchronous source
  and dependency inspection is owned by `ASSETIO-010`.
- No native file dialog, path browser, drag/drop behavior change, or automatic
  import when the path changes.
- No KTX/KTX2 decoder. Those extensions remain recognized only so the promoted
  path can report deterministic unsupported-format diagnostics.
- No app-layer import of assets, core, geometry, ECS, graphics, platform, or
  RHI modules; `app -> runtime` remains the only Sandbox dependency direction.
- No new widget service/module or generic UI framework; host the app-internal
  free function in the existing editor-shell module so existing panel
  implementation units can reuse one convention.

## Context

- Owners/layers: runtime owns route/capability evaluation, facade models, and
  dispatch-time command validation; `src/app/Sandbox/Editor` owns only ImGui
  presentation and consumes the copied runtime model.
- Symptom: `SandboxEditorFileImportModel::Enabled` currently reflects only
  whether the import command surface exists. `Sandbox.EditorShell.cpp` uses
  that one value to enable the path, every payload-hint choice, and `Import
  asset`, so a non-empty but incompatible request can look actionable and reach
  the command surface before the router rejects it.
- Live reproduction (2026-07-16): in the real Sandbox editor, an empty path
  left `Import asset` enabled with no hover explanation and failed only after
  click as `InvalidPath`; entering checked-in companion-only
  `assets/models/Duck0.bin` likewise remained enabled and failed only after
  submission as `AssetUnsupportedFormat`.
- Reproduction: the checked-in contract case
  `SandboxEditorUi.FileImportCommandRoutesThroughRuntimeOwnedSurface` supplies
  `assets/models/Duck.gltf` with an explicit `Graph` hint and still observes an
  enabled file-import model. The GLTF route has exactly one promoted payload,
  `ModelScene`, so `Graph` is deterministically incompatible without reading
  the file or relying on a temporary fixture.
- `Asset.ImportRouter` already owns case-insensitive extension classification,
  supported import-payload lists, PLY ambiguity, and deterministic
  `MissingExtension`, `UnsupportedExtension`, `AmbiguousPayloadKind`, and
  `PayloadKindNotSupported` diagnostics. Runtime must consume that contract;
  the app must not duplicate its format table.
- KTX/KTX2 are intentionally recognized by the router while
  `IsSupportedTextureImportFormat()` and the promoted decoder registration omit
  them. Route readiness therefore includes concrete promoted importer
  capability, not extension classification alone.
- Expected behavior: path entry stays editable; the hint chooser becomes
  actionable only for a recognized, promoted import format; each incompatible
  hint is disabled with a reason; `Import asset` is enabled only when the
  command surface, route, concrete importer, and selected hint are ready.
- Impact: invalid requests currently fail late and disabled controls provide no
  discoverable next action, making the editor appear broken even though the
  lower-layer router is behaving correctly.

## Required changes

- [x] Extend `SandboxEditorFileImportModel` with `CanChoosePayloadHint`,
      `CanImport`, `ResolvedPayloadKind`, deterministic payload-option records
      (`Kind`, `Enabled`, `DisabledReason`),
      `PayloadHintDisabledReason`, and `ImportDisabledReason`; preserve
      `Enabled` as command-surface availability rather than overloading it with
      form validity.
- [x] Add one private runtime prerequisite evaluator shared by
      `BuildFileImportModel(...)` and
      `ApplySandboxEditorFileImportCommand(...)`. It must use
      `DiagnoseAssetImportRoute(...)` plus the promoted model/texture capability
      predicates and must not read or parse the source file.
- [x] Make the evaluator return reasons in a stable priority order: unavailable
      command surface, empty path, missing/unsupported extension, unavailable
      promoted importer, ambiguous hint, then incompatible explicit hint.
- [x] Populate the six payload options in stable enum order. Allow `Unknown`
      auto-resolution for single-payload formats; disable it for ambiguous PLY;
      enable only `Mesh` and `PointCloud` for PLY; disable every incompatible
      explicit kind with a format-specific reason.
- [x] Revalidate route, importer capability, and hint immediately before
      dispatch so a direct caller cannot bypass the presentation gate and an
      invalid command never invokes the runtime import callback.
- [x] Keep the path field independently editable, and split the app's current
      all-controls disabled scope into separate hint and `Import asset` scopes
      driven by the runtime model.
- [x] Add one app-internal free-function convention in the existing editor-shell
      module using
      `ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled`, and
      apply it to the payload combo, disabled payload rows, `Import asset`, and
      the existing asset-queue `Clear completed`/`Cancel` controls that already
      carry runtime-owned disabled reasons. Keep it reusable by the existing
      app panel implementation units for `UI-037`; do not add a widget module,
      service, class, or validation policy.
- [x] Keep expected incomplete form state out of persistent diagnostics;
      diagnostics remain for unavailable subsystems and submitted-command
      failures, while form guidance lives in status text and tooltips.

## Tests

- [x] Extend runtime facade contracts for missing command surface, empty path,
      missing/unsupported extension, unavailable KTX importer, OBJ + `Graph`,
      PLY + `Unknown`, both valid PLY hints, XYZ + `Unknown`, and GLB +
      `Unknown`; assert resolved payload, per-option availability, and exact
      disabled-reason priority.
- [x] Turn the checked-in `assets/models/Duck.gltf` + `Graph` case into a
      regression: `Enabled` remains true, `CanImport` is false, the reason names
      the `ModelScene` requirement, and dispatch does not call the fake import
      surface until a compatible hint is supplied.
- [x] Add app-linked presentation coverage proving the real `File / Import`
      window renders its independently disabled controls and that hovering a
      disabled import control produces the runtime-owned reason through the
      `AllowWhenDisabled` path.
- [x] Cover queue `Clear completed` and row `Cancel` disabled reasons through
      the same tooltip helper without changing their command behavior.
- [x] Preserve the existing source/link contract proving every Sandbox editor
      implementation imports runtime only.

## Docs

- [x] Update `src/app/Sandbox/README.md` with the linear path -> hint -> import
      interaction and disabled-reason behavior.
- [x] Update `src/runtime/README.md` with the runtime-owned prerequisite model,
      dispatch-time fail-closed validation, and the explicit deferral of file
      and companion reads to `ASSETIO-010`.
- [x] Regenerate `docs/api/generated/module_inventory.md` if the exported
      facade model surface changes, and refresh `tasks/SESSION-BRIEF.md` when
      the task changes lifecycle state.

## Acceptance criteria

- [x] An incompatible or ambiguous path/hint pair cannot dispatch an import,
      and its exact next action is visible from the disabled control.
- [x] A valid single-payload or explicitly disambiguated PLY route enables
      import without app-owned format knowledge.
- [x] KTX/KTX2 remain deterministically unavailable despite router
      recognition; no unsupported decoder is advertised.
- [x] Direct command calls and the app panel share the same runtime validation
      result, and no invalid command reaches the import callback.
- [x] The canonical app-linked Null-window `Engine::Run()` integration test
      exercises the real `File / Import` window and disabled-tooltip path.
- [x] Layering remains `app -> runtime`, with no file IO/decode in ImGui panel
      construction and no new service/registry/widget framework.

## Verification

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict

cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi\.FileImport|SandboxEditorPresentation\.FileImport' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Verification completed on 2026-07-16:

- The focused runtime and app-linked presentation selection passed 6/6. The
  integration case opened the real File / Import window inside Null-window
  `Engine::Run()`, hovered the disabled import button, and observed an active
  tooltip window. Runtime string contracts and source-linked helper assertions
  pin the reason propagation; queue clear/cancel reuse is source-contracted.
- The aggregate `IntrinsicTests` target built successfully and the default
  CPU-supported gate passed 3,792/3,792 in 468.28 seconds.
- Strict task policy, task schema, layering, test-layout, documentation-link,
  root-hygiene, PR-contract, and diff checks passed. Regenerating the public
  module inventory produced no diff and retained 386 modules.

## Forbidden changes

- Reading, statting, or parsing the candidate asset synchronously from the
  app/ImGui phase or once per frame.
- Moving route tables, decoder-capability policy, or command validation into
  `src/app`.
- Treating `SandboxEditorFileImportModel::Enabled` as equivalent to
  `CanImport`, or silently rewriting an incompatible user-selected hint.
- Enabling KTX/KTX2 without a separately scoped promoted decoder and tests.
- Shipping tooltip text owned only by app code when the runtime model can
  supply the deterministic reason.
- Adding a second disabled-tooltip flag convention or wrapper instead of the
  one app-internal free function shared with `UI-037`.
- Mixing companion preflight, native dialogs, drag/drop changes, or unrelated
  editor refactors into this bug fix.

## Maturity

- Target: `Operational` through an app-linked integration test that runs the
  canonical editor callback inside Null-window `Engine::Run()` and exercises
  the real disabled-tooltip presentation path; route and dispatch behavior are
  also pinned by the default CPU contract gate.
- Achieved: the canonical app-linked Null-window test exercises the real
  disabled-hover path, while the runtime facade and dispatch contracts pin
  exact route, capability, resolution, reason-priority, and callback behavior.
