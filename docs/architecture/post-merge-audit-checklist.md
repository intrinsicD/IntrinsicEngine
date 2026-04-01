# Post-Merge Audit Checklist

Use this checklist for architecture-touching merges (runtime loop, render graph, ECS lifecycle, module-boundary refactors, EditorUI controllers).

## 1) Contract stability

- [ ] New/changed API has at least one compile-time or CPU-side contract test.
- [ ] Any behavior-sensitive path has one regression test for the prior failure mode.
- [ ] Degenerate/edge inputs are covered for the changed logic.

## 2) Runtime safety and performance

- [ ] No hot-path heap allocation introduced in per-frame code.
- [ ] Telemetry markers/counters exist for newly introduced phase work.
- [ ] Frame budget delta is measured and recorded (CPU + GPU where relevant).

## 3) Graph ownership alignment

- [ ] CPU Task Graph responsibilities remain in simulation/extraction/prep lanes.
- [ ] GPU Frame Graph owns pass/resource dependency changes.
- [ ] Async Streaming Graph handles deferred heavy work (not UI thread, not draw loop).

## 4) Configuration ownership

- [ ] New tunables have a single owner module (no duplicated policy constants).
- [ ] Feature toggles have deterministic precedence and conflict logging.
- [ ] README/TODO/ADR references are synchronized with implementation state.

## 5) High-churn UI gate

For EditorUI controller churn above 120 changed lines, `tests/Test_EditorUI.cpp` must be updated in the same PR.

Suggested check:

```bash
tools/check_ui_contract_guard.sh origin/main 120
```
