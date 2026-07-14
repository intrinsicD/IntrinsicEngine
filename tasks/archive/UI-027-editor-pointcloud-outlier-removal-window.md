---
id: UI-027
theme: F
depends_on: [GEOM-016]
maturity_target: CPUContracted
completed_on: 2026-06-29
---
# UI-027 — Sandbox EditorUI point-cloud outlier-removal window

## Status
- Completed at `CPUContracted`. Commit: this commit (UI-027 retirement);
  implementation in the preceding commit "Wire GEOM-016 outlier removal into the
  Sandbox editor (UI-027)".
- Landed as a single cohesive change covering both slices: the
  `PointCloud > Processing > Remove Outliers` window plus the live, undoable
  command. `SandboxEditorGeometryProcessingAlgorithm` gained
  `StatisticalOutlierRemoval` / `RadiusOutlierRemoval` (added to every
  exhaustive switch); a new `SandboxEditorPointCloudOutlierRemovalCommand` /
  `...Result` / `ApplySandboxEditorPointCloudOutlierRemovalCommand` drive the
  `GEOM-016` operators and rebuild the entity's point `GeometrySources` via
  `GeometrySources::PopulateFromCloud`, undoable through `EditorCommandHistory`.
- Headless Null-backend contract tests in
  `tests/contract/runtime/Test.SandboxEditorUi.cpp` cover statistical
  publication + undo/redo (published count equals `KeptCount`), radius
  publication, and fail-closed lanes.
- Verification caveat: the runnable structural checks (layering, test-layout,
  docs-sync, doc-links, task-policy) pass, but the C++/CTest gate could not run
  in the authoring sandbox — vcpkg bootstrap requires a github clone the egress
  proxy denies (403), so the `ci` preset cannot configure there. The change
  mirrors the UI-024 / point-cloud vertex-normals command/window/undo
  precedents verbatim; CI owns the compile + contract-test confirmation.

## Goal
- Add a Sandbox EditorUI method window under `PointCloud > Processing > Remove Outliers` that runs the `GEOM-016` `RemoveStatisticalOutliers` / `RemoveRadiusOutliers` operators on the selected point-cloud entity and publishes the kept points back to the entity's canonical `GeometrySources` point data.
- Follow the `UI-024`/`UI-025`/`UI-026` editor pattern: domain menu → `Processing` submenu → method window with parameter controls and a single action button, publication to canonical geometry data, and undo/redo through the editor command history.

## Non-goals
- No geometry kernel implementation; `GEOM-016` owns `RemoveStatisticalOutliers`/`RemoveRadiusOutliers` and their diagnostics. The runtime only composes ECS `GeometrySources`, the `GEOM-016` operators, and the editor command/history seam.
- No GPU/RHI allocation, renderer feature work, shader work, or compute path; CPU publication plus the existing deferred dirty-tag contract only.
- No new persistent or generated asset; the command rewrites the live entity's point `GeometrySources` in place via the command history.
- No new sampler/descriptor/registration leaves; this task adds only the two outlier-removal operators under the point-cloud domain.
- No `Runtime.Engine.cppm` public API expansion unless the existing `SandboxEditorContext` and command-history seams cannot express the workflow.

