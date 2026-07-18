---
id: RUNTIME-181
theme: F
depends_on:
  - ARCH-016
maturity_target: Operational
---
# RUNTIME-181 — Extract the config-control composition module

## Goal
- Move live config preview, validation, apply orchestration, recipe-activation
  requests, and app-section callback ownership out of `Runtime.Engine` into
  one app-composed `ConfigControlModule`.

## Non-goals
- No config schema, precedence, serialization, or validation behavior change.
- No second config tree, draft store, apply path, or UI-only control.
- No movement of boot-critical `EngineConfig` values out of the kernel before
  window/device/renderer creation.
- No movement of the frame-loop's validated recipe-activation execution out
  of kernel substrate.
- No compatibility `Engine::GetConfigControl()` facade.

## Context
- Owner/layer: `runtime` control composition over `core` config data; state is
  global.
- Engine must read boot configuration before module resolution, but it need
  not own the domain control facade, app-section registry lifecycle, or apply
  callbacks.
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

## Required changes
- [ ] Add one concrete `ConfigControlModule` owning `EngineConfigControl`,
      render-recipe control state, the app-section registry, and change
      callbacks.
- [ ] Borrow the kernel's boot config and narrow recipe-activation capability,
      then resolve window/renderer capabilities after boot without duplicating
      config, recipe execution, or registry ownership.
- [ ] Preserve startup recipe-activation requests and completed-frame-boundary
      hot apply through module lifecycle/frame hooks; the frame loop remains
      the owner that validates and performs activation.
- [ ] Publish the existing control surface through `ServiceRegistry`; do not
      add a forwarding facade or a second apply API.
- [ ] Remove `Runtime.EngineConfigControl` from Engine state/interface and
      remove `Engine::GetConfigControl`.
- [ ] Migrate Sandbox, tests, and future app-section callers to the resolved
      control service.

## Tests
- [ ] Preserve parse/round-trip, invalid-preview no-mutation, hot apply,
      app-section callback ordering, and source-tag coverage.
- [ ] Add integration coverage proving startup and live apply through the
      app-composed module.
- [ ] Run focused config/recipe/profiler-control coverage, strict layering, and
      the complete default CPU-supported gate.

## Docs
- [ ] Update runtime config-control, frame-graph, and Sandbox control-surface
      documentation.
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Engine retains boot config and recipe-activation substrate only and
      exposes no config-control domain facade.
- [ ] Exactly one config draft/validation/apply lane serves files, UI, and
      agent/CLI.
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

## Maturity
- Target: `Operational`; startup and live config apply must execute through the
  canonical app/runtime path.
