---
id: PHYSICS-004
theme: C
depends_on: [PHYSICS-003]
maturity_target: Operational
---
# PHYSICS-004 — Operational runtime physics module and bridge privatization

## Goal

- Compose the existing physics world/synchronization contract as an optional
  app-owned `PhysicsModule` in the real engine lifecycle, prove fixed-step
  authoring/simulation/writeback through `Engine::Run()`, and make the
  test-only public `PhysicsBridge` an implementation detail.

## Non-goals

- No new solver, collision model, GPU backend, renderer visualization, or
  physics handles in ECS.
- No `PhysicsService`, universal simulation service, or backend registry; this
  task's real consumers use module config and copied diagnostics.
- No runtime ownership of physics solver internals; `physics` remains the
  world/solver owner and runtime only composes ECS synchronization.

## Context

- `PHYSICS-001..003` delivered the CPU physics world, collision/solver
  contracts, and `Runtime.PhysicsBridge`, but production never composes the
  bridge; only integration tests instantiate it directly.
- Runtime is the correct owner for ECS authoring synchronization, fixed-step
  scheduling, dynamic transform writeback, world replacement, and shutdown.
- A real module boundary is justified by lifecycle and per-world sidecars.
  Config control and copied diagnostics are sufficient for the current
  consumers; any future command surface requires a separate task with a real
  caller.

## Slice plan

- **Slice A — module composition.** Add the optional module, per-world state,
  fixed-step config validation, diagnostics snapshots, and deterministic
  startup/shutdown.
- **Slice B — real workflow.** Compose it in Sandbox/default policy behind a
  validated config flag and prove ECS authoring → step → writeback during the
  real engine loop.
- **Slice C — cleanup.** Move bridge implementation/state behind the module,
  migrate direct bridge tests to module behavior, and delete the public
  `Runtime.PhysicsBridge` surface.

## Control surfaces

- Config: serializable enable/fixed-step/step-budget settings through the
  generic validated config lane.
- UI/Agent: optional diagnostics/control consumers use the same config apply
  and copied module snapshot; no direct world/bridge access.

## Required changes

- [ ] Add `PhysicsModule` as an `IRuntimeModule` with explicit dependencies,
      per-world physics state/sidecars, fixed-step accumulation, and teardown.
- [ ] Synchronize CPU descriptors from ECS before steps and write dynamic
      transforms back after steps with the existing dirty/generation contract.
- [ ] Handle world create/replace/destroy, entity removal, invalid authoring,
      step budget, and shutdown without leaking handles into ECS.
- [ ] Register validated config and copied diagnostics through existing
      config/module seams; disabled configuration owns no active physics world.
- [ ] Migrate direct `PhysicsBridge` tests to the composed module and a real
      Null-window `Engine::Run()` integration.
- [ ] Privatize/delete the public `Runtime.PhysicsBridge` module after the
      operational path and exact lifecycle parity pass.

## Tests

- [ ] Module contracts cover authoring creation/update/removal, stable handle
      reuse, fixed-step accumulation, collision/solver stepping, dynamic-only
      writeback, invalid descriptors, and clear/shutdown.
- [ ] Real Engine integration proves an authored dynamic body advances and
      writes back during `Engine::Run()` while static/kinematic bodies do not.
- [ ] Config file, programmatic/agent apply, and UI control (if exposed) share
      one validation/apply path.
- [ ] Structural tests prove production composes `PhysicsModule` and no public
      `PhysicsBridge` import remains.

## Docs

- [ ] Update physics/runtime architecture, config, and Sandbox documentation
      with module lifecycle and disabled/default behavior.
- [ ] Regenerate module inventory and refresh physics/task indexes.

## Acceptance criteria

- [ ] Physics authoring/simulation/writeback runs through a composed optional
      runtime module in the real engine loop.
- [ ] Lower physics remains ECS/runtime/graphics-free and ECS stores no live
      physics handles.
- [ ] The direct public bridge surface is deleted after module integration
      tests preserve its behavior.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PhysicsModule|RuntimePhysics|PhysicsBridge' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Physics solver/world state in ECS or runtime, graphics/RHI dependencies in
  physics, or a second public bridge/service with identical ownership.
- Claiming `Operational` from direct bridge tests without real engine
  composition.
- Deleting the bridge before module behavior and lifecycle parity pass.

## Maturity

- Target: `Operational` through the real CPU/Null engine loop; closure also
  requires retirement of the public test-only bridge surface.
