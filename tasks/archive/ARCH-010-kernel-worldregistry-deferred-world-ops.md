---
id: ARCH-010
theme: F
depends_on:
  - ARCH-008
  - ARCH-009
maturity_target: CPUContracted
completed: 2026-07-08
---
# ARCH-010 — Kernel WorldRegistry with deferred, two-phase world operations

## Status

- Retired on 2026-07-08 at `CPUContracted`.
- PR: pending. Commit: pending local change.
- The code landed as `Extrinsic.Runtime.WorldRegistry`, with `Engine` booting
  `DefaultWorldHandle` world #0 through the registry and retaining `GetScene()`
  as an active-world compatibility accessor. `Engine::Worlds()` and
  `Engine::ActiveWorld()` expose the kernel mechanism without introducing a
  global world singleton.
- Active-world swaps and world destroys are request-only and apply only at the
  end-of-frame Maintenance boundary. Destroy is two-phase:
  `WorldWillBeDestroyed` is published, jobs scoped to that world are cancelled,
  and the registry tears the inactive world down on a later Maintenance pass.
- Render extraction now carries an explicit `WorldHandle` parameter and mirrors
  it in `RuntimeRenderExtractionStats`; `Engine` passes the active world while
  preserving the existing single-active-world rendering behavior.
- Verified locally with the `ci` preset, which enables
  `INTRINSIC_ENABLE_SANITIZERS=ON`: configure passed, the focused
  `IntrinsicRuntimeContractTests` target built, all five
  `RuntimeWorldRegistry.*` tests passed directly and through CTest, the
  combined ARCH-008/009/010 seam CTest subset passed 18/18, `IntrinsicTests`
  built, and the full default CPU-supported CTest gate passed 3628/3628.
- `Operational` use of the world registry remains owned by `ARCH-012`, which
  composes command -> job -> event -> commit through a real
  `ClusteringModule` flow. Preview/readiness/switch UX policy remains out of
  this mechanism slice.

## Goal
- Give the runtime kernel a `WorldRegistry` owning N `ECS::SceneRegistry`
  instances behind `WorldHandle`s: world lifetimes, scalar active-world
  state, `RequestSetActiveWorld`/`RequestDestroyWorld` as deferred requests
  applied only in the Maintenance phase, and two-phase destruction
  (`WorldWillBeDestroyed` announce pump → job cancellation → teardown at the
  boundary), per
  [ADR-0024](../../docs/adr/0024-kernel-module-architecture.md) D2/D4/D7.

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
- Two-phase destroy consumes `ARCH-008` (announce event pumps before teardown)
  and `ARCH-009` (`CancelAllForWorld` between announce and teardown).

## Required changes
- [x] New `Extrinsic.Runtime.WorldRegistry` module (interface `.cppm` +
      implementation `.cpp`): `CreateWorld(debugName)`,
      `RequestDestroyWorld`, `RequestSetActiveWorld`, `ActiveWorld()`,
      `Get(WorldHandle)`.
- [x] Deferred-op application in the Maintenance phase of
      `Engine::RunFrame()`: active-world swaps and destroys whose announce
      occurred on a prior Maintenance pass.
- [x] `WorldWillBeDestroyed` / `ActiveWorldChanged` kernel events.
- [x] `CancelAllForWorld` invoked between announce and teardown.
- [x] Engine boot creates world #0; existing engine-internal registry
      accesses route through the registry (mechanical redirection only).
- [x] Extraction entry point takes an explicit `WorldHandle` parameter.

## Tests
- [x] Contract tests (headless, `contract;runtime` labels): create/get;
      active-world swap requested mid-frame applies at Maintenance, not
      immediately; `ActiveWorldChanged` delivered at the following pump.
- [x] Two-phase destroy test: announce event observed before the registry is
      torn down; world-scoped in-flight job is cancelled in between; handles
      to the destroyed world report invalid afterwards.
- [x] Frame-0 test: engine boots with a valid active world before any module
      or app content runs.

## Docs
- [x] Regenerate `docs/api/generated/module_inventory.md` (new module).
- [x] Record the mechanism/policy world split in the runtime architecture
      doc, citing ADR-0024 D2.

## Acceptance criteria
- [x] No world mutation (swap/destroy) can take effect mid-frame.
- [x] All listed tests pass under the default CPU gate.
- [x] Single-world behavior of the existing engine is unchanged (existing
      CPU gate stays green).
- [x] `Operational` follow-up is owned by `ARCH-012`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeWorldRegistry.*'
ctest --test-dir build/ci --output-on-failure -R 'RuntimeWorldRegistry' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'Runtime(WorldRegistry|KernelEvents|JobService)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
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
- Target achieved: `CPUContracted`. `Operational` remains owned by
  `ARCH-012`.
