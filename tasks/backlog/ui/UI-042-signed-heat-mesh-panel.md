---
id: UI-042
theme: I
depends_on: [RUNTIME-210]
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
# UI-042 — Signed Heat mesh method panel

## Goal

- Add a Mesh Processing panel for the Signed Heat surface method that selects an
  oriented halfedge source property, edits the shared runtime config, runs the
  CPU reference, and visualizes the published signed-distance field.

## Non-goals

- No Signed Heat kernel/runtime/config implementation, curve-drawing tool,
  point-cloud/volume variant, or custom renderer pass.
- No Graph or PointCloud menu entry for the surface-only implementation.

## Context

- `RUNTIME-210` supplies the legitimate faces/halfedges input gate and
  same-cardinality vertex properties. The Feng–Crane literature review and
  repository surface Variant A limitations govern labels and diagnostics.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Triangle mesh plus oriented halfedge source curve. |
| Compatible entity sources | Mesh only. |
| RuntimeModule | Consume `RUNTIME-210` copied model, config, execute, result, and history records. |
| Config/agent | Panel edits/applies the same Signed Heat config as non-UI callers. |
| UI | Register Mesh / Processing / Signed Heat and a compatible halfedge-property selector. |
| Publication | Visualize `v:signed_heat_distance` and identify `v:is_signed_heat_source` through existing property/presentation paths. |
| End-to-end tests | Menu discovery, property readiness, config parity, execute/publication, diagnostics, visualization, and history. |

## Required changes

- [ ] Register the feature-owned panel only in the Mesh Processing domain.
- [ ] Populate the oriented-source selector from runtime-filtered boolean
      halfedge properties and render exact missing/type/count/empty diagnostics.
- [ ] Route numeric edits through config preview/apply, execute the typed runtime
      command, and show status/factorization/boundary diagnostics and backend.
- [ ] Reuse scalar-property visualization/colormap controls for the distance
      output and preserve runtime-owned history/publication.

## Tests

- [ ] Assert the Mesh menu entry exists and Graph/PointCloud entries do not.
- [ ] Cover compatible/incompatible halfedge-property options, config edits,
      successful run, degenerate diagnostics, and published visualization.
- [ ] Verify UI and agent config paths resolve identically and the panel performs
      no direct ECS mutation.

## Docs

- [ ] Update Sandbox method/menu docs with the surface-only input and output
      visualization workflow.

## Acceptance criteria

- [ ] A compatible mesh exposes an operational Signed Heat panel end to end.
- [ ] Unsupported domains are absent for a documented topology reason.
- [ ] UI, config/agent, runtime publication, and tests share one contract.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'SignedHeat' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No Graph/PointCloud advertisement, UI-authored source fallback, direct ECS
  publication, duplicate config, or Signed-Heat-specific rendering subsystem.
