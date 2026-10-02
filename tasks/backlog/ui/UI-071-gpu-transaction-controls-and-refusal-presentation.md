---
id: UI-071
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI consolidation; evidence is the diff, the ImGui panel tests, review and CI.
contract_schema: 1
contracts: [runtime.editor-prepared-frame-locality, repo.source-documentation]
---
# UI-071 — One Stop/Accept/Discard row and one disabled-reason presentation

## Goal
Every Sandbox panel that drives a two-phase GPU transaction draws its Stop/Accept/Discard row
through one shared helper in `Sandbox.PanelSupport.*`, and every disabled action shows its
reason in the same way.

## Context
Source: 2026-10-01 duplication/consistency audit (findings 1.4, 3.1, 3.4), re-verified at
`665c693dd`. All paths are under `src/app/Sandbox/Editor/`; MPP = `Sandbox.MeshProcessingPanels.cpp`,
MP = `Sandbox.MethodPanels.cpp`. Line numbers are at the audit revision.

Hand-written row sites: MPP ~207-212 (scalar transactions), ~2186-2199 (Outliers), ~3221-3229
(CPD), ~3409-3424 and ~3636-3655 (Normals, Smoothing); MP ~1617-1635 (consolidation),
~1916-1923 (K-Means).

Inconsistencies today:
- Accept refusal reason: wrapped text (scalar, Outliers), tooltip (Normals), both
  (Smoothing), via `DrawProcessingActionButton` (consolidation), not shown at all (K-Means).
- Discard: always enabled for scalar, Outliers, K-Means and consolidation, but disabled by phase
  for Normals and Smoothing. The scalar row switches one button between "Stop" and "Discard".
- IDs: the scalar row uses unsuffixed `"Accept"`; the others use `##Family` suffixes.
- Dead guard: `&& transaction.CanAccept` after an already-disabled button (Normals, Smoothing).
- Upload/cache-hit counters are formatted four ways (MPP ~205, ~2185; MP ~1625, ~1910-1912).
- Disabled-reason presentation has four styles: tooltip plus inline text (9 MPP sites), grey text
  after the button (MP Poisson, MP ~1678), tooltip only (topology buttons), and no reason at all
  in MP, which has 17 `BeginDisabled` blocks and no `DrawDisabledReasonTooltip` call.

Coordination, do not duplicate:
- [UI-069](../../done/UI-069-shared-operation-progress-widget.md) owns the shared progress read model and
  widget; this row sits next to it and must reuse its phase/progress data once it lands.
- [UI-037](../../active/UI-037-linear-domain-action-readiness-tooltips.md) and
  [UI-058](UI-058-all-reasons-readiness-tooltips.md) own readiness content and the all-reasons
  tooltip. This task decides only the presentation rule (where the reason appears) and records it
  there; `DrawProcessingActionButton` draws it.
- [RUNTIME-311](../runtime/RUNTIME-311-unify-gpu-scalar-outlier-transaction-lifecycle.md) unifies the
  runtime lifecycle, and may expose one phase/snapshot shape this helper should consume.
- `ProcessingDraftState` helpers and widths belong to UI-049 and are out of scope.

Proposed shape (confirm against the snapshot types before coding):
`DrawGpuTransactionControls(view, onStop, onAccept, onDiscard, idSuffix)` where `view` carries
phase, `CanAccept`, the refusal reason and the IO counters. It owns the Stop-while-running vs
Discard-by-phase rule, the unique IDs and one counter format.

## Acceptance criteria
- [ ] One helper draws the row for scalar, Outliers, CPD, Normals, Smoothing, consolidation and K-Means; the seven hand-written rows are removed.
- [ ] A single rule decides when Discard is enabled (by transaction phase) and where the Accept refusal reason is shown (tooltip on the disabled control plus one inline line); K-Means shows its reason.
- [ ] Buttons use `##Family`-suffixed IDs; the upload/cache-hit counters use one format.
- [ ] MP's disabled actions show their reasons through the shared presentation; the rule is recorded in UI-037/UI-058 or the sandbox editor boundaries doc.
- [ ] The per-phase enabled/disabled rule of Accept and Discard is covered by the shared helper's per-phase test (`GpuTransactionRowEnablesButtonsByPhase`), the call-site snapshot adapters (`GpuTransactionPhaseOf` static_asserts, panel tests on the null device) and a source scan that no hand-written row remains. MP's running phases are unreachable on the null device, so no MP panel is driven through them; no behavior change to the runtime commands.

