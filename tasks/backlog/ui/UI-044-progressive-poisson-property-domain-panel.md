---
id: UI-044
theme: I
depends_on: [RUNTIME-212, UI-038]
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
# UI-044 — Progressive Poisson property-domain panel

## Goal

- Extend the existing shared Progressive Poisson panel with catalog-backed
  input/output selection over every compatible element domain.

## Non-goals

- No sampler/runtime/GPU/config change, surface sampling, converter, or edit to
  retired `UI-038` history.

## Context

- `UI-038` delivered correct Mesh/Graph/PointCloud provenance discovery but
  only for `Vertices`; `RUNTIME-212` supplies the clarified property contract.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Selected finite `vec3` property. |
| Compatible entity sources | Every resolved mesh/graph/point-cloud element domain. |
| RuntimeModule | Consume `RUNTIME-212` readiness/config/run/publication records. |
| Config/agent | Preserve one shared validated sampler config. |
| UI | Add grouped input/output property selectors to the existing shared panel. |
| Publication | Visualize named same-domain hierarchy properties. |
| End-to-end tests | Property discovery, CPU/GPU routing, visualization, disabled reasons, history. |

## Required changes

- [ ] Populate compatible properties from the canonical runtime catalog and
      keep selections stable by full `GeometryPropertyRef` identity.
- [ ] Reuse the existing shared panel state, config controls, backend
      diagnostics, and visualization choices.
- [ ] Show the originating element domain and never imply surface conversion.

## Tests

- [ ] Cover every physical property-domain family, including face centers,
      with shared config and identical hierarchy results.
- [ ] Verify CPU/Vulkan/fallback routing and no app-owned mutation.

## Docs

- [ ] Update Sandbox Progressive Poisson property workflow documentation.

## Acceptance criteria

- [ ] The retired three-provenance UI remains one panel and now exposes every
      compatible property domain.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No copied panel/config, converter, UI-owned readiness/publication, or
  surface-sampling controls.
