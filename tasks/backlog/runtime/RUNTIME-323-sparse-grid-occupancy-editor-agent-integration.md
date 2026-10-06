---
id: RUNTIME-323
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive integration of an existing CPU container; evidence is the diff, deterministic CPU tests, property/config/agent/panel tests, a Vulkan acceptance smoke, review and CI.
contract_schema: 1
contracts: [repo.source-documentation, method.engine-integration, geometry.element-domain-sources, geometry.property-coherence, runtime.processing-compilation-locality, runtime.editor-prepared-frame-locality]
---
# RUNTIME-323 — Grid occupancy via SparseGrid in Sandbox and agent

## Goal
- Give the test-only `Geometry::Grid::SparseGrid` (`Geometry.Grid.cppm`) a product consumer:
  bin samples on a configured grid, count occupied cells in a `SparseGrid`, and publish each
  sample's cell occupancy (sample count) as a same-domain scalar property.
- Proposed product case (Codex draft, not a separate operator decision): a small, visible
  end-to-end use without a PDE or volume framework.
- Origin: REVIEW-007 E7 (2026-10-06), GE14. Draft: Codex, read-only on `e47a1484b`;
  paths re-checked on `098d47df9`.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Finite `vec3` position property plus an explicit grid (origin, cell size). |
| Compatible entity sources | All compatible mesh/graph/point-cloud element domains, including faces, edges and halfedges. |
| RuntimeModule | Existing `Runtime.PointFieldOperations`, new CPU operation `SparseGridOccupancy` reusing `PointScalarCapture`/`PublishPointScalarField` (`Runtime.GeometryProcessingOperations.PointFields.hpp`). |
| Config/agent | `sandbox.sparse_grid` (source, position/output property, origin, cell size, block budget); `operation = sparse_grid_occupancy`. |
| UI | Shared grid-occupancy window reachable from all matching provenance menus; existing property picker and Show path. |
| Publication | One scalar per source slot on the same domain, no topology change, unrelated properties kept, one undo step. |
| End-to-end tests | Each domain → config/panel/agent → property → inspector/visualization, revision and save/load. |

## Acceptance criteria
- [ ] Cell index is `floor((p - origin) / cell_size)` with defined boundary behaviour and coordinate
      space; occupancy is the sample count per cell, not a normalized density.
- [ ] Output is independent of hash iteration order and property names; allocated-but-empty slots in
      a block (8³ per block) never count as occupancy.
- [ ] Invalid cell size, non-finite input, coordinate/index overflow, block/memory budget and
      unrepresentable output are rejected before mutation.
- [ ] Deleted slots, aliasing, an existing output property and undo/redo follow the existing
      point-scalar publication contract; preview allocates no persistent grid; worker results publish
      only for live sources and an active attachment.
- [ ] UI and agent share config/readiness/apply; the property is inspectable, saveable and visible
      (`RuntimeSandboxAcceptanceGpuSmoke.SparseGridPropertyIsVisible`).
- [ ] `Geometry.Grid` does not enter the config/prepared-frame closure; docs
      (`geometry.md`, `property-coherence.md`, `agent-control-lane.md`, READMEs) and the module
      inventory are updated.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(GridDimensions|SparseGrid|SparseGridOperations|SparseGridConfig|PointSpacingOperations|AgentOperations|SandboxConfigSections|SandboxProcessingPanels|SandboxEditorSessionLifecycle|RuntimeEngineLayering|RuntimeEnginePrivateGlue|ProcessingCompilationLocality|EditorCompilationLocality)\.'
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.SparseGridPropertyIsVisible$'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
```

## Context
- No SparseGrid layout change, voxel ECS, sparse marching cubes, GPU grid or PDE. METHOD-003 keeps
  the closest-point PDE use of grids; GEOM-013 and METHOD-033 keep their meshing/Poisson grids.
- GEOM-084 (voxel downsampling) and GEOM-061 are neighbours, not dependencies.
