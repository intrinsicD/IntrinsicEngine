---
id: UI-038
theme: F
depends_on: []
maturity_target: Operational
---
# UI-038 — Progressive Poisson destructive-conversion safety

## Goal
- Make the existing in-place Progressive Poisson mesh-to-point-cloud conversion
  explicit and recoverable: runtime provides a side-effect-free conversion
  preview, requires a fresh confirmation for destructive apply, and commits
  the conversion as one undoable command used identically by UI and agent/CLI
  apply callers. Config supplies validated parameters but never confers
  confirmation.

## Non-goals
- No change to Progressive Poisson sampling mathematics, seed determinism, channel semantics, CPU/GPU backend resolution, or point-cloud-to-point-cloud operation.
- No implicit creation of a second entity and no broad geometry-domain conversion framework; this task secures the existing selected-entity mesh-to-point-cloud operation.
- No UI-only confirmation policy, app-owned geometry snapshot, or persistent “do not ask again” bypass.
- No confirmation for ordinary config preview/apply or a non-destructive Progressive Poisson run whose selected entity is already a point cloud.

## Context
- Owner/layers: `src/runtime/Runtime.SandboxEditorFacades.*` and the existing editor-command history own preview, confirmation validation, mutation, undo, and redo. `src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp` renders the preview/confirmation surface and calls the runtime facade only.
- The current mesh path samples a surface and replaces the selected entity's mesh `GeometrySources` with point-cloud sources. The Run button and `AutoRunOnEdit` share that direct command path, so a routine edit can irreversibly remove mesh topology without an explicit warning or a real undo record.
- The preview is a plain runtime value containing stable entity identity and source revision, source/target domains, source vertex/face counts, requested output count, the mesh/topology/property state that will be replaced, and a concise warning. Preview has no side effects and yields a confirmation token bound to the relevant entity, source generation, and effective config.
- Runtime/session state is authoritative for a pending conversion and its token. The app may render ImGui's modal lifecycle but owns no pending-conversion truth, validation result, geometry/config snapshot, or permission to bypass confirmation.
- Control surfaces are co-equal: UI requests preview then submits the confirmed runtime command; agent/CLI callers use those same two operations; config files and editor widgets continue through the existing Progressive Poisson preview/validate/apply lane. `AutoRunOnEdit` may update config or request a preview, but cannot confirm a destructive conversion.
- Config preview/apply may validate and store Progressive Poisson parameters or
  request a conversion preview; only an explicit UI or agent/CLI apply caller
  may submit the fresh runtime confirmation token. Reading/applying a config
  file is never user confirmation.
- Backend axis is unchanged. Direct CPU, derived-job, and promoted GPU/fallback paths obey the same confirmation and history contract, and asynchronous completion rechecks token/source freshness before mutation.

## Slice plan
- **Slice A — Preview and token contract.** Add side-effect-free runtime
  preview, stable consequence data, confirmation-token identity/freshness, and
  fail-closed contracts for cancel/mismatch/consumption without mutating ECS or
  history.
- **Slice B — Atomic apply/history.** Route direct and asynchronous backend
  completions through one generation-validated history command, with exact
  deterministic before/after state for Undo/Redo and stale-result discard.
- **Slice C — App operational proof.** Present the runtime preview in the real
  Sandbox confirmation flow, prove Cancel/Confirm behavior and auto-run
  non-bypass, and cite the app integration before claiming `Operational`.

## Required changes
- [ ] Add right-sized runtime records/functions for a side-effect-free mesh-conversion preview and confirmed command. The preview reports what will be replaced and returns a token tied to stable entity ID, geometry/source generation, and the effective Progressive Poisson config.
- [ ] Require a valid unconsumed token for every mesh-to-point-cloud apply entry point. Missing, cancelled, consumed, entity-mismatched, config-mismatched, or stale-generation confirmation fails closed with an actionable result and no entity/history mutation.
- [ ] Represent pending preview/confirmation in the existing runtime Sandbox editor session/facade model as plain data; do not add a one-consumer service/controller or make app-local modal state authoritative.
- [ ] Route confirmed conversion through `EditorCommandHistory` as one atomic command. Capture the complete before state needed to restore mesh `GeometrySources`, compatible vertex/face properties, presentation/visualization bindings affected by the conversion, dirty/generation state, and selection visibility; capture/store the deterministic after state so redo does not resample differently.
- [ ] Make Undo restore the exact pre-conversion mesh-domain state and Redo restore the exact sampled point-cloud state. Cancelled/failed/stale previews and failed jobs create no history entry and do not mark the scene dirty.
- [ ] For derived jobs, bind the confirmation to the submitted source/config snapshot and revalidate it at completion. Selection, entity lifetime, source generation, config, or confirmation changes before completion discard the result without mutation.
- [ ] Replace the mesh Run path with an explicit preview/confirmation flow that lists source and target domains, source/output counts, and lost/replaced topology/property information. Point-cloud runs retain the direct non-destructive path.
- [ ] Prevent `AutoRunOnEdit`, keyboard activation, agent requests, and backend/fallback variants from bypassing mesh confirmation. Auto-run may refresh/show the runtime preview, but only an explicit confirmed command may convert the entity.
- [ ] Preserve the existing validated config parameter route and source
      tagging. UI and agent/CLI apply callers submit the same preview token and
      command; neither receives a private mutation seam, and config loading
      alone can never submit or satisfy confirmation.

