---
id: RUNTIME-181
theme: F
depends_on:
  - ARCH-016
  - RUNTIME-179
maturity_target: Operational
---
# RUNTIME-181 — Extract the config-control composition module

## Goal
- Move live config preview, validation, apply orchestration, recipe-activation
  requests, and app-section callback ownership out of `Runtime.Engine` into
  the existing `EngineConfigControl`, promoted directly to one app-composed
  runtime module and published service.

## Non-goals
- No config schema, precedence, serialization, or validation behavior change.
- No second config tree, draft store, apply path, or UI-only control.
- No movement of boot-critical `EngineConfig` values out of the kernel before
  window/device/renderer creation.
- No change from the existing synchronous startup or hot-apply semantics.
- No renderer API or render-frame-input redesign.
- No `ConfigControlModule` wrapper around `EngineConfigControl`, module frame
  hook, apply queue, or second activation record.
- No compatibility `Engine::GetConfigControl()` facade.

## Context
- Owner/layer: `runtime` control composition over `core` config data; state is
  global.
- Engine must resolve boot configuration and honor a non-empty
  `Render.DefaultRecipeConfigPath` before frame zero even when the optional
  live-control module is omitted. It need not own the domain control facade,
  app-section registry lifecycle, or apply callbacks.
- Live behavior is synchronous today: startup applies before application
  initialization, and a valid hot apply mutates renderer/config state and
  dispatches section callbacks before the call returns. The superseded
  completed-frame-boundary wording was contradicted by production and tests;
  no frame hook or queue is required.
- The right-sized seam is one plain config-kernel capability carrying the
  borrowed active config, framebuffer-extent reader, frame-recipe-override
  setter, and startup recipe result/state. Shared free recipe functions use
  that capability for both kernel startup and live control; it is not a
  service/interface/class.
- `EngineConfigControl` owns the sole app-section registry. Production creates
  the control object before `ResolveEngineConfigForBoot`, resolves boot through
  that exact registry object, and then moves the same control into
  `Engine::AddModule`.
- `GRAPHICS-127` is deliberately gated on this owner and `RUNTIME-182`: its
  profiler field and Frame Graph presentation must extend the settled
  config/UI lanes rather than create transitional Engine facades.
- The module must preserve the P3 contract: config file, UI, and agent/CLI use
  one side-effect-free preview/validate-then-apply path.

## Control surfaces
- Config: existing engine config documents and registered `app.sections`.
- UI: existing Sandbox config and frame-recipe panels.
- Agent/CLI: existing `RuntimeConfigControlSource::AgentCli` operations through
  the resolved module service.

## Status
- Completed and retired at `Operational` on 2026-07-19; owner: Codex team;
  implementation branch: `codex/runtime-181-config-control-module`.
- Implementation commit: `bc90ed71`; merged to `main` as `763505dc`;
  research trace recorded on `main` as `9f6370dc`.
- Omission, lifecycle, caller, and test inventories completed 2026-07-19.
  Focused behavior, app compilation, the complete default CPU-supported gate,
  and strict structural gates are recorded below.

## Required changes
- [x] Make the existing `EngineConfigControl` itself `final : IRuntimeModule`;
      own one `RuntimeEngineConfigSectionRegistry`, expose it before boot, and
      publish/withdraw the exact control instance through `ServiceRegistry`.
- [x] Factor recipe context creation, preview/load, apply, clear, and
      initialize-reset into shared free functions over one plain narrow kernel
      capability. Engine startup and the control module must call the same
      functions; the control stores no raw `IWindow*` or `IRenderer*`.
- [x] Keep startup recipe activation in `Engine::Initialize()`, reset the
      entire activation state/override unconditionally each initialize, then
      conditionally load the configured path. Module omission must still
      produce the configured first frame or fail closed to defaults.
- [x] Bind the active config and current boot's narrow callbacks/state during
      module registration/resolution. Preserve synchronous hot apply and
      post-commit lexical app-section callback order without a frame hook.
