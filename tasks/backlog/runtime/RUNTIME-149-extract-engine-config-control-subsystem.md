---
id: RUNTIME-149
theme: F
depends_on: []
maturity_target: Operational
---
# RUNTIME-149 — Extract render-recipe and hot-config control out of Engine

## Goal
- Move the runtime config-control facade — render-recipe preview/activate/
  apply/clear plus the engine-config hot-subset preview/apply — from
  `Extrinsic.Runtime.Engine` into a new engine-owned subsystem
  `Extrinsic.Runtime.EngineConfigControl`, exposed via
  `Engine::GetConfigControl()`.

## Non-goals
- No change to which config fields are hot-applicable versus boot-only,
  and no change to rejection diagnostics
  (`RejectedBootOnlyFields` semantics stay identical).
- No change to recipe validation or
  `Graphics.RenderRecipeConfig` loading.
- No boot-time resolution moves (owned by `RUNTIME-146`).

## Context
- Owning subsystem/layer: `runtime`. The subsystem needs (1) the renderer
  to install/clear `FrameRecipeOverride`, (2) read/write access to the
  engine-owned active `EngineConfig`, and (3) the sandbox
  progressive-Poisson change notification. `Engine` constructs it in
  `Initialize()` with those references; applying a hot subset mutates the
  `Engine`-owned config through the subsystem, keeping one config
  authority.
- Current locations, all in `src/runtime/Runtime.Engine.{cppm,cpp}`:
  - Public methods: `CreateRenderRecipeConfigContext`,
    `PreviewRenderRecipeConfigDocument`, `LoadRenderRecipeConfigPreviewFile`,
    `ActivateRenderRecipeConfigDocument`, `ApplyRenderRecipeConfigPreview`,
    `LoadAndApplyRenderRecipeConfigFile`, `ClearActiveRenderRecipeOverride`,
    `GetRenderRecipeState`, `PreviewEngineConfigControlDocument`,
    `LoadEngineConfigControlFile`, `ApplyEngineConfigHotSubset`,
    `LoadAndApplyEngineConfigHotSubsetFile`, `GetEngineConfigControlState`.
  - State: `m_RenderRecipeState`, `m_ConfigControlState`.
  - Exported types: `RuntimeRenderRecipeActivationSource`,
    `RuntimeConfigControlSource`, `RuntimeRenderRecipeApplyStatus`,
    `RuntimeRenderRecipeApplyResult`, `RuntimeRenderRecipeState`,
    `RuntimeEngineConfigApplyStatus`, `RuntimeEngineConfigApplyResult`,
    `RuntimeEngineConfigControlState`.
  - Anonymous-namespace helpers: `ToRecipeActivationSource`,
    `RecordBootOnlyDifference`, `FindBootOnlyEngineConfigDifferences`,
    `ProgressivePoissonPlaygroundConfigEquals`.
- Known consumers to update: `Runtime.SandboxEditorUi` (recipe/config
  panels), agent/CLI control paths in `src/app/Sandbox/main.cpp`,
  `Test.RuntimeConfigControlFacade.cpp`,
  `Test.RuntimeRenderRecipeActivation.cpp`.
- Startup recipe activation from the boot config
  (`RuntimeRenderRecipeActivationSource::StartupConfigFile`) is triggered
  from `Engine::Initialize()`; it delegates to the subsystem after
  construction.
- Part of the `Runtime.Engine` decomposition series (`RUNTIME-146..151`).

- Transitional facade (ADR-0024): the `Engine::Get{X}()` accessor this task adds is a **transitional landing**, not the end state. Per ADR-0024 D9/D12 and the [kernel target-state](../../../docs/architecture/kernel-target-state.md) scorecard (`Engine::GetX()` domain-facade accessors = 0), its conversion to a RuntimeModule / Resolve-phase service is owned by `ARCH-013` (per-subsystem decision) and tracked by `ARCH-014`. This task extracts the subsystem; it does not make the accessor permanent.
## Control surfaces
- Config: unchanged — same hot-subset and recipe documents.
- UI: `SandboxEditorUi` calls `engine.GetConfigControl().*` instead of
  `engine.*`.
- Agent/CLI: unchanged flags; call sites re-pointed at the subsystem.

## Required changes
- [ ] Add `Extrinsic.Runtime.EngineConfigControl` interface +
      implementation units under `src/runtime/`, registered in
      `src/runtime/CMakeLists.txt`.
- [ ] Move the methods, state, exported types, and helpers listed in
      Context verbatim; keep `Engine` as the owner of the active
      `EngineConfig` value.
- [ ] Add `Engine::GetConfigControl()` (and `const` overload); migrate
      call sites; do not keep delegating methods on `Engine`.
- [ ] Keep the startup recipe activation path
      (`StartupConfigFile` source) working from `Initialize()`.

## Tests
- [ ] `Test.RuntimeConfigControlFacade.cpp` and
      `Test.RuntimeRenderRecipeActivation.cpp` pass with only import/name
      updates (behavior-preservation evidence, including boot-only field
      rejection and recipe-override install/clear).
- [ ] Default CPU gate stays green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.

## Docs
- [ ] Update `src/runtime/README.md` module list for the new module.
- [ ] Regenerate module inventories per `intrinsicengine-docs-sync`.
- [ ] Update `tasks/backlog/runtime/README.md` status line on retirement.

## Acceptance criteria
- [ ] `Runtime.Engine.cppm` no longer declares any recipe/config-control
      method, type, or member listed in Context.
- [ ] Exactly one config authority remains (the `Engine`-owned active
      config, mutated only through the subsystem's apply path).
- [ ] CPU gate and layering check pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Mixing this mechanical move with changes to hot/boot-only field
  classification or recipe validation.
- Touching boot resolution, asset import, scene document, or frame-loop
  code owned by `RUNTIME-146..148`/`RUNTIME-150`.

## Maturity
- Target: `Operational` — the control surfaces are already exercised by
  the sandbox editor and contract tests; the move must preserve that. No
  new capability follow-up is owed.
