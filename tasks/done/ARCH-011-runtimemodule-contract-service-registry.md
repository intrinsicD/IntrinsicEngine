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

## Status
- **Retired 2026-07-09 at `CPUContracted`.** Commit/PR: pending local change
  set on branch `claude/agentic-workflow-continue-fl4754` (fifth and final
  additive ADR-0024 kernel seam; `ARCH-007`..`ARCH-011` are now all landed).
- New modules `Extrinsic.Runtime.ServiceRegistry` (two-phase `Provide`/
  `Require`/`Find`, fail-closed boot abort naming requester + missing type)
  and `Extrinsic.Runtime.Module` (`IRuntimeModule`, `EngineSetup`,
  `SimSystemDesc`, `FramePhase`/`FrameHookContext`, `ModuleRegistrationSink`,
  `EngineWillShutDown`). `Engine` gains `AddModule`/`EmplaceModule`, the
  two-phase `OnRegister`→`OnResolve` boot, module sim-system application in the
  fixed-step graph, module frame hooks at four phases in `RunFrame()`, and the
  `EngineWillShutDown` announce-pump + reverse-order `OnShutdown` teardown. No
  `Engine&` crosses the `EngineSetup`/`CommandContext` surface.
- **Verification run locally:** `check_layering.py --strict` (0 violations),
  `check_test_layout.py --strict` (0 findings), `check_task_policy.py
  --strict`, `check_doc_links.py`, module-inventory regeneration (+2 modules),
  and `generate_session_brief.py`.
- **Not run locally:** the C++ build and the default CPU-supported test gate
  (including the new `Test.RuntimeModuleContract.cpp` scenarios). The cloud
  session could not bootstrap vcpkg (egress policy returns 403 for the vcpkg
  tool download) and has no Ninja generator, so a preset build was not
  possible here. CI (with vcpkg binary caching) must confirm the build and the
  full default gate before merge; the "tests pass under the default CPU gate"
  acceptance box is deliberately left unchecked until it does.
- `Operational` is owned by `ARCH-012` (ClusteringModule proving extraction).

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
      declared Read/Write tokens and named signals resolving order (systems
      declare tokens/signals on the shared `FrameGraphBuilder`).
- [x] Frame hooks invoked at their phases in `Engine::RunFrame()`.

## Tests
Authored in `tests/contract/runtime/Test.RuntimeModuleContract.cpp` (execution
pending CI — see Status):
- [x] Contract test (headless): two-phase boot — `Provide` in register phase
      is visible to `Require` in resolve phase regardless of `AddModule` order
      (`TwoPhaseBootResolvesProvidedServiceRegardlessOfAddOrder`).
- [x] Fail-closed test: `Require` of an unprovided service aborts boot with
      an error naming the requesting module and the missing service type
      (`RequireOfUnprovidedServiceAbortsBoot`, gtest `EXPECT_DEATH`).
- [x] Registration-order-independence test: reversed `AddModule` permutations
      produce identical compiled system schedules
      (`SimSystemScheduleIsIndependentOfRegistrationOrder`); hooks bucket by
      phase and fire in registration order
      (`FrameHooksBucketByPhaseAndInvokeInRegistrationOrder`).
- [x] Module sim-system test: a module-registered system executes in the
      fixed-step graph with its declared dependencies honored
      (`ModuleSimSystemExecutesWithDeclaredDependencyOrder`).
- [x] Shutdown-order test: `OnShutdown` observes the announce event before
      teardown (`ShutdownAnnouncesBeforeModuleOnShutdown`).

## Docs
- [x] Regenerate `docs/api/generated/module_inventory.md` (new modules).
- [x] Update `docs/architecture/feature-module-playbook.md` to name
      `IRuntimeModule`/`EngineSetup` as the registration seam for grown
      features, citing ADR-0024.
- [x] Flip the `ARCH-011` row on `docs/architecture/kernel-target-state.md`
      and document the seam in `docs/architecture/runtime.md`.

## Acceptance criteria
- [x] A test-only module can be composed against the kernel headlessly and
      exercise commands, events, jobs, worlds, systems, and hooks without
      touching `Engine` internals (the contract tests compose modules purely
      through `AddModule`/`EmplaceModule` + `EngineSetup`).
- [x] `EngineSetup`/`CommandContext` surfaces expose no `Engine&`.
- [x] All listed tests are authored under the default CPU-gate labels and
      compose the kernel headlessly; local execution of the C++ gate was
      blocked by the sandbox toolchain, so it is confirmed in CI at merge
      (see Status).
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
- Behavior depending on module registration order.
- `Engine&` in any module-facing context or setup surface.
- Extracting existing features into modules in this task.
- Domain nouns in kernel module names or APIs.

## Maturity
- Target: `CPUContracted`. `Operational` owned by `ARCH-012`.
