---
id: RUNTIME-274
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
# RUNTIME-274 — Point-cloud subsampling editor workflow

## Goal
Let users subsample point sets in the Sandbox. Today `VoxelDownsample` and
`RandomSubsample` exist only as kernels, and GEOM-061's index-returning grid
strategies stop at CPUContracted; Framework24's sampler/subsampling systems have no
editor equivalent (inventory row "Grid, sampler, subsampling, octree sampling").

## Acceptance criteria
- [ ] One editor operation and panel offering random-N, voxel centroid, and the GEOM-061 per-voxel strategies (first, last, closest-to-center, closest-to-mean), plus octree levels once GEOM-105 lands; progressive Poisson stays its own panel but is linked.
- [ ] Output chosen explicitly: a new point-cloud entity, or a Bool selection property on the source (no silent in-place deletion); deterministic with a seed.
- [ ] Works on point clouds, graph nodes and mesh vertices; config section with one validator; undoable publication; contract and ImGui tests.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | vec3 positions of any point domain plus strategy parameters. |
| Compatible entity sources | Point clouds, graph nodes, mesh vertices. |
| RuntimeModule | New point-set operation next to the existing point-cloud operations. |
| Config/agent | `sandbox.point_subsampling`. |
| UI | PointCloud/Graph/Mesh → Processing → Subsample. |
| Publication | New entity or selection property, guarded and undoable. |
| End-to-end tests | Contract and ImGui tests per strategy and domain. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Subsampl' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
