---
id: UI-043
theme: I
depends_on: [RUNTIME-211]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# UI-043 — K-Means property-domain panel

## Goal

- Let one K-Means panel select compatible input and output properties from any
  mesh, graph, or point-cloud element domain through `RUNTIME-211`.

## Non-goals

- No clustering/runtime/backend/config implementation, converter, or
  per-domain panel copy.

## Context

- Existing three-menu discovery still filters to the shared vertex source.
  Reuse `RUNTIME-211`'s Lloyd/k-means++ literature intake and runtime preflight.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Selected finite `vec3` property. |
| Compatible entity sources | Every resolved element-domain property. |
| RuntimeModule | Consume `RUNTIME-211` property catalog/readiness/config/run/results. |
| Config/agent | Edit the same validated clustering config. |
| UI | Shared provenance-menu entry plus input/label/color/scalar property selectors. |
| Publication | Display same-domain output and history consequences. |
| End-to-end tests | Menu/property discovery, config/backend routing, diagnostics, visualization, undo/redo. |

## Required changes

- [ ] Replace vertex-only choices with compatible catalog rows, grouped by
      logical element domain and filtered by runtime readiness.
- [ ] Route every action through the existing validated K-Means operation and
      preserve backend/fallback/diagnostic display.
- [ ] Bind output visualization to the selected originating domain.

## Tests

- [ ] Cover vertex, edge, halfedge, face, graph, and point property discovery,
      including face-center execution.
- [ ] Verify shared config/backend state and exact disabled reasons.

## Docs

- [ ] Update Sandbox method menu/property-selector documentation.

## Acceptance criteria

- [ ] Any compatible typed property is discoverable without conversion or a
      `VertexProperty` filter.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'KMeans|Clustering' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No UI-owned clustering/publication, duplicated config, converter, or
  per-provenance implementation.
