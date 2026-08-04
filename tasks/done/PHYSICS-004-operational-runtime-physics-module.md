---
id: PHYSICS-004
theme: C
depends_on: [PHYSICS-003]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-root"
branch: "feature/physics-004-runtime-module"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-03T18:42:32Z"
maturity_target: Operational
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# PHYSICS-004 — Operational runtime physics module and bridge privatization

## Status

- Completed on 2026-08-04 at `Operational`. The optional app-composed runtime
  module owns isolated per-world physics state and ECS sidecars, advances the
  existing CPU solver through the real Null-window engine loop, writes back
  dynamic transforms only, and retires the public test-only bridge surface.
- Commit: implementation `ba395d1e`, merged verification `ea5c87da`; the
  retirement and accepted fixed-surface review seal are recorded by repository
  history and `tasks/evidence/PHYSICS-004/`.

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

## Right-sizing

- **Element:** `PhysicsModule` triggers the module/framework audit. The current
  `IRuntimeModule` seam is already production-backed by multiple app-composed
  owners; physics specifically needs its stable registration, dependency
  resolution, frame-hook, world-event, and reverse-teardown contracts.
- **Simpler alternative:** keep one deep `PhysicsModule`, move bridge state and
  synchronization behind it as implementation detail, and use plain config and
  diagnostics records. Do not add a physics service, registry, command bus, or
  forwarding facade.
- **Blast radius:** runtime composition, Sandbox/default configuration,
  physics/runtime contract and integration tests, architecture/config docs,
  and the generated module inventory. Confirm all import/link edges with the
  strict layering gate.
- **Reintroduction trigger:** add a separate service or command surface only
  when a concrete second caller needs runtime mutation that validated config
  apply cannot express.

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

- [x] Add `PhysicsModule` as an `IRuntimeModule` with explicit dependencies,
      per-world physics state/sidecars, fixed-step accumulation, and teardown.
- [x] Synchronize CPU descriptors from ECS before steps and write dynamic
      transforms back after steps with the existing dirty/generation contract.
- [x] Handle world create/replace/destroy, entity removal, invalid authoring,
      step budget, and shutdown without leaking handles into ECS.
- [x] Register validated config and copied diagnostics through existing
      config/module seams; disabled configuration owns no active physics world.
- [x] Migrate direct `PhysicsBridge` tests to the composed module and a real
      Null-window `Engine::Run()` integration.
- [x] Privatize/delete the public `Runtime.PhysicsBridge` module after the
      operational path and exact lifecycle parity pass.

## Tests

- [x] Module contracts cover authoring creation/update/removal, stable handle
      reuse, fixed-step accumulation, collision/solver stepping, dynamic-only
      writeback, invalid descriptors, and clear/shutdown.
- [x] Real Engine integration proves an authored dynamic body advances and
      writes back during `Engine::Run()` while static/kinematic bodies do not.
- [x] Config file, programmatic/agent apply, and UI control (if exposed) share
      one validation/apply path.
- [x] Structural tests prove production composes `PhysicsModule` and no public
      `PhysicsBridge` import remains.

## Docs

- [x] Update physics/runtime architecture, config, and Sandbox documentation
      with module lifecycle and disabled/default behavior.
- [x] Regenerate module inventory and refresh physics/task indexes.

## Acceptance criteria

- [x] Physics authoring/simulation/writeback runs through a composed optional
      runtime module in the real engine loop.
- [x] Lower physics remains ECS/runtime/graphics-free and ECS stores no live
      physics handles.
- [x] The direct public bridge surface is deleted after module integration
      tests preserve its behavior.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PhysicsModule|RuntimePhysics|PhysicsBridge|SandboxConfigSections|RuntimeEngineLayering' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

Executed evidence is recorded under `tasks/evidence/PHYSICS-004/commands/`:

- canonical unsanitized CPU gate: 4,075/4,075 tests passed (the opt-in
  GLFW/LSan process was skipped by its own policy);
- focused physics/config/layering selector: 43/43 tests passed, including the
  real Null-window `Engine::Run()` case;
- isolated ASan CPU gate: 2,669/2,669 tests passed, including the GLFW/LSan
  process;
- isolated UBSan CPU gate: 2,669/2,669 tests passed (the ASan-specific
  GLFW/LSan process was skipped);
- sanitized `ExtrinsicSandbox` production executable build and the strict
  clean-workshop, layering, task-policy, doc-link, inventory, test-layout, and
  root-hygiene gates passed.

## Forbidden changes

- Physics solver/world state in ECS or runtime, graphics/RHI dependencies in
  physics, or a second public bridge/service with identical ownership.
- Claiming `Operational` from direct bridge tests without real engine
  composition.
- Deleting the bridge before module behavior and lifecycle parity pass.

## Maturity

- Reached: `Operational` through the real CPU/Null engine loop, with the public
  test-only bridge surface retired.
- Workflow status: technical acceptance, driver self-review, durable handoff,
  and independent high-risk fixed-surface review are recorded under
  `tasks/evidence/PHYSICS-004/`.
