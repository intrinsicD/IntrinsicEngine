---
id: UI-055
theme: I
depends_on: [RUNTIME-273]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive panel slice; evidence is the ImGui action test, the live Xephyr Sandbox session (screenshots of preview and plots) and the CPU gate.
contract_schema: 1
contracts: [method.engine-integration]
---
# UI-055 — Coherent Point Drift editor panel

## Completion — 2026-09-28
Commit: the enclosing `claude/cpd` commit records this retirement.
View > Coherent Point Drift (plus Mesh/Graph/PointCloud > Processing redirects) in
`Sandbox.MeshProcessingPanels.cpp`: source/target combos with swap and selection
following, position properties, field-table controls with hints, Start/Restart,
Step, Step 10, Run to end, Cancel, Apply, Discard, phase/backend/points/iterations/
termination/sigma²/matched weight/mean move, ImPlot sigma² (log) and objective +
matched-weight plots, CSV trace export (`exports/cpd-trace-<stamp>.csv`), and a live
viewport preview of the moving source through the new
`SceneInteractionModule::SetPreviewOverlay` channel. Verifying the preview exposed
two renderer bugs fixed here: transient debug primitives ignored the camera (packets
are now world space with world-radius round point sprites, pinned by
`TransientDebugSurfaceGpuSmoke.WorldSpacePacketsFollowTheCameraAndPointsKeepTheirRadius`)
and BUG-227 (scene image flipped with the fullscreen-pass count).

## Goal
A proper CPD panel (Registration → Coherent Point Drift, plus domain Processing
redirects) that covers at least Framework24's CPD system and makes the method
inspectable while it runs.

## Framework24 parity checklist (`lib_bcg_viewer/src/bcg_system_coherent_point_drift.cpp`)
- Set source / set target from the selection; capture the current transforms.
- Variant tabs: rigid, affine, nonrigid, bayesian (METHOD-050).
- Nonrigid kernel choice (Framework24 offers Gaussian, inverse multiquadric,
  rational quadratic, mean shift; the CPD paper uses Gaussian — offer the others
  only if METHOD-015/GEOM-059 provide them with tests) and low-rank eigenpair count.
- Source/target subsampling with grid strategies and progressive subsampling.
- Anderson acceleration toggle and window size.
- Convergence criteria (relative/absolute RMSE, sigma², delta) and max iterations.
- E-step strategy with KD-tree threshold → here the METHOD-049 policy and tolerance.
- Init / Step / Converge controls with live preview of the moving source.
- Trace plots of RMSE, sigma² and delta per iteration, with export (CSV).
- BCPD: upsampling and the spectral (low-rank) option.

## Checklist disposition
- Set source/target from the selection: present (target follows when two entities are selected; swap button). Transforms are captured at start and guarded at apply.
- Variants: rigid, affine, nonrigid present; bayesian owned by METHOD-050 (its UI row).
- Kernel choice and low-rank eigenpair count: CPD uses the Gaussian kernel only (METHOD-015); low-rank `G` and E-step policy/tolerance owned by METHOD-049 (its UI row). No other kernels without tested geometry support.
- Subsampling / progressive subsampling and BCPD upsampling: owned by METHOD-050 (upsampling) and METHOD-049 (scan-size policies); the panel reports the nonrigid point cap reason meanwhile.
- Anderson acceleration: deferred to METHOD-049 as an acceleration option (not in the reference).
- Convergence criteria and max iterations: present (tolerance on the objective, sigma² floor, initial sigma², iteration cap).
- Init/Step/Converge with live preview: present; nothing publishes before Apply.
- Trace plots and CSV export: present (sigma², objective, matched weight; RMSE is not a CPD quantity and is replaced by the objective).

## Acceptance criteria
- [x] Every checklist item is present or explicitly dispositioned in this file.
- [x] Requested/actual backend and variant, readiness/disabled reasons and cancel are shown; results are undoable through history (undo verified live).
- [x] Live preview does not publish until the user applies.
- [x] Real ImGui action test `SandboxProcessingPanels.CoherentPointDriftPanelStepsRunsAndApplies` (select, pick target, start, step, run to end, apply); live Xephyr session with screenshots of the preview overlay, sigma²/objective plots, paused and finished apply, the stale-input rejection message, and undo.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | As RUNTIME-273. |
| Compatible entity sources | As RUNTIME-273. |
| RuntimeModule | UI only; drives RUNTIME-273 commands. |
| Config/agent | Edits `sandbox.coherent_point_drift` through its validator. |
| UI | This panel and domain redirects. |
| Publication | Through RUNTIME-273. |
| End-to-end tests | ImGui action tests and live evidence. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
