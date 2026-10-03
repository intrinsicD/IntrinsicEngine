---
id: GEOM-109
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive geometry slice; evidence is the diff, geometry unit tests, review and CI.
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources]
---
# GEOM-109 — `Geometry.Properties.Statistics`: statistics, histogram and comparison

## Goal
Provide pure geometry-layer statistics and comparison over any typed property of
any element domain, for the Property Inspector (UI-059) and agent inspection
(RUNTIME-278).

## Context
- Operator direction 2026-10-03: finish the larger MCP inspection and diagnostics gaps, establish tasks first, delegate with appropriate effort, then obtain Claude review. This direction takes precedence over automatic Framework24 work selection.
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Reuse check: `Runtime.EditorPropertyWidgets` `BuildEditorScalarPropertyPlotModel(ConstPropertySet, selected)` computes finite samples/min/max/non-finite count privately (runtime); parity helpers in `Runtime.GeometryValueComparison.hpp`; `Geometry.PointCloud.QualityMetrics`. None offers mean/RMS/NaN/Inf/zero counts, per-component vector stats or row comparison in geometry.
- API: `ComputePropertyStatistics(const ConstPropertySet&, std::string_view name, StatisticsParams{Bins, honor *:deleted})` → `{Kind, Count, FiniteCount, NaNCount, InfCount, ZeroCount, per-component {Min, Max, Mean, RMS, StdDev}, Histogram{Edges, Counts} (per component or magnitude)}`; `ComparePropertyValues(a, b)` → `{RowCount, ComparableRows, MaxAbsError, MeanAbsError, RMSError, MaxErrorRow, IdenticalRows}` with checked numeric conversion.

## Control surfaces
- Config: N/A (pure kernel).
- UI: via RUNTIME-278 / UI-059.
- Agent/CLI: via RUNTIME-278 `property_stats` / `property_compare`.

## Acceptance criteria
- [ ] Module with purpose synopsis; scalar (float/double/int/uint/bool) and `glm::vec2/3/4` kinds; deleted rows excluded when a `*:deleted` property exists; empty/all-non-finite inputs return defined results.
- [ ] Unit tests in `tests/unit/geometry` (`Test.GeometryPropertyStatistics.cpp`): exact values on small fixtures, NaN/Inf counts, histogram bin edges, vector components and magnitude, comparison of identical/offset/different-kind properties, row-count mismatch diagnostic.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(GeometryPropertyStatistics)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- Runtime/ECS imports in geometry; name- or provenance-based eligibility.

## Execution plan (2026-10-03)
- Interactive micro lane; one writing agent per isolated worktree. Root integrates and owns the shared `build/ci` verification tree.
- Geometry statistics and cursor/diagnostics ownership use `xhigh` effort (numerics and concurrency); runtime inspection uses `high` (domain resolution and bounded queries). UI and MCP integration are coordinated by the root agent.
- Reuse existing property/domain resolution, logger ring, operation registry, EditorShell and panel widgets; plain records and free functions. No new telemetry framework or generic property-write API.
- Dependencies determine integration order. UI/runtime paths land together before their MCP counterparts are reported complete.
- Completion requires the named behavior tests, combined verification and a read-only Claude review of a fixed diff.
