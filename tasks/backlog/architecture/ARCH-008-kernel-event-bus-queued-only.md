---
id: ARCH-008
theme: F
depends_on: []
---
# ARCH-008 — Queued-only kernel event bus with two pump points

## Goal
- Give the runtime kernel an `EventBus` wrapper over `entt::dispatcher` that
  is queued-only (no `trigger` in the API), pumps at exactly two main-thread
  points per frame (post-command-drain and post-simulation), and accepts
  publishes from worker threads via an internal thread-safe inbox, per
  [ADR-0024](../../../docs/adr/0024-kernel-module-architecture.md) D7.

## Non-goals
- No migration of existing direct `entt::dispatcher`/`entt::sigh` users in
  runtime/graphics; they migrate incrementally as extractions land.
- No event recording/replay (relates to the parked determinism question in
  ADR-0024).
- No renaming or absorbing `Extrinsic.Asset.EventBus` — the asset-layer bus
  is a separate, layer-local mechanism; the kernel bus must pick a
  non-colliding module/type name (e.g. `Runtime.KernelEvents`).

## Context
- Owner/layer: `runtime` (kernel spine per ADR-0024 D9).
- `entt::dispatcher` is not thread-safe; workers must never touch it. The
  inbox is merged into the dispatcher at the next pump.
- Delivery semantics (ADR-0024 D7): events published during a pump deliver at
  the **next** pump, never recursively within the current one — cascades are
  bounded and ordered.
- Consequence recorded in ADR-0024: all teardown becomes two-phase
  (announce at a pump, destroy at a boundary). `ARCH-010` consumes this for
  `WorldWillBeDestroyed`.
- Pump A runs after the `ARCH-007` drain point; pump B runs after Phase 2
  (fixed-step simulation) in `src/runtime/Runtime.Engine.cpp`. If `ARCH-007`
  has not landed yet, pump A anchors to the same pre-sim location the drain
  will occupy; the two tasks are independently landable.

## Required changes
- [ ] New kernel event-bus module (interface `.cppm` + implementation
      `.cpp`): typed `Subscribe`/`Unsubscribe` with handles, thread-safe
      `Publish` (deferred always), internal MPSC inbox for cross-thread
      publishes.
- [ ] Same-pump deferral: publishes made by listeners during `Pump()` land in
      the next pump's batch.
- [ ] Wire pump A (post-drain, pre-sim) and pump B (post-sim, pre-UI/extraction)
      into `Engine::RunFrame()` with phase comments.
- [ ] Diagnostics: per-pump delivered-event counters exposed for tests.

## Tests
- [ ] Unit/contract tests (headless, `unit;runtime` labels): subscribe →
      publish → delivered at next pump, not at publish time.
- [ ] Cascade-bounding test: listener publishes during pump; delivery happens
      at the following pump only.
- [ ] Cross-thread test: publishes from a worker thread are delivered at the
      next main-thread pump without data races (exercised under the
      sanitizer-enabled preset).
- [ ] Unsubscribe test: handle removal stops delivery; unsubscribing during a
      pump is safe.

## Docs
- [ ] Regenerate `docs/api/generated/module_inventory.md` (new module).
- [ ] Record the two pump points in the frame-phase description doc, citing
      ADR-0024 D7.

## Acceptance criteria
- [ ] The kernel event API exposes no synchronous dispatch path.
- [ ] Both pumps run in `Engine::RunFrame()` at the ADR-0024 positions.
- [ ] All listed tests pass under the default CPU gate; the cross-thread test
      passes under sanitizers.
- [ ] `Operational` follow-up is owned by `ARCH-012`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Exposing `entt::dispatcher::trigger` (or any immediate dispatch) through
  the kernel API.
- Letting worker threads reach the dispatcher directly.
- Migrating existing dispatcher call sites in this task.

## Maturity
- Target: `CPUContracted` (headless contract tests under the default CPU
  gate). `Operational` owned by `ARCH-012`.
