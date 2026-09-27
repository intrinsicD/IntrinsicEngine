---
id: UI-063
theme: F
depends_on: [RUNTIME-283]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI slice; evidence is the diff, the ImGui window test, review and CI.
contract_schema: 1
contracts: [geometry.element-domain-sources]
---
# UI-063 — File > Properties import/export window

## Goal
Let users export chosen properties of an entity and import a property table into
an entity domain from the File menu.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Commands from RUNTIME-283; path via the UI-047 chooser when available. UI-048 records that `File/*` windows currently live under `View`; this window adds a top-level `File` menu path for `file.properties`.

## Control surfaces
- Config: N/A.
- UI: `file.properties` window (entity, property multi-select from the catalog, format combo, path, result/diagnostics) under `File > Properties`.
- Agent/CLI: `property_export`/`property_import` (RUNTIME-283).

## Acceptance criteria
- [ ] Window registered under a `File` menu path; export and import both reachable; readiness reasons shown.
- [ ] ImGui test exports a face property to CSV in a temp dir and imports it back as a new property.
- [ ] `docs/architecture/sandbox-editor-feature-boundaries.md` window list updated.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|PropertyIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- File IO in app code.