## Tests
- [ ] Add a runtime contract test named
      `SandboxEditorProgressivePoisson.MeshConversionPreviewIsSideEffectFree`
      that compares entity sources/properties/generations, command history,
      selection, and scene-dirty state before and after preview and cancellation.
- [ ] Add
      `SandboxEditorProgressivePoisson.MeshConversionRequiresFreshConfirmation`
      covering absent, cancelled, consumed, wrong-entity, changed-config,
      changed-source-generation, and stale asynchronous-completion tokens;
      every case fails closed without mutation/history.
- [ ] Add
      `SandboxEditorProgressivePoisson.MeshConversionUndoRedoRoundTripsGeometry`
      covering a successful confirmed conversion, one atomic history entry,
      exact mesh restoration on Undo, and exact deterministic point-cloud
      restoration on Redo, including affected properties/presentation bindings
      and selection visibility.
- [ ] Add
      `SandboxEditorProgressivePoisson.MeshConversionAutoRunCannotBypassConfirmation`
      covering config edits, debounce expiry, direct CPU, derived-job,
      requested GPU, and fallback paths. No mesh domain replacement occurs
      until the explicit confirmed command.
- [ ] Extend app integration coverage to assert the modal renders the runtime preview, Cancel is side-effect free, Confirm submits the runtime token once, and the app keeps no independent confirmation authority.
- [ ] Preserve point-cloud Progressive Poisson coverage and assert it remains directly runnable without a destructive-conversion confirmation.

## Docs
- [ ] Update `src/runtime/README.md` with the preview/token/apply contract, stale-completion behavior, and exact Undo/Redo state guarantees.
- [ ] Update `src/app/Sandbox/README.md` with the mesh-to-point-cloud warning/confirmation flow, auto-run restriction, and UI/config/agent parity.
- [ ] Regenerate `docs/api/generated/module_inventory.md` for the exported runtime facade changes.

## Acceptance criteria
- [ ] No Run, auto-run, agent, direct, derived-job, GPU, or fallback path can convert a mesh entity to point cloud without a fresh runtime-issued confirmation for the current entity/source/config.
- [ ] Preview and Cancel are side-effect free; failed or stale confirmation leaves entity state and command history unchanged and reports why a new preview is required.
- [ ] Confirmed apply is one undoable history command; Undo exactly restores the mesh-domain state and Redo exactly restores the previously sampled point-cloud state without resampling drift.
- [ ] The Sandbox presents the conversion consequences before confirmation, while runtime owns all pending-conversion, validation, mutation, and history truth.
- [ ] UI and agent/CLI apply callers, across every backend, use the same runtime
      preview and confirmed-command seam. Config supplies validated parameters
      only and cannot confirm; point-cloud-to-point-cloud Progressive Poisson
      remains non-destructive and unaffected.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorProgressivePoisson\.(MeshConversionPreviewIsSideEffectFree|MeshConversionRequiresFreshConfirmation|MeshConversionUndoRedoRoundTripsGeometry|MeshConversionAutoRunCannotBypassConfirmation)$' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No mesh-to-point-cloud mutation before explicit confirmation and no bypass through auto-run, agents, async completion, backend selection, or CPU fallback.
- No interpretation of config load/apply, config source tags, or auto-run state
  as destructive-conversion confirmation.
- No UI-owned source snapshot, confirmation token authority, validation logic, undo record, geometry/domain state, or separate mutation path.
- No dirty/history entry for preview, cancellation, rejection, stale completion, or failed sampling.
- No lossy Undo, nondeterministic resampling on Redo, or partial restoration of geometry properties/presentation state.
- No global geometry conversion framework, new one-consumer service/controller, second-entity workflow, algorithm change, or unrelated panel/runtime refactor.

## Maturity
- Target: `Operational` through the real runtime command-history and
  app-presentation integration path. CPU/null coverage is sufficient for this
  workflow-safety contract; no Vulkan-specific follow-up is owed.
