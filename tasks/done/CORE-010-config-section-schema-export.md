---
id: CORE-010
theme: F
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, repo.task-contract-discovery]
---
# CORE-010 — `EngineConfigSectionRegistration::SchemaJson` and `ExportEngineConfigSchema`

## Completion — 2026-09-28
Commit: the enclosing `claude/config-schemas` commit records this retirement.
`EngineConfigSectionRegistration::SchemaJson` (rejected at `Register` unless it is a
JSON object) and `ExportEngineConfigSchema(registry)` (`$defs` in registry name order
with `x-schema-id`/`x-schema-version`, `x-schema-missing` placeholders, deterministic).
The read-only agent operation `config_schema {section?}` returns the export or one
section; `config_get` also returns the section's `schema`.

Deviations from the planned text, all deliberate:
- Core's implementation unit already parses JSON with nlohmann (`CanonicalObjectJson`),
  so the export is composed with it instead of string concatenation; the core
  interface stays JSON-free.
- The runtime-side check that the export parses with one `$defs` entry per
  registration is `EngineConfigSectionSchemas.ExportContainsEverySandboxSection`
  in `tests/integration/runtime` (the full registry is app-owned), next to the
  unit test `CoreEngineConfigSections.SchemaExportListsEverySectionInNameOrder`.

## Goal
- Let every registered engine-config section carry a machine-readable JSON Schema
  string and export all registered sections as one schema document, keeping core
  JSON-agnostic.

## Non-goals
- No schema generation per section (RUNTIME-276 owns the declarative field tables).
- No JSON-Schema validator dependency; no change to payload encoding (enums stay integer-coded).

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- `src/core/Core.Config.EngineLoad.cppm`: `EngineConfigSectionRegistration { EngineConfigSection DefaultSection; EngineConfigSectionValidator Validate; EngineConfigSectionChangedCallback OnChanged; }`, `EngineConfigSectionRegistry::{Register, Find, Entries}`, `EngineConfigSection { Name, SchemaId, SchemaVersion, PayloadJson }`. Payloads are opaque strings; core has no JSON library.
- 24 Sandbox registrations in `src/app/Sandbox/Sandbox.ConfigSections.cpp` (`MakeSelectionConfigSectionRegistration`, `MakePropertySmoothingConfigSectionRegistration`, …) built by `CreateSandboxConfigSectionRegistry()`.
- Consumers: RUNTIME-276 (fills `SchemaJson`), RUNTIME-287/288 (`config_schema` tool, per-section `inputSchema`), UI-057 (field hints).

## Control surfaces
- Config: schema describes every section of the engine-config document.
- UI: consumed by UI-057 field hints (through RUNTIME-276's runtime accessor, not JSON).
- Agent/CLI: `config_schema {section?}` read-only agent operation (registered in `Runtime.AgentOperations`).

## Required changes
- [x] Add `std::string SchemaJson{}` to `EngineConfigSectionRegistration` (opaque, may be empty).
- [x] Add `std::string ExportEngineConfigSchema(const EngineConfigSectionRegistry&)` in `Core.Config.EngineLoad` returning one document (`$schema`, `$defs` keyed by section `Name` with `x-schema-id` and `x-schema-version`; sections without a schema appear as `{"type":"object"}` with `x-schema-missing:true`), deterministic order, composed by string concatenation of the opaque per-section strings (no JSON library in core).
- [x] Register a read-only `config_schema` agent operation in the runtime config family (RUNTIME-287 registry) that returns the export or one section.

## Tests
- [x] `tests/unit/core` test: export ordering, missing-schema placeholder, section metadata; runtime contract test parses the export with nlohmann and checks one `$defs` entry per registration.

## Docs
- [x] `docs/architecture/runtime-config-control.md` and `docs/architecture/engine-config.md` document schema export; module inventory regenerated.

## Acceptance criteria
- [x] Every registration can carry a schema; the export is a single well-formed JSON document for the full Sandbox registry.
- [x] Core stays JSON-library-free; layering check passes.
- [x] `config_schema` agent operation returns the same document the export function produces.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'EngineConfig|RuntimeConfigControl|AgentOperations' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding a JSON library dependency to core.
- Changing payload encoding or section names.
