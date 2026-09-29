---
id: UI-067
theme: G
depends_on: [UI-055, RUNTIME-273]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI and responsiveness work requested by the operator (2026-09-29); verified by contract tests and a live sandbox check.
contract_schema: 1
contracts: [repo.source-documentation]
---
# UI-067 — Observable, responsive registration (ICP, CPD, BCPD)

## Completion — 2026-09-29
Commit: on `claude/cpd-nystrom`, `e087e7746` (orbit pitch), `a3a5fac25` (CPD cancellation and
stages), `0b137f51d` (sphere impostor point runs), `03371204a` (BCPD subsamples), `3acfeabb8`
(shared trace plots), `873f6f846` (ICP progress and cancel), `59bd6b399` (log/flat plot axes).
Live check (release sandbox, Xephyr, two 385456-point M16 clouds): BCPD with 2000/2000
subsamples; Step ran one iteration in under a second and paused with Apply enabled; Step 10
showed "Iteration N: stage for X s" while running; Cancel during Run to end ended the run within
0.4 s ("cancelled; nothing was applied"); source/target subsamples and the moving preview drew as
shaded spheres; all trace plots updated live. ICP (20/50 iterations, 0.15 s each) showed the
running iteration, RMSE and inliers; Cancel after 1 s stopped at iteration 10 without applying.
The check found unreadable axes for zero log values and a near-constant inlier count, fixed in
`59bd6b399`. Operational.

## Goal
- Operator report (2026-09-29): after pressing Step the CPD panel stayed "Running" with only
  Restart/Cancel/Discard; registration progress is not observable; subsamples should render with
  the engine's sphere points; the orbit camera felt inverted since BUG-227 (fixed in e087e7746).
- Every registration method shows its current iteration, a live preview and plots of its
  per-iteration quantities over iterations or time, without slowing the solver; Step runs
  exactly one iteration and shows it; Cancel and Discard answer at once.

## Slices
1. CPD responsiveness: cooperative cancellation inside `Solver::Initialize` and `Step` (kernel
   builds, E-step, M-step), a stage/elapsed-time report in the snapshot, Cancel/Discard update
   the phase immediately, trace rows carry elapsed time.
2. Transient debug points drawn as sphere impostors with the forward point shading (shared
   include) in one draw instead of one draw per point.
3. BCPD subsample (source and target) exposed through Result and snapshot and drawn as spheres.
4. Shared registration trace plots (all per-iteration series, x = iteration or seconds).
5. ICP: live trace (RMSE, inliers, elapsed), cancellation, moving-source preview, plots.
6. Live sandbox check of the CPD and ICP panels.

## Acceptance criteria
- [x] A Step on a large nonrigid/BCPD run shows its stage and elapsed time; Cancel ends it within one kernel row block or E-step chunk; Apply is available after a completed Step.
- [x] CPD and ICP panels plot every per-iteration quantity over iterations or seconds; the solver publishes at most one snapshot per iteration.
- [x] Preview and BCPD subsamples render as shaded spheres in a single draw.
- [x] Contract tests for cancellation, stage reporting and the transient point draw shape.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift|Registration|TransientDebug|RuntimeCameraControllers' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```
