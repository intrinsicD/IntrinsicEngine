---
id: ARCH-011
theme: F
depends_on:
  - ARCH-007
  - ARCH-008
  - ARCH-009
  - ARCH-010
---
# ARCH-011 — RuntimeModule contract, EngineSetup, and two-phase ServiceRegistry

## Goal
- Give the runtime kernel the composition seam everything else registers
  through: `IRuntimeModule` (`Name`/`OnRegister`/`OnResolve`/`OnShutdown`),
  an `EngineSetup` registration surface (command handlers, event
  subscriptions, sim systems, frame-phase hooks), a two-phase
  `ServiceRegistry` (`Provide` → `Resolve`, fail-closed at boot), and
  `Engine::AddModule` + the RegisterAll → ResolveAll boot sequence, per
  [ADR-0024](../../../docs/adr/0024-kernel-module-architecture.md)
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
- [ ] New `Extrinsic.Runtime.Module` module: `IRuntimeModule`, `EngineSetup`
      (references to Commands/Events/Jobs/Worlds/Services +
      `RegisterSimSystem(SimSystemDesc)` + `RegisterFrameHook(FramePhase, fn)`).
- [ ] New `Extrinsic.Runtime.ServiceRegistry` module: `Provide<T>`,
      `Require<T>` (fail-closed boot error naming requester and missing
      service), `Find<T>` (optional dependency).
- [ ] `Engine::AddModule(std::unique_ptr<IRuntimeModule>)` +
      `EmplaceModule<T>`; boot sequence: all `OnRegister`, then all
      `OnResolve`, deterministic error on failure; `OnShutdown` runs after
      the teardown announce pump (two-phase, ADR-0024 D7).
- [ ] `SimSystemDesc` registration path appends module systems into the
      fixed-step FrameGraph alongside `Runtime.EcsSystemBundle` passes, with
      declared Read/Write tokens and named signals resolving order.
- [ ] Frame hooks invoked at their phases in `Engine::RunFrame()`.

## Tests
- [ ] Unit/contract tests (headless, `unit;runtime` labels): two-phase boot —
      `Provide` in register phase is visible to `Require` in resolve phase
      regardless of `AddModule` order.
- [ ] Fail-closed test: `Require` of an unprovided service aborts boot with
      an error naming the requesting module and the missing service type.
- [ ] Registration-order-independence test: two permutations of the same
      module set produce identical system schedules and hook sequences.
- [ ] Module sim-system test: a module-registered system executes in the
      fixed-step graph with its declared dependencies honored.
- [ ] Shutdown-order test: `OnShutdown` observes the announce event before
      teardown.

## Docs
- [ ] Regenerate `docs/api/generated/module_inventory.md` (new modules).
- [ ] Update `docs/architecture/feature-module-playbook.md` to name
      `IRuntimeModule`/`EngineSetup` as the registration seam for grown
      features, citing ADR-0024.

## Acceptance criteria
- [ ] A test-only module can be composed against the kernel headlessly and
      exercise commands, events, jobs, worlds, systems, and hooks without
      touching `Engine` internals.
- [ ] `EngineSetup`/`CommandContext` surfaces expose no `Engine&`.
- [ ] All listed tests pass under the default CPU gate.
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
- Behavior depending on module registration order.
- `Engine&` in any module-facing context or setup surface.
- Extracting existing features into modules in this task.
- Domain nouns in kernel module names or APIs.

## Maturity
- Target: `CPUContracted`. `Operational` owned by `ARCH-012`.
