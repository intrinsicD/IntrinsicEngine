---
id: UI-045
theme: I
depends_on: [RUNTIME-213]
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
# UI-045 — Point-set normal property-domain panel

## Goal

- Let users choose the point-set normal estimator for any compatible position
  property while presenting topology-aware mesh/graph normal variants only
  when their real inputs resolve.

## Non-goals

- No normal kernel/runtime/config implementation, method conflation, converter,
  or per-domain panel copies.

## Context

- `RUNTIME-213` separates generic point-set eligibility from stronger
  topology-aware methods and records the Hoppe/Mitra–Nguyen literature basis.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Point-set: selected finite `vec3`; topology-aware: explicit adjacency plus properties. |
| Compatible entity sources | Point-set on every element domain; stronger variants where their named topology resolves. |
| RuntimeModule | Consume `RUNTIME-213` method/property readiness and command results. |
| Config/agent | Edit the same validated method/config state. |
| UI | Shared method selector plus position/normal property selectors and exact requirements. |
| Publication | Visualize the named same-domain normal output. |
| End-to-end tests | Property discovery, method gating, config/run/history, diagnostics. |

## Required changes

- [ ] Group compatible position properties by element domain and filter normal
      outputs with the runtime preflight.
- [ ] Explain point-set versus topology-aware neighborhood semantics and show
      exact missing-input reasons.
- [ ] Submit only the typed runtime operation and preserve shared history and
      visualization paths.

## Tests

- [ ] Cover face/edge/halfedge/vertex/node/point property discovery and
      point-set execution plus legitimate topology-aware disabled states.
- [ ] Verify config parity and no provenance-only or handle-wrapper filter.

## Docs

- [ ] Update Sandbox normal-method menus and property selection docs.

## Acceptance criteria

- [ ] Generic point-set normal estimation is visible on every compatible
      property; stronger methods advertise only their actual topology needs.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'Normal' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No converter, provenance-only filtering, UI-owned mutation, duplicated
  method state, or unimplemented method advertisement.
