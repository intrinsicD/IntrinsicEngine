---
id: ARCH-008
theme: F
depends_on: []
maturity_target: CPUContracted
completed: 2026-07-08
---
# ARCH-008 — Queued-only kernel event bus with two pump points

## Status

- Retired on 2026-07-08 at `CPUContracted`.
- PR: pending. Commit: pending local change.
- The code landed as the domain-free `Extrinsic.Runtime.KernelEvents` module
  plus `Engine::Events()` access, two `Engine::RunFrame()` pump points,
  headless runtime contract tests, runtime frame-phase docs, and regenerated
  module inventory.
- The initial sketch named `entt::dispatcher`; the landed implementation uses
  a direct queued dispatcher/inbox so the runtime kernel never exposes or wraps
  an immediate-dispatch `trigger` path. The retained ADR-0024 D7 contract is
  queued-only publish, main-thread pump delivery, worker-safe publish, and
  next-pump cascade deferral.
- Verified locally with the `ci` preset, which enables
  `INTRINSIC_ENABLE_SANITIZERS=ON`: configure passed, the focused
  `IntrinsicRuntimeContractTests` target built, all six
  `RuntimeKernelEvents.*` contract tests passed directly and through CTest,
  `IntrinsicTests` built, and the full default CPU-supported CTest gate passed
  3616/3616.
- `Operational` use of the event bus remains owned by `ARCH-012`, which
  composes command -> job -> event -> commit through a real
  `ClusteringModule` flow.

## Goal
- Give the runtime kernel a `KernelEventBus` that is queued-only (no
  synchronous dispatch API), pumps at exactly two main-thread points per frame
  (post-command-drain and post-simulation), and accepts publishes from worker
  threads via an internal thread-safe inbox, per
  [ADR-0024](../../docs/adr/0024-kernel-module-architecture.md) D7.

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
- Immediate dispatcher implementations are not worker-safe; workers must only
  publish into the event bus inbox. The inbox is swapped into a bounded pump
  batch at the next main-thread pump.
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
- [x] New kernel event-bus module (interface `.cppm` + implementation
      `.cpp`): typed `Subscribe`/`Unsubscribe` with handles, thread-safe
      `Publish` (deferred always), internal MPSC inbox for cross-thread
      publishes.
- [x] Same-pump deferral: publishes made by listeners during `Pump()` land in
      the next pump's batch.
- [x] Wire pump A (post-drain, pre-sim) and pump B (post-sim, pre-UI/extraction)
      into `Engine::RunFrame()` with phase comments.
- [x] Diagnostics: per-pump delivered-event counters exposed for tests.

## Tests
- [x] Unit/contract tests (headless, `unit;runtime` labels): subscribe →
      publish → delivered at next pump, not at publish time.
- [x] Cascade-bounding test: listener publishes during pump; delivery happens
      at the following pump only.
- [x] Cross-thread test: publishes from a worker thread are delivered at the
      next main-thread pump without data races (exercised under the
      sanitizer-enabled preset).
- [x] Unsubscribe test: handle removal stops delivery; unsubscribing during a
      pump is safe.

## Docs
- [x] Regenerate `docs/api/generated/module_inventory.md` (new module).
- [x] Record the two pump points in the frame-phase description doc, citing
      ADR-0024 D7.

## Acceptance criteria
- [x] The kernel event API exposes no synchronous dispatch path.
- [x] Both pumps run in `Engine::RunFrame()` at the ADR-0024 positions.
- [x] All listed tests pass under the default CPU gate; the cross-thread test
      passes under sanitizers.
- [x] `Operational` follow-up is owned by `ARCH-012`.

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
- Target achieved: `CPUContracted` (headless contract tests under the default
  CPU gate). `Operational` remains owned by `ARCH-012`.
