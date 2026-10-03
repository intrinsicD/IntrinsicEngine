---
id: RUNTIME-278
theme: F
depends_on: [GEOM-109, RUNTIME-287]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: operator-directed interactive implementation; diff, focused tests, combined verification and Claude review provide evidence.
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources]
---
# RUNTIME-278 — Property inspection operations (stats, compare, values)

## Goal
- Provide read-only editor queries for statistics, comparison and paged values of any
  typed property on any element domain of an entity, shared by the Property
  Inspector (UI-059) and agent operations.

## Non-goals
- No mutation, no history entries, no new state; "before/after an operation" compares the operation's output property against its input.

## Context
- Operator direction 2026-10-03: finish the larger MCP inspection and diagnostics gaps, establish tasks first, delegate with appropriate effort, then obtain Claude review. This direction takes precedence over automatic Framework24 work selection.
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Resolution path: `BuildGeometryAvailability` + `ResolveGeometryPropertySet`; catalogs from `GetEditorPointInputCatalog` / domain catalogs (`EditorDomainWindowModel::PropertyCatalog` in `Runtime.EditorWorkspaceSnapshots.cppm`).
- `src/runtime/Editor/Runtime.EditorPropertyWidgets.cppm` `BuildEditorScalarPropertyPlotModel` duplicates min/max/finite filtering; switch it to GEOM-109.

## Control surfaces
- Config: N/A (read-only queries).
- UI: Property Inspector window (UI-059); the Appearance histogram reuses the same model.
- Agent/CLI: read-only `property_list {entity}`, `property_stats {entity, domain, name, bins}`, `property_compare {entity, a, b}`, `property_values {entity, domain, name, offset, limit ≤ 65536}` registered in `Runtime.AgentOperations`.

## Required changes
- [ ] `Runtime.PropertyInspectionOperations.cppm` + implementation: `GetEditorPropertyStatistics(commands, entity, GeometryPropertyRef, bins)`, `CompareEditorProperties(commands, entity, refA, refB)`, `ReadEditorPropertyValues(commands, entity, ref, offset, limit)` returning typed results with `EditorDiagnostic` on failure.
- [ ] `BuildEditorScalarPropertyPlotModel` reuses GEOM-109 (private min/max loop removed).
- [ ] Agent operations registered with input schemas and row bounds.

## Tests
- [ ] `tests/contract/runtime/Test.PropertyInspectionOperations.cpp` on `EditorFeatureTestContext`: vertex, face, edge and point-cloud properties; compare smoothing output vs input; paging bounds; missing entity/property diagnostics; no history revision change.

## Docs
- [ ] `docs/architecture/sandbox-editor-feature-boundaries.md` lists the inspection queries; module inventory regenerated.

## Acceptance criteria
- [ ] Statistics/compare/values work for every canonical element domain via the shared resolution path.
- [ ] Queries are side-effect free (document revision and history unchanged).
- [ ] Agent operations return the same data as the runtime functions.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(PropertyInspection|EditorPropertyWidgets|AgentOperations)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- Unbounded value dumps; any mutation or cached inspection state.
- ARCH-019 exclusions (no generic property write).

## Execution plan (2026-10-03)
- Interactive micro lane; one writing agent per isolated worktree. Root integrates and owns the shared `build/ci` verification tree.
- Geometry statistics and cursor/diagnostics ownership use `xhigh` effort (numerics and concurrency); runtime inspection uses `high` (domain resolution and bounded queries). UI and MCP integration are coordinated by the root agent.
- Reuse existing property/domain resolution, logger ring, operation registry, EditorShell and panel widgets; plain records and free functions. No new telemetry framework or generic property-write API.
- Dependencies determine integration order. UI/runtime paths land together before their MCP counterparts are reported complete.
- Completion requires the named behavior tests, combined verification and a read-only Claude review of a fixed diff.
