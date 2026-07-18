---
id: CORE-009
theme: F
depends_on: []
maturity_target: Operational
---
# CORE-009 — App-owned config sections out of core EngineConfig

## Goal

- Remove application/method configuration from
  `Core::Config::EngineConfig`. Replace its typed `SandboxConfig` with a
  generic, schema-validated named-section lane, move both current Sandbox
  sections to their owning feature surface, and make live apply report and
  dispatch section changes without enumerating feature fields.

## Non-goals

- No change to engine-config schema-version policy or the fail-closed
  preview/validate/apply contract.
- No change to Progressive Poisson or parameterization behavior, defaults,
  tokens, or UI. Values move; semantics do not.
- No new hot-appliable engine fields beyond the two existing Sandbox sections.
- No `IConfigSection` hierarchy, `std::any`, service/factory framework, global
  registry, or reuse of `ServiceRegistry`; boot config resolves before runtime
  service/module registration.
- No compatibility parser that keeps Sandbox field knowledge in core. No
  checked-in `config/engine.json` exists, so document a one-time path migration.
- No runtime-module ownership or scope change; `ARCH-015` owns that policy.

## Context

- Ownership: `core` owns generic config document records and fail-closed JSON
  orchestration; `runtime` owns the live control facade and existing
  presentation-free Sandbox config DTOs/commands; `app/Sandbox` owns pre-boot
  section composition.
- Retired `ARCH-006` moved Sandbox presentation to `src/app/Sandbox`. Live
  apply now lives in `Runtime.EngineConfigControl`, not `Runtime.Engine`.
- `Core.Config.Engine` currently embeds both
  `ProgressivePoissonPlaygroundConfig` and the full parameterization config
  family inside `SandboxConfig`. Deleting only the Poisson block would leave
  the same ownership defect and would not permit `SandboxConfig` deletion.
- Sandbox calls `ResolveEngineConfigForBoot(...)` before constructing
  `Engine`; `EngineSetup` and `ServiceRegistry` registration happen later in
  `Engine::Initialize()`. The same narrow registry therefore must be created
  by Sandbox before boot parsing and handed to the Engine-owned live controller.
- `EngineConfigControl` currently hand-compares both typed sections and reports
  `SandboxProgressivePoissonChanged` /
  `SandboxParameterizationChanged`. Those flags and equality helpers are
  feature knowledge in the composition root.
- Two present sections plus the concrete future `RUNTIME-175` consumer justify
  one plain vector-backed registry. Linear lookup is sufficient.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R3 and the `ARCH-013` post-seam re-review.

## Status

- In progress; owner: Codex; branch:
  `codex/core-009-app-config-sections`; activated after CORE-008 retirement on
  2026-07-18.
- Right-sizing decision: one generic section record in `EngineConfig` and one
  plain registration descriptor/registry. Registrations carry stable name,
  schema/version, canonical default payload, validator, and non-failing change
  callback. They do not create a polymorphic service system.
- File migration decision: replace `sandbox.progressive_poisson` and
  `sandbox.parameterization` with registered entries under `app.sections`;
  document a one-time migration rather than retaining app-specific legacy
  parsing in core.

## Control surfaces

- Config: generic `app.sections` records in the engine config document.
- UI: Sandbox method panels continue to edit the same typed drafts and submit
  through the validated engine-config path.
- Agent/CLI: `EngineConfigControl` preview/file/apply entry points remain the
  co-equal non-ImGui surface and expose deterministic changed-section names.

## Required changes

- [ ] Replace `SandboxConfig` with plain generic section records carrying a
      stable section name, schema id/version, and canonical opaque JSON payload.
      Keep `nlohmann::json` private to implementation units.
- [ ] Add a small vector-backed registration lane with defaults, validation,
      and change callbacks. Preview must retain registered reference defaults
      and diagnose unknown, duplicate, schema/version-mismatched, or invalid
      sections without side effects.
- [ ] Move Progressive Poisson and parameterization types, tokens,
      serialization, equality, and validation out of `Core.Config.Engine` into
      the existing presentation-free Sandbox feature surface; delete
      `SandboxConfig` and the specialized runtime apply flags/helpers.
- [ ] Compose both registrations in `app/Sandbox` before
      `ResolveEngineConfigForBoot(...)`; pass the same registry into the
      Engine-owned live config controller so boot, file, UI, and agent/CLI
      paths share one contract.
- [ ] Genericize hot apply: preflight recipe-path changes, compare canonical
      section records deterministically, commit atomically, report ordered
      changed-section names, and fire each changed section callback exactly
      once after a successful commit. No-change and rejected applies fire none.
- [ ] Document the one-time JSON path migration and update `RUNTIME-175` to
      consume this section lane rather than adding another `SandboxConfig`
      field.

## Tests

- [ ] Core contracts: registered sections round-trip canonical payloads and
      reference defaults; unknown/duplicate/bad-schema/bad-version/invalid
      records fail closed with stable diagnostics.
- [ ] Runtime contracts: changed-section reporting is deterministic; callbacks
      fire once only for committed value changes and never for no-change,
      invalid, or boot-only-rejected candidates.
- [ ] Sandbox contracts: Progressive Poisson and parameterization retain
      configured behavior through file, editor, and agent/CLI paths using the
      generic section lane.
- [ ] Operational integration: Sandbox registers both schemas before boot
      preview, passes the registry into a Null `Engine::Run()` path, and a live
      section apply reaches the owning callback/state.
- [ ] Existing engine-config, config-control, Sandbox method/panel, and
      parameterization GPU-smoke compile contracts stay green.

## Docs

- [ ] Update `docs/architecture/engine-config.md`,
      `docs/architecture/runtime-config-control.md`, relevant core/runtime/app
      READMEs, and the parameterization/config roadmap references.
- [ ] Regenerate `docs/api/generated/module_inventory.md` after module-surface
      changes and `tasks/SESSION-BRIEF.md` after task-state changes.

## Acceptance criteria

- [ ] `src/core/` is grep-clean for Sandbox, Progressive Poisson, and
      parameterization config identifiers.
- [ ] `RuntimeEngineConfigApplyResult` contains no feature-specific flags and
      reports deterministic changed-section names.
- [ ] Both current Sandbox sections preserve UI, file, configured-command, and
      agent/CLI semantics with unchanged defaults.
- [ ] Unknown or invalid sections never replace registered defaults or invoke
      callbacks; boot-only field differences still reject the whole hot apply.
- [ ] Focused Operational integration and the default CPU gate are green with
      no new layering violation.

## Maturity

- Target: `Operational`. Deterministic core/runtime contracts are necessary but
  insufficient; retirement also requires an integration-labeled Sandbox
  pre-boot registration plus Null `Engine::Run()` and live callback proof.
- No Vulkan-specific follow-up is owed: the config registry is CPU/headless.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'EngineConfig|RuntimeConfigControl|Sandbox.*Config' --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- Weakening fail-closed handling by silently accepting unknown, duplicate,
  mismatched, or invalid section data.
- Calling section callbacks during preview or before the entire hot apply has
  passed preflight and committed.
- Changing Sandbox method defaults/behavior while moving ownership.
- Adding app/method types, field names, serializers, or validators back into
  core.
- Adding an interface/factory/service layer for a two-section value registry.
