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
- In progress; owner: Codex team; branch:
  `codex/runtime-181-config-control-module`; activated 2026-07-19.
- Omission, lifecycle, caller, and test inventories completed 2026-07-19; the
  implementation gate is open.

## Required changes
- [ ] Make the existing `EngineConfigControl` itself `final : IRuntimeModule`;
      own one `RuntimeEngineConfigSectionRegistry`, expose it before boot, and
      publish/withdraw the exact control instance through `ServiceRegistry`.
- [ ] Factor recipe context creation, preview/load, apply, clear, and
      initialize-reset into shared free functions over one plain narrow kernel
      capability. Engine startup and the control module must call the same
      functions; the control stores no raw `IWindow*` or `IRenderer*`.
- [ ] Keep startup recipe activation in `Engine::Initialize()`, reset the
      entire activation state/override unconditionally each initialize, then
      conditionally load the configured path. Module omission must still
      produce the configured first frame or fail closed to defaults.
- [ ] Bind the active config and current boot's narrow callbacks/state during
      module registration/resolution. Preserve synchronous hot apply and
      post-commit lexical app-section callback order without a frame hook.
- [ ] On shutdown, clear live bindings, withdraw the exact service instance,
      and ensure reinitialize rebinds/reprovides against the newly created
      window/renderer without replaying callbacks or stale activation state.
- [ ] Remove the config-section-registry constructor argument, registry/control
      members, `Runtime.EngineConfigControl` import, and `GetConfigControl`
      accessors from Engine.
- [ ] Migrate Sandbox and callers to preboot composition plus
      `Services().Find<EngineConfigControl>()`; moduleless editor paths must
      expose unavailable/null commands rather than dereference a missing
      service.
- [ ] Ratchet the exact Engine convergence snapshot from 41/19/2/30 to
      40 plain imports, 18 domain imports, two re-exports, and 29 getter names.

## Tests
- [ ] Preserve parse/round-trip, invalid-preview no-mutation, hot apply,
      immediate renderer/config mutation, app-section callback ordering, and
      Editor/AgentCli/Programmatic source-tag coverage.
- [ ] Cover valid, invalid/missing, and empty startup paths with the module
      both composed and omitted; a valid omitted-module path must affect frame
      zero, while invalid/empty paths use defaults.
- [ ] Prove a composed module observes the kernel's already-applied startup
      state without loading twice; invalid hot candidates preserve a prior
      override, while a valid hot change to an empty path clears it
      synchronously.
- [ ] Add shutdown/reinitialize coverage for exact withdrawal/republication,
      no boot callback replay, stale direct-override clearing on an empty-path
      second boot, and apply targeting the newly created renderer.
- [ ] Preserve an omitted app section's current value and emit no callback.
- [ ] Add an Engine-layering regression for module ownership and update exact
      convergence/gate-routing ratchets.
- [ ] Run focused config/recipe/profiler-control coverage, strict layering, and
      the complete default CPU-supported gate.

## Docs
- [ ] Update runtime config-control, frame-graph, and Sandbox control-surface
      documentation.
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Engine retains `EngineConfig` and startup recipe-activation substrate
      only; it imports/owns/exposes no config-control domain facade or
      app-section registry.
- [ ] Exactly one config draft/validation/apply lane serves files, UI, and
      agent/CLI.
- [ ] The optional module does not determine boot recipe semantics, and live
      apply remains synchronous.
- [ ] App-section defaults and callbacks remain deterministic across boot,
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
