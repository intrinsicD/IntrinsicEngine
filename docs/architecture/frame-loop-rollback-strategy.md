# Frame Loop Rollback Strategy

This document closes the near-term TODO item:

- "Define rollback strategy before each phase starts"

The goal is to make the runtime/frame-pipeline migration reversible without leaving permanent compatibility clutter in the engine.

---

## 1. Policy

Every frame-loop migration phase must ship with:

1. an explicit rollback toggle,
2. a narrow legacy-compatible adapter shim,
3. pass/fail gates that decide whether the new path may remain enabled by default.

The design target is not a long-lived dual architecture. It is a **one-window rollback plan**: keep the fallback only long enough to protect the next cutover, then delete it.

---

## 2. Concrete runtime hook

The engine now reserves two `FeatureRegistry` descriptors for frame-loop orchestration:

- `FrameLoop.StagedPhases`
  - default-enabled
  - the intended path for the staged runtime migration
- `FrameLoop.LegacyCompatibility`
  - default-disabled
  - a rollback shim that preserves the current frame-order contract

Rollback precedence is deliberate:

- if `FrameLoop.LegacyCompatibility` is enabled, it wins;
- otherwise the engine runs `FrameLoop.StagedPhases`.

This keeps rollback activation deterministic and easy to audit in logs and tests.

---

## 3. Adapter-shim rule

The fallback path must not become a second independently evolving runtime.

Allowed shim responsibilities:

- preserve current call ordering,
- reuse the same streaming/fixed-step/render lane coordinators,
- provide a stable escape hatch during cutover,
- isolate "new shape" API churn from the old top-level loop temporarily.

Disallowed shim responsibilities:

- adding new behavior unavailable in the staged path,
- introducing separate ownership models,
- forking system registration policy,
- accumulating permanent conditional branches across the renderer.

Put differently: the shim may preserve order, but it must not preserve architectural debt.

---

## 4. Pass / fail gates for enabling a new phase

Before a migration phase remains on by default, all of the following must stay green:

### Test gates

- `tests/Test_RuntimeFrameLoop.cpp`
  - validates lane ordering and rollback-mode routing
- `tests/Test_RuntimeSystemBundles.cpp`
  - validates canonical system registration order and feature toggles
- `tests/Test_RenderUpdateIntegration.cpp`
  - validates CPU-side render/update contracts that depend on the frame loop
- `tools/check_todo_active_only.sh`
  - ensures the backlog stays active-only as phases complete

### Runtime/telemetry gates

- render-graph compile/execute succeeds without new validation errors,
- resize / pick / debug-view behavior remains stable,
- headless/runtime smoke paths remain green,
- telemetry stays within the agreed phase budget before default cutover.

The telemetry budget is phase-specific, but the default expectation is conservative:

$$
\Delta t_{\text{cpu,phase}} \le \text{agreed budget}
$$

If the measured overhead exceeds the agreed budget or a correctness gate regresses, the phase must flip back to `FrameLoop.LegacyCompatibility` until the regression is fixed.

---

## 5. Removal rule

After a phase has:

1. cleared its gates,
2. stayed stable for one migration window,
3. and no longer needs the rollback escape hatch,

delete the shim immediately.

Do not keep "just in case" legacy frame-loop branches. Git history is the long-term archive; the live engine should contain only the active path plus the current migration window.
