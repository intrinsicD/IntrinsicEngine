---
id: UI-055
theme: I
depends_on: [RUNTIME-273]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from the 2026-09-27 Framework24 gap audit; implementation owes its own tests and evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# UI-055 — Coherent Point Drift editor panel

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

## Acceptance criteria
- [ ] Every checklist item is present or explicitly dispositioned in this file.
- [ ] Requested/actual backend and variant, readiness/disabled reasons and cancel are shown; results are undoable through history.
- [ ] Live preview does not publish until the user applies (or publishes per step only in step mode, documented).
- [ ] Real ImGui action tests (pattern of `Test.SandboxProcessingPanels.cpp`) for run, step, cancel and apply; screenshot evidence of the plots in a live session.

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
