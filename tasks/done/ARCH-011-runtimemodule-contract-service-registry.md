---
id: ARCH-011
theme: F
depends_on:
  - ARCH-007
  - ARCH-008
  - ARCH-009
  - ARCH-010
maturity_target: CPUContracted
completed: 2026-07-08
---
# ARCH-011 — RuntimeModule contract, EngineSetup, and two-phase ServiceRegistry

## Status

- Retired on 2026-07-08 at `CPUContracted`.
- PR: pending. Commit: pending local change.
- The code landed `Extrinsic.Runtime.Module` and
  `Extrinsic.Runtime.ServiceRegistry`, plus `Engine::AddModule(...)`,
  `Engine::EmplaceModule<T>(...)`, and `Engine::Services()`.
- Runtime boot now sorts modules by stable name, invokes all
  `IRuntimeModule::OnRegister(EngineSetup&)`, then invokes all
  `IRuntimeModule::OnResolve(EngineSetup&)` after the service registry has
  every provided service. Built-in kernel services (`CommandBus`,
  `KernelEventBus`, `JobService`, `WorldRegistry`) are provided by the engine.
- Module-facing setup/context surfaces expose command, event, job, world,
  service, fixed-step system, and frame-hook seams without exposing `Engine&`.
  Module sim systems carry declared wait/signal labels so fixed-step pass
  ordering is data-driven rather than `AddModule`-order driven.
- Verified locally with the `ci` preset, which enables
  `INTRINSIC_ENABLE_SANITIZERS=ON`: the focused
  `RuntimeServiceRegistry.*:RuntimeModule.*` test set passed 5/5 and the
  combined ARCH-007/008/009/010/011 runtime seam CTest subset passed 30/30;
  `IntrinsicTests` built and the full default CPU-supported CTest gate passed
  3633/3633. Structural checks are recorded below.
- `Operational` use of the module contract remains owned by `ARCH-012`, which
  composes command -> job -> event -> commit through a real
  `ClusteringModule` flow.

## Goal
- Give the runtime kernel the composition seam everything else registers
  through: `IRuntimeModule` (`Name`/`OnRegister`/`OnResolve`/`OnShutdown`),
  an `EngineSetup` registration surface (command handlers, event
  subscriptions, sim systems, frame-phase hooks), a two-phase
  `ServiceRegistry` (`Provide` → `Resolve`, fail-closed at boot), and
  `Engine::AddModule` + the RegisterAll → ResolveAll boot sequence, per
  [ADR-0024](../../docs/adr/0024-kernel-module-architecture.md)
  D1/D3/D12/D13.

## Non-goals
- No module extraction in this task (`ARCH-012` proves the seam).
- No extension-pass registration on `EngineSetup` yet — the D10 slot
  contract is its own follow-up task and must first check the two ADR-0024
  D10 validation items (splatting lighting participation; order-dependent
  transparency).
- No input capture filter chain yet (ADR-0024 D11) — seeded with the
  EditorUiModule extraction line (`ARCH-006`/`UI-034`).
- No `InlineModule` builder and no `OnSimTick`/`OnVariableTick` removal —
  final migration step per ADR-0024 D12; existing `IApplication` behavior is
  unchanged.
- No dynamic loading ("plugin" rejected in ADR-0024 D1).

## Context
- Owner/layer: `runtime` (kernel spine per ADR-0024 D9).
- `Runtime.Engine.cppm` already carries a Register-before-Initialize /
  Resolve-after pattern in embryo (reference-provider comment around line
  808) and ad-hoc hook registries (`RegisterPostImportProcessor`,
  `RegisterImportCompletedHandler`, `RegisterInputAction`,
  `RegisterRuntimeGpuJobParticipant`). This task introduces the general
  mechanism; existing ad-hoc hooks keep working and are rerouted internally
  where mechanical, with full migration owned by later extraction tasks.
- Ordering rule (ADR-0024 D3): module registration order must not affect
  behavior — inter-module ordering comes from declared data dependencies and
  two-phase startup.
- D13: registration surface is `EngineSetup` (narrow capabilities), not
  `Engine&`. "Kernel" remains a documentation word; the class stays `Engine`.
- Frame-phase hooks: `AfterCommandDrain`, `UiBuild`, `BeforeExtraction`,
  `Maintenance` — neutral slots; nothing UI-specific in the kernel.

## Required changes
- [x] New `Extrinsic.Runtime.Module` module: `IRuntimeModule`, `EngineSetup`
      (references to Commands/Events/Jobs/Worlds/Services +
      `RegisterSimSystem(SimSystemDesc)` + `RegisterFrameHook(FramePhase, fn)`).
- [x] New `Extrinsic.Runtime.ServiceRegistry` module: `Provide<T>`,
      `Require<T>` (fail-closed boot error naming requester and missing
      service), `Find<T>` (optional dependency).
- [x] `Engine::AddModule(std::unique_ptr<IRuntimeModule>)` +
      `EmplaceModule<T>`; boot sequence: all `OnRegister`, then all
      `OnResolve`, deterministic error on failure; `OnShutdown` runs after
      the teardown announce pump (two-phase, ADR-0024 D7).
- [x] `SimSystemDesc` registration path appends module systems into the
      fixed-step FrameGraph alongside `Runtime.EcsSystemBundle` passes, with
      declared Read/Write tokens and named signals resolving order.
- [x] Frame hooks invoked at their phases in `Engine::RunFrame()`.

## Tests
- [x] Unit/contract tests (headless, `contract;runtime` labels): two-phase boot —
      `Provide` in register phase is visible to `Require` in resolve phase
      regardless of `AddModule` order.
- [x] Fail-closed test: `Require` of an unprovided service aborts boot with
      an error naming the requesting module and the missing service type.
- [x] Registration-order-independence test: two permutations of the same
      module set produce identical system schedules and hook sequences.
- [x] Module sim-system test: a module-registered system executes in the
      fixed-step graph with its declared dependencies honored.
- [x] Shutdown-order test: `OnShutdown` observes the announce event before
      teardown.

## Docs
- [x] Regenerate `docs/api/generated/module_inventory.md` (new modules).
- [x] Update `docs/architecture/feature-module-playbook.md` to name
      `IRuntimeModule`/`EngineSetup` as the registration seam for grown
      features, citing ADR-0024.

## Acceptance criteria
- [x] A test-only module can be composed against the kernel headlessly and
      exercise commands, events, jobs, worlds, systems, and hooks without
      touching `Engine` internals.
- [x] `EngineSetup`/`CommandContext` surfaces expose no `Engine&`.
- [x] All listed tests pass under the default CPU gate.
- [x] `Operational` follow-up is owned by `ARCH-012`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeServiceRegistry.*:RuntimeModule.*'
ctest --test-dir build/ci --output-on-failure -R 'Runtime(CommandBus|KernelEvents|JobService|WorldRegistry|Module)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Behavior depending on module registration order.
- `Engine&` in any module-facing context or setup surface.
- Extracting existing features into modules in this task.
- Domain nouns in kernel module names or APIs.

## Maturity
- Target achieved: `CPUContracted`. `Operational` remains owned by
  `ARCH-012`.