## Verification
```bash
cmake --build build/ci -j$(nproc)
ctest --test-dir build/ci -R 'SandboxEditor|SandboxProcessingPanels|SandboxEditorMeshMethods|SandboxEditorClusteringMethods' -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60
bash tools/repo/check_ui_contract_guard.sh
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Slice log
- Slice 1: `DrawGpuTransactionControls(view, idSuffix)` in `Sandbox.PanelSupport.*` returns the pressed
  `GpuTransactionRowAction` (the caller invokes its family command; no callback indirection), with
  `ResolveGpuTransactionRowState` (the per-phase enabled rule, testable without drawing) and one counter
  formatter. The runtime's transactions share only `EditorGpuTransactionPhase` (the typed snapshots have no
  common shape), so each family adapts its own snapshot at its call site. Adopters: scalar and Outliers.
  Recorded unifications: the scalar row now shows a disabled Accept while running and Discard (its old
  "Stop" cancelled the run, as Discard does), its Accept ID is `Accept##Scalar`, and Outliers' Discard is
  disabled once the transaction is terminal (it was never drawn then). Tests:
  `GpuTransactionRowEnablesButtonsByPhase` (running, awaiting accept, stale, accepting, terminal),
  `GpuTransactionCountersUseOneFormat`; the scalar panel test now clicks `Accept##Scalar`.
- Slice 2: Normals and Smoothing use the row (Smoothing with Stop). The snapshots (scalar, Outliers, Normals,
  Smoothing) now carry the lifecycle's own `GpuTransactionAcceptRefusal` text in the non-waiting phases
  ("No GPU result waits for Accept.", "Accept is already under way."), so a disabled Accept always has a
  runtime reason; the K-Means observation keeps its state message while running. Normals' separate residency
  line now reports only the topology bundle (the upload bytes are in the shared counters line). Exception:
  CPD is not a GPU two-phase transaction (its own run object and `EditorCoherentPointDriftPhase`, with
  Step/Apply/Discard), so its row stays hand-written; only its disabled-reason presentation falls under the
  slice-4 rule. Contract tests pin the refusal text for Running and Accepting.
- Slice 3: point-cloud consolidation and K-Means use the row (both with Stop). The observation's state message is
  the Accept reason while Accept is refused (K-Means now shows it; consolidation already used it as its tooltip)
  and the state line otherwise, so it is never drawn twice; counters use the shared format (K-Means keeps its
  CPU stage upload/readback fields, consolidation shows upload and hits only). The GPU run observations only
  exist with a live Vulkan run, so no MP panel test can reach a running phase on the null device: the MP side is
  covered by the shared helper's per-phase test and by `SandboxEditorPresentation.GpuTransactionRowsAreDrawnByTheSharedHelper`
  (source scan: no hand-written Accept/Discard/Stop button outside CPD's Discard).
- Slice 4: Method-panel disabled actions go through `DrawProcessingActionButton`; the panel's own gating
  (`ReadinessUnlessBlocked`: first blocker's reason) covers consolidation Apply/Reload/Undo/Redo, K-Means
  Apply/Reload (and Run while a GPU run is pending, with the consolidation wording), the two parameterization pin
  buttons and UV Undo/Redo; the atlas button shows the validator's own message
  (`ValidateParameterizationAtlasConfig`) before the runtime preview's. Existing inline notes stay. Left as
  they are because they are not actions: the consolidation strategy combo entries ("unavailable" note) and the
  support-radius input (disabled by the radius mode, with its note). No new reason is computed from geometry;
  readiness content stays with UI-037/UI-058. The presentation rule is recorded in
  `docs/architecture/sandbox-editor-feature-boundaries.md`.
- Review fixes: the consolidation observation reports the lifecycle's accepting text ("Reading the GPU positions back.") while accepting; Outliers, Keypoints, Density, Density weights and Point spacing no longer wrap their Run button in a bare `BeginDisabled` (`ReadinessWhileGpuRunPending` supplies the pending-run reason, K-Means uses it too); CPD Step/Run to end/Apply take their reasons from `ResolveEditorCoherentPointDriftStepReadiness`/`...ApplyReadiness`; Show Gradient and the UV Undo/Redo (missing history blocker) carry reasons; `GpuTransactionPhaseOf` has static_asserts.
