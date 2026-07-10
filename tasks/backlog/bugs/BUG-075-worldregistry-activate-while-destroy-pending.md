---
id: BUG-075
theme: G
depends_on: []
---
# BUG-075 — A world can be made active while its destroy is pending

## Goal
- Reject (or coherently resolve) a request to activate a world that already has a
  pending or announced destroy, so the registry cannot end in an
  active-and-being-destroyed state.

## Non-goals
- No change to the deferred create/destroy model (ARCH-010) beyond the
  activation guard.

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.WorldRegistry.cpp`.
- `RequestSetActiveWorld` checks only `Contains(world)`
  (`Runtime.WorldRegistry.cpp:55-68`), and a `DestroyPending` world still
  satisfies `Contains` (`Find` treats only `Empty` as absent, `:207-220`).
- Suspected sequence: in one `ApplyMaintenance`, the pending active-switch is
  applied first, then the destroy sweep announces the now-active world
  (`:138-173`), publishing `WorldWillBeDestroyed` for it. The active-world guard
  (`:182-183`) then prevents the free forever, leaving the world stuck
  `active + DestroyAnnounced` after listeners were already told it is dying. No
  crash/UAF, but an inconsistent lifecycle state.

## Required changes
- [ ] Make `RequestSetActiveWorld` reject a world with a pending/announced
      destroy (or clear the pending destroy as part of activation, whichever is
      the intended semantics), with a clear `Core::Result` error.
- [ ] Confirm the maintenance sweep cannot both announce-destroy and keep-active
      the same world in one pass.

## Tests
- [ ] Activating a destroy-pending world is rejected (or coherently resolved) and
      the registry never reports a world as both active and destroy-announced.
- [ ] `ctest --test-dir build/ci --output-on-failure -R RuntimeWorldRegistry --timeout 60` and default CPU gate.

## Docs
- [ ] Document the activation-vs-destroy precedence in the world lifecycle notes.

## Acceptance criteria
- [ ] No world can be simultaneously active and destroy-announced.
- [ ] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R RuntimeWorldRegistry --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not resolve this by silently ignoring destroy requests.
- Mixing mechanical file moves with semantic refactors.