- [x] On shutdown, clear live bindings, withdraw the exact service instance,
      and ensure reinitialize rebinds/reprovides against the newly created
      window/renderer without replaying callbacks or stale activation state.
- [x] Remove the config-section-registry constructor argument, registry/control
      members, `Runtime.EngineConfigControl` import, and `GetConfigControl`
      accessors from Engine.
- [x] Migrate Sandbox and callers to preboot composition plus
      `Services().Find<EngineConfigControl>()`; moduleless editor paths must
      expose unavailable/null commands rather than dereference a missing
      service.
- [x] Ratchet the exact Engine convergence snapshot from 41/19/2/30 to
      40 plain imports, 18 domain imports, two re-exports, and 29 getter names.

## Tests
- [x] Preserve parse/round-trip, invalid-preview no-mutation, hot apply,
      immediate renderer/config mutation, app-section callback ordering, and
      Editor/AgentCli/Programmatic source-tag coverage.
- [x] Cover valid, invalid/missing, and empty startup paths with the module
      both composed and omitted; a valid omitted-module path must affect frame
      zero, while invalid/empty paths use defaults.
- [x] Prove a composed module observes the kernel's already-applied startup
      state without loading twice; invalid hot candidates preserve a prior
      override, while a valid hot change to an empty path clears it
      synchronously.
- [x] Add shutdown/reinitialize coverage for exact withdrawal/republication,
      no boot callback replay, stale direct-override clearing on an empty-path
      second boot, and apply targeting the newly created renderer.
- [x] Preserve an omitted app section's current value and emit no callback.
- [x] Add an Engine-layering regression for module ownership and update exact
      convergence/gate-routing ratchets.
- [x] Run focused config/recipe/control coverage and strict layering.
- [x] Run the complete default CPU-supported gate before retirement.

## Docs
- [x] Update runtime config-control, frame-graph, and Sandbox control-surface
      documentation.
- [x] Regenerate the module inventory.

## Acceptance criteria
- [x] Engine retains `EngineConfig` and startup recipe-activation substrate
      only; it imports/owns/exposes no config-control domain facade or
      app-section registry.
- [x] Exactly one config draft/validation/apply lane serves files, UI, and
      agent/CLI.
- [x] The optional module does not determine boot recipe semantics, and live
      apply remains synchronous.
- [x] App-section defaults and callbacks remain deterministic across boot,
      hot apply, shutdown, and reinitialize.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'ConfigControl|RenderRecipe|ConfigSection|Profiler' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

Retirement evidence (2026-07-19):

- Canonical `ci` built `IntrinsicRuntimeContractTests`,
  `IntrinsicRuntimeIntegrationTests`, and
  `IntrinsicSandboxEditorIntegrationTests`; focused config-control,
  recipe-activation, optional-editor, app-section, profiler, and
  parameterization coverage passed 94/94 CTest cases.
- A sandbox-capable non-headless build linked `ExtrinsicSandbox`, including the
  app-owned preboot composition in `main.cpp`.
- `IntrinsicTests` built 1,163/1,163 targets, including conditional GPU smoke
  callers. The complete default CPU-supported selector passed 4,137/4,137
  cases with one expected GLFW/LSan capability skip.
- Live test-gate reconciliation covered 36 targets, 4,188 cases, and 338
  assertion sources.
- Strict kernel convergence observed 40 plain imports, 18 domain imports, two
  re-exports, and 29 public getter names; strict layering scanned 748 files and
  6,589 references with zero violations.
- Strict task policy validated 143 tasks with zero findings; docs checked
  2,916 relative links with no breakage; test layout and root hygiene passed.
- Kernel-convergence and test-gate-routing regression suites each passed
  19/19 self-tests; the generated inventory contains 389 modules.

## Forbidden changes
- Adding a second config-control class, registry, or application-only apply
  route.
- Applying config mutations directly from UI handlers.
- Hiding config state behind Engine-private domain glue.
- Passing raw renderer/window ownership through the control module or changing
  renderer/frame-input APIs without a demonstrated blocker.

## Maturity
- Target: `Operational`; startup and live config apply must execute through the
  canonical app/runtime path.
