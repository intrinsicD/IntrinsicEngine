---
id: RUNTIME-274
theme: I
depends_on: [GEOM-061, GEOM-111]
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
- [ ] One editor operation and its own panel ("Point Sampling") offering every `Geometry.PointSampling` method (GEOM-111/112/113: random, exact and weighted farthest point, progressive Poisson profiles, the approximate FPS family, sample elimination) with their parameters, plus voxel centroid, the GEOM-061 per-voxel strategies, and octree levels once GEOM-105 lands; the progressive Poisson panel is linked and publishes through the same path.
- [x] Output chosen explicitly: a new point-cloud entity, or a Bool selection property on the source (no silent in-place deletion); deterministic with a seed.
- [ ] Backend combo (CPU / Vulkan through RUNTIME-290) and progressive prefix streaming from the seam's `Extend`.
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

## Log
- 2026-09-28 slice 1: `Extrinsic.Runtime.PointSamplingOperations` with the `sandbox.point_sampling`
  section (every `Geometry.PointSampling` method with its parameters, optional float weights or
  priority scores, output as undoable rank/selection properties or a new point-cloud entity via
  the generated-entity helper, which now accepts point clouds), the "Point Sampling" window in the
  View menu and the agent tools `preview_point_sampling` / `run_point_sampling`; contract tests
  cover every method, weights, validation, parented refusal and undo. Remaining: voxel centroid and
  the GEOM-061 per-voxel strategies and octree levels (their kernels are not implemented yet),
  mesh/graph-domain contract rows, an ImGui panel test, and the Vulkan backend (RUNTIME-290).