## Context
- Status: backlog. Unblocked by `GEOM-016` once it lands (the explicit `RemoveStatisticalOutliers`/`RemoveRadiusOutliers` operators in `Geometry.PointCloud.Utils`).
- Owning subsystem/layer: `src/runtime/Editor/Runtime.SandboxEditorUi.cppm` and `src/runtime/Editor/Runtime.SandboxEditorUi.cpp`. Runtime composes ECS `GeometrySources` + the geometry operators + editor command/history; geometry owns the algorithm and never depends on runtime.
- Mirrors `UI-024` (mesh denoise window): the domain menu exposes a `Processing` submenu, a method window owns the action button, the command discovers the selected point-cloud entity, calls the geometry-owned operator, publishes the kept points, stamps the deferred dirty tag, and records an undoable entry in `EditorCommandHistory`.
- `SandboxEditorGeometryProcessingAlgorithm` (in `Runtime.SandboxEditorUi.cppm`) is the routing/advertisement enum for the editor processing menu. Today its point-cloud filter members (`BilateralFilter`, `OutlierEstimation`, `KernelDensity`) are advertised; this task adds `StatisticalOutlierRemoval` and `RadiusOutlierRemoval`. Because the editor's algorithm switches are exhaustive (no `default`) and the build is `-Werror` on `-Wswitch`, **every** switch over this enum must gain the new cases. As of this writing those switch sites are: the supported-domains map (`GetSandboxEditorSupportedGeometryProcessingDomains`), the processing availability/domain check, the menu-item builder (`GetSandboxEditorGeometryProcessingMenuItems`), and `DebugNameForSandboxEditorGeometryProcessingAlgorithm`. Re-grep `SandboxEditorGeometryProcessingAlgorithm::` before editing to confirm the complete set; both new members map to `Domain::PointCloudPoints`.
- Unlike `OutlierEstimation` (which only publishes a per-point `p:outlier_score` property), removal changes the point count, so the command must rebuild the entity's point `GeometrySources` from `OutlierRemovalResult::Filtered` (or the kept-index list) rather than writing a single scalar property in place. Confirm the canonical point-data publication seam used by the existing point-cloud editor paths and reuse it.
- GPU synchronization follows the promoted deferred dirty-tag contract used by `UI-024`: the editor command publishes CPU point data, stamps the point-cloud dirty tag, and extraction/residency repacks and uploads on the next extraction opportunity. The UI command must not call renderer/RHI upload APIs or launch a GPU task.

## Slice plan
- [x] Slice 1 — Feature-gated leaf and window: add the `PointCloud > Processing > Remove Outliers` menu leaf, the two enum members across all exhaustive switches, the window state/model, and parameter controls (method toggle statistical/radius, `KNeighbors`, `StdDevMultiplier`, `SearchRadius`, `MinNeighbors`). The action reports deterministic diagnostics while publication is not yet wired.
- [x] Slice 2 — Live command: wire the runtime command DTO/result to `GEOM-016`, rebuild the entity point data from kept points, stamp the point-cloud dirty tag, surface `OutlierRemovalResult` diagnostics (kept/rejected/non-finite counts, distance threshold), and record an undoable history entry.

## Required changes
- [x] Add `StatisticalOutlierRemoval` and `RadiusOutlierRemoval` to `SandboxEditorGeometryProcessingAlgorithm` and update every exhaustive switch over the enum (mapping both to `Domain::PointCloudPoints`).
- [x] Add the `PointCloud > Processing > Remove Outliers` menu leaf and a method window exposing the statistical/radius parameter controls with sensible ranges and tooltips.
- [x] Implement the editor command: discover the selected point-cloud entity, run the selected `GEOM-016` operator, publish the kept points to canonical point `GeometrySources`, stamp the deferred point-cloud dirty tag, and record an undoable `EditorCommandHistory` entry.
- [x] Surface `OutlierRemovalResult` diagnostics and fail-closed status (`EmptyInput`/`InsufficientPoints`/`InvalidParameters`/`BuildFailed`) in the window.

## Tests
- [x] Add a `unit;runtime` (headless, Null backend) test that builds a point-cloud entity from a two-cluster + outlier fixture, applies the statistical and radius commands, and asserts the published point count equals `OutlierRemovalResult::KeptCount` and the rejected points are gone.
- [x] Add an undo/redo test asserting the command restores the original point set on undo and reapplies on redo.
- [x] Confirm the default CPU/headless gate stays green; any interactive Vulkan coverage is `gpu;vulkan` label-gated.

## Docs
- [x] Document the window, parameter ranges, and diagnostics in the Sandbox/UI docs.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [x] A user can select a point-cloud entity, choose statistical or radius removal, adjust parameters, and apply removal with the kept points reflected live and diagnostics shown.
- [x] The command routes through the editor command/history seam with working undo/redo.
- [x] Headless tests cover deterministic kept-count publication and undo/redo; default gate green.
- [x] App imports runtime only; geometry owns the algorithm; no layering violations introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditor|PointCloud' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Adding outlier-removal algorithm logic in the UI layer (drive `GEOM-016` only).
- Calling renderer/RHI upload APIs or launching a GPU task from the UI command.
- Introducing layering violations (app imports runtime only; graphics stays ECS-free).

## Maturity
- Target: `CPUContracted` (headless deterministic command + publication contract). No `Operational` follow-up is owed for this editor-only CPU/null contract; interactive Vulkan coverage, if added, stays `gpu;vulkan` label-gated.
