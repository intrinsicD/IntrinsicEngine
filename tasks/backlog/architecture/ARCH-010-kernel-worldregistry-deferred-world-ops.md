---
id: ARCH-010
theme: F
depends_on:
  - ARCH-008
  - ARCH-009
---
# ARCH-010 — Kernel WorldRegistry with deferred, two-phase world operations

## Goal
- Give the runtime kernel a `WorldRegistry` owning N `ECS::SceneRegistry`
  instances behind `WorldHandle`s: world lifetimes, scalar active-world
  state, `RequestSetActiveWorld`/`RequestDestroyWorld` as deferred requests
  applied only in the Maintenance phase, and two-phase destruction
  (`WorldWillBeDestroyed` announce pump → job cancellation → teardown at the
  boundary), per
  [ADR-0024](../../../docs/adr/0024-kernel-module-architecture.md) D2/D4/D7.

## Non-goals
- No `WorldSwitchModule` (preview orchestration, readiness tracking, switch
  UX) — that is module policy seeded after `ARCH-011`/`ARCH-012`.
- No simultaneous multi-world rendering (hard switch per ADR-0024 D4);
  extraction takes a world handle parameter but only the active world is
  extracted.
- No `ISceneModule` interface (rejected alternative in ADR-0024).
- No scene-document ownership (loading/saving/authoring policy) — owned by
  `RUNTIME-148`.
- No non-active-world ticking (frozen worlds per ADR-0024 D4).

## Context
- Owner/layer: `runtime` (kernel substrate per ADR-0024 D9).
- Today the Engine owns a single scene registry directly; this task wraps it:
  world #0 is created at boot so frame 0 is never ambiguous, and existing
  single-world call sites resolve through `Get(ActiveWorld())`.
- `Registry&`/`WorldHandle` is always an explicit parameter — never a global
  (ADR-0024 D2). Render-world extraction gains the world-handle parameter
  here (cheap future-proofing pinned in D4).
- Two-phase destroy consumes `ARCH-008` (announce event pumps a full frame
  before teardown) and `ARCH-009` (`CancelAllForWorld` between announce and
  teardown).

## Required changes
- [ ] New `Extrinsic.Runtime.WorldRegistry` module (interface `.cppm` +
      implementation `.cpp`): `CreateWorld(debugName)`,
      `RequestDestroyWorld`, `RequestSetActiveWorld`, `ActiveWorld()`,
      `Get(WorldHandle)`.
- [ ] Deferred-op application in the Maintenance phase of
      `Engine::RunFrame()`: active-world swaps and destroys whose announce
      pumped a full frame ago.
- [ ] `WorldWillBeDestroyed` / `ActiveWorldChanged` kernel events.
- [ ] `CancelAllForWorld` invoked between announce and teardown.
- [ ] Engine boot creates world #0; existing engine-internal registry
      accesses route through the registry (mechanical redirection only).
- [ ] Extraction entry point takes an explicit `WorldHandle` parameter.

## Tests
- [ ] Unit/contract tests (headless, `unit;runtime` labels): create/get;
      active-world swap requested mid-frame applies at Maintenance, not
      immediately; `ActiveWorldChanged` delivered at the following pump.
- [ ] Two-phase destroy test: announce event observed one full frame before
      the registry is torn down; world-scoped in-flight job is cancelled in
      between; handles to the destroyed world report invalid afterwards.
- [ ] Frame-0 test: engine boots with a valid active world before any module
      or app content runs.

## Docs
- [ ] Regenerate `docs/api/generated/module_inventory.md` (new module).
- [ ] Record the mechanism/policy world split in the runtime architecture
      doc, citing ADR-0024 D2.

## Acceptance criteria
- [ ] No world mutation (swap/destroy) can take effect mid-frame.
- [ ] All listed tests pass under the default CPU gate.
- [ ] Single-world behavior of the existing engine is unchanged (existing
      CPU gate stays green).
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
- A synchronous destroy/swap path (all world mutations defer to Maintenance).
- Global/singleton access to "the" registry from new code.
- Preview/multi-world rendering work.

## Maturity
- Target: `CPUContracted`. `Operational` owned by `ARCH-012`.
