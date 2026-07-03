---
id: CORE-009
theme: F
depends_on: []
---
# CORE-009 — App-owned config sections out of core EngineConfig

## Goal
- Remove method/application-specific configuration from
  `Core::Config::EngineConfig`: replace the embedded
  `SandboxConfig.ProgressivePoisson` block with a generic, schema-validated
  application config section mechanism, and genericize the Engine hot-apply
  path so it no longer enumerates one method's 18 fields.

## Non-goals
- No change to the config file format version policy or the fail-closed
  preview/apply contract (CORE-003 lane semantics stay).
- No change to Progressive Poisson behavior or defaults — values move, they
  do not change.
- No new hot-appliable engine fields beyond re-expressing the existing
  `sandbox.progressive_poisson` subset through the generic mechanism.

## Context
- Owner/layer: `core` owns the config document lane; `runtime` owns
  registration/wiring; the Poisson consumer registers its section from where
  the feature lives (today runtime editor code; `ARCH-006` may later move
  the registration to `app` with its owner — coordinate, don't block).
- Today `Core::Config::EngineConfig` embeds
  `SandboxConfig{ ProgressivePoissonPlaygroundConfig }`
  (`src/core/Core.Config.Engine.cppm:57-93`), and Engine hot-apply
  hand-compares its fields via `ProgressivePoissonPlaygroundConfigEquals`
  with a dedicated `RuntimeEngineConfigApplyResult::SandboxProgressivePoissonChanged`
  flag (`src/runtime/Runtime.Engine.cpp:2190-2212, 3271-3329`). Core config
  should not know a research method exists.
- Existing related surfaces: `runtime-config-control.md` documents the hot
  subset as exactly `render.default_recipe_config_path` +
  `sandbox.progressive_poisson`; that doc contract must be updated in the
  same PR (docs-sync policy).
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R3.

## Control surfaces
- Config: generic `app`/`sections` lane in the engine config document;
  Poisson playground values live in a registered section.
- UI: Sandbox Poisson panel keeps reading/writing its config through the
  registered section (no user-visible behavior change).
- Agent/CLI: `Engine` config-control facade previews/applies section changes
  through the same fail-closed path as today.

## Required changes
- [ ] Add a generic named-section mechanism to the core config document
      lane: opaque payload with registered schema/validator + change
      callback; unknown sections fail closed exactly like unknown fields do
      today.
- [ ] Register the Progressive Poisson playground config as such a section
      from its owning feature code; delete `SandboxConfig` and
      `ProgressivePoissonPlaygroundConfig` from `Core.Config.Engine`.
- [ ] Genericize hot-apply: `ApplyEngineConfigHotSubset` dispatches changed
      sections to their registered callbacks; delete
      `ProgressivePoissonPlaygroundConfigEquals` and the
      Poisson-specific apply-result flag in favor of a generic
      changed-sections report.
- [ ] Migrate existing `config/engine.json`-style documents (compat read of
      the old `sandbox.progressive_poisson` shape or a documented one-time
      migration — pick one, document it).

## Tests
- [ ] Contract: registered section round-trips parse/validate/apply with
      change callback firing exactly on value change.
- [ ] Contract: unregistered/unknown section fails closed with diagnostics.
- [ ] Contract: existing Poisson hot-apply behavior preserved end-to-end
      through the generic path (same observable playground state).
- [ ] Existing engine-config and config-control suites stay green.

## Docs
- [ ] Update `docs/architecture/engine-config.md` (section mechanism) and
      `docs/architecture/runtime-config-control.md` (hot subset wording).
- [ ] Regenerate `docs/api/generated/module_inventory.md` if `.cppm`
      surfaces change.

## Acceptance criteria
- [ ] `src/core/Core.Config.Engine.cppm` contains no method/sandbox-specific
      types (grep-clean for Poisson identifiers).
- [ ] Poisson playground control keeps working via UI, config file, and the
      agent/CLI facade with unchanged semantics.
- [ ] Default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Weakening the fail-closed config contract (silent acceptance of unknown
  or invalid sections).
- Changing Poisson defaults/behavior while moving the config.
- Adding new hot-appliable engine core fields under cover of this refactor.
