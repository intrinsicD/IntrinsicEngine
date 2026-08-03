---
id: RUNTIME-212
theme: I
depends_on: [RUNTIME-208, HARDEN-087]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-212 — Progressive Poisson property-domain publication

## Goal

- Generalize the retired `RUNTIME-208` vertex-source integration so
  Progressive Poisson consumes any selected finite `vec3` property and
  publishes its source-cardinality hierarchy attributes on that same element
  domain.

## Non-goals

- No sampler, CPU/GPU backend, hierarchy, or visualization-semantic change.
- No surface sampling, cardinality mutation, converter, or edit to retired
  task history.
- No ImGui work; `UI-044` owns property selection/discovery.

## Context

- `RUNTIME-208` correctly removed provenance conversion but still hardcodes
  `Vertices`/`v:position`; face centers and edge/halfedge samples remain hidden.
- Re-check the repository working-draft formulation plus Brandt et al.'s
  visibility-aware progressive farthest-point sampling (DOI
  `10.1111/cgf.13848`) and Yuksel's weighted sample elimination (DOI
  `10.1111/cgf.12538`). These comparisons operate on sample values/order and do
  not justify a vertex-property restriction or numerical substitution.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | One finite `Property<vec3>` and validated sampler config. |
| Compatible entity sources | Every resolved mesh/graph/point-cloud element-domain property. |
| RuntimeModule | Extend the existing Progressive Poisson command/job/GPU participant path. |
| Config/agent | Preserve one validated sampler config; add canonical property refs to the request. |
| UI | `UI-044` supplies catalog-backed property selection. |
| Publication | Named same-cardinality level/rank/radius/prefix properties on the originating element domain. |
| End-to-end tests | CPU/GPU/fallback property-domain matrix, visualization binding, staleness, history, and UI parity. |

## Required changes

- [ ] Add canonical input/output property refs and resolve every logical
      element domain through `GeometryAvailability`.
- [ ] Carry the selected domain through CPU/GPU job identity, stale checks,
      diagnostics, visualization recipe, and undo/redo publication.
- [ ] Preserve topology, order, provenance, presentation, and all unrelated or
      custom properties while updating only the four named outputs.
- [ ] Export one copied property-aware readiness result for all callers.

## Tests

- [ ] Parameterize identical sample values across every physical property
      domain, including mesh faces, and compare hierarchy attributes.
- [ ] Preserve CPU/Vulkan/fallback parity plus topology/custom-property and
      apply/undo/redo identity.
- [ ] Reject stale selected-property results without partial publication.

## Docs

- [ ] Update Progressive Poisson method/runtime docs and the integration audit
      without modifying retired `RUNTIME-208`/`UI-038` records.

## Acceptance criteria

- [ ] Sampler eligibility and publication are property-domain based, not
      `Vertices` based.
- [ ] Existing numerical/backend behavior remains unchanged.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No implicit surface sampling, conversion, topology/cardinality change,
  duplicate sampler path, or silent paper/backend substitution.
