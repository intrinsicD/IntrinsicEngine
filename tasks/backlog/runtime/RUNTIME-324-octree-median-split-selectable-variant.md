---
id: RUNTIME-324
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive variant wiring; evidence is the diff and focused kernel/runtime/config/agent/panel tests.
contract_schema: 1
contracts: [repo.source-documentation, method.engine-integration, geometry.element-domain-sources, geometry.property-coherence, runtime.processing-compilation-locality]
---
# RUNTIME-324 — Octree split point Center/Mean/Median selectable in point spacing

## Goal
- Make `Octree::SplitPoint::{Center, Mean, Median}` selectable for the CPU-octree path of the
  point-spacing/radius estimation (`EstimateRadii`, `Geometry.PointCloud.Utils.cpp`, today
  hard-coded `Center`) through config, Sandbox and agent. `Center` stays the default.
- Origin: REVIEW-007 E7 (2026-10-06), GE18 — `Median`/`ComputeMedianCenter` are test-only today.
  Draft: Codex, read-only on `e47a1484b`; paths re-checked on `098d47df9`.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: compatible 3D samples and existing k/radius parameters. |
| Compatible entity sources | All domains point spacing already supports (mesh, graph, point cloud). |
| RuntimeModule | Existing `Runtime.PointFieldOperations` point-spacing execution (`Runtime.GeometryProcessingOperations.Density.cpp`). |
| Config/agent | `sandbox.point_spacing` gains `octree_split_point = center\|mean\|median` (runtime-owned enum, no `Geometry.Octree` import in the config interface); existing `operation = point_spacing` reports the effective policy or N/A. |
| UI | Split combo in the point-spacing panel, marked effective only for the CPU-octree backend. |
| Publication | Unchanged same-domain radius property, revision and undo path. |
| End-to-end tests | Each variant through config → runtime → agent/panel; dispatch of the chosen variant and unchanged neighbourhood results checked. |

## Acceptance criteria
- [ ] `RadiusEstimationParams` gains a split-point field defaulting to `Center`; only the
      `EstimateRadii` call site reads it. Other octree users (`SurfaceReconstruction`, `Graph.Utils`,
      normals, remaining `PointCloud.Utils` sites) keep their policies.
- [ ] File config, config preview/apply, panel and agent select the same policy; invalid tokens are
      rejected without mutation.
- [ ] Tests prove `Median` reaches the median dispatch (equal outputs alone are no proof); kNN
      contract, self/duplicate handling, ties and the radius definition hold for all three.
- [ ] Coincident points, axis-degenerate data, unequal clusters, ties and non-finite input are
      covered; no lost elements.
- [ ] LBVH backends report no effective octree policy; the panel does not claim one.
- [ ] `spatial-indices.md`, `spatial-index-consumers.md`, `agent-control-lane.md` and the Sandbox
      README are updated.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(Octree|PointSpacing|PointCloud_Radius|PointSpacingOperations|PointSpacingConfig|AgentOperations|SandboxConfigSections|SandboxProcessingPanels|RuntimeEngineLayering|RuntimeEnginePrivateGlue|ProcessingCompilationLocality)\.|^PointCloud_Integration\.DownsampleThenEstimateRadii$'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Context
- Possible follow-up (not filed): a matched-baseline performance comparison of the three split
  policies needs its own task and an `ara/logic/claims.md` row; this task makes no performance statement.
- No GPU kernel or render path changes, so no Vulkan test is owed; if the shared Vulkan point
  dispatch is touched after all, rerun `PointLBVHGpuSmoke.PointSpacingPublishesAcrossDomainsAndPreservesCandidatePolicy`.
- No global octree setting, `SpatialIndexCache` change or `ComputeMedianCenter` change.
  GEOM-105/RUNTIME-274 may adopt the variant later; METHOD-032 keeps its own corner lattice.
