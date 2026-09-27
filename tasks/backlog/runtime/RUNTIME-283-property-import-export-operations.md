---
id: RUNTIME-283
theme: F
depends_on: [GEOIO-005, UI-046, RUNTIME-287]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, io.geometry-format-capabilities, geometry.element-domain-sources, geometry.property-coherence]
---
# RUNTIME-283 — Property import/export operations

## Goal
- Export selected properties of an entity to PLY/CSV/NPY and import a property table
  into an existing entity's domain with validation and undo.

## Non-goals
- No whole-geometry export (UI-046 owns it; reuse its entity → `MeshIOResult` projection).
- No new file formats beyond GEOIO-005.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Publication path: `CaptureGeometryScalarProperty`/`ApplyGeometryScalarProperty` inside `CommandHistory->Execute`, marking GPU dirty as other property publishers do. Paths are checked against allowed roots when invoked by an agent (ARCH-019).

## Control surfaces
- Config: N/A (per-call commands).
- UI: File > Properties > Export…/Import… (UI-063).
- Agent/CLI: `property_export {entity, refs[], path, format}` (mutating-file), `property_import {entity, domain, path, name?, kind?}` (mutating-scene) in `Runtime.AgentOperations`; RUNTIME-282 `--export` for property tables.

## Required changes
- [ ] `Runtime.PropertyIOOperations.cppm` + implementation: `ExportEditorProperty` (read-only for the scene; refuses to overwrite a source imported this session unless `overwrite:true`), `ImportEditorProperty` (row count must equal domain element count; checked conversion; one history command "Import property").
- [ ] Agent operations registered.

## Tests
- [ ] `tests/contract/runtime/Test.PropertyIOOperations.cpp`: export/import round trip per format and domain; wrong row count rejected with no mutation; undo restores; overwrite refusal; path outside roots refused.

## Docs
- [ ] `docs/architecture/sandbox-editor-feature-boundaries.md`; module inventory regenerated.

## Acceptance criteria
- [ ] Properties round-trip through export and import on vertex, face and point-cloud domains with undo.
- [ ] Invalid tables fail closed with diagnostics and leave the scene unchanged.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PropertyIO|AgentOperations' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Duplicating UI-046's entity projection; writing outside allowed roots for agent calls; delete operations.
