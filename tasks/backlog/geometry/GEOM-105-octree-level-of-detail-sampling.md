---
id: GEOM-105
theme: I
depends_on: [GEOM-061]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from the 2026-09-27 Framework24 gap audit; implementation owes its own tests and evidence.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources, geometry.property-coherence]
---
# GEOM-105 — Octree level-of-detail point sampling

## Goal
Pick one representative per octree node at a chosen depth (first, closest to node
center, closest to node mean, or the mean), giving a browsable multi-resolution
point LOD that grid sampling cannot. Framework24 `bcg_octree_sampling*.h` is the
checklist; `Geometry.Octree` already exists.

## Acceptance criteria
- [ ] Index-returning kernel per depth with deterministic tie-breaking, shared reduction semantics with GEOM-061.
- [ ] Tests: counts per level, representative choice, monotone refinement across depths, empty/degenerate inputs.
- [ ] Offered as strategies in RUNTIME-274's subsampling operation.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | vec3 samples plus depth and strategy. |
| Compatible entity sources | Point domains via RUNTIME-274. |
| RuntimeModule | RUNTIME-274. |
| Config/agent | RUNTIME-274 strategy values. |
| UI | RUNTIME-274 panel. |
| Publication | RUNTIME-274. |
| End-to-end tests | Kernel tests here; editor path in RUNTIME-274. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Octree' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
