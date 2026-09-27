---
id: GEOM-110
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive geometry slice; evidence is the diff, geometry unit tests, review and CI.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GEOM-110 — Connected components and per-component topology/genus

## Goal
Add the one missing mesh-analysis piece for the Mesh Health report (RUNTIME-286)
and boundary/component selection queries (RUNTIME-280): connected components with
per-component Euler characteristic, boundary loops and genus.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Existing and reused: `Geometry.HalfedgeMesh.Analysis` (`Analyze`, issue masks), `Geometry.HalfedgeMesh.Quality` (`ComputeQuality`: `BoundaryLoopCount`, `EulerCharacteristic`, `IsClosed`, `Volume`), `Geometry.HalfedgeMesh.Utils` (`CollectBoundaryLoops`, `IsConnectedManifoldWithEulerOne`), `Geometry.HalfedgeMesh.Boundary`. No component counter exists.
- API: `MeshUtils::CountConnectedComponents(mesh, std::vector<uint32_t>* faceComponentOut)` (union-find over faces via edge adjacency, deleted-aware; isolated vertices reported separately) and `ComputeComponentTopology(mesh)` → per component `{Faces, Vertices, Edges, Euler, BoundaryLoops, Genus = (2 − χ − b)/2, Closed, Manifold}`.

## Control surfaces
- Config: N/A (pure kernel).
- UI: via RUNTIME-286 / UI-066.
- Agent/CLI: via RUNTIME-286 `mesh_health`.

## Acceptance criteria
- [ ] Functions added to `Geometry.HalfedgeMesh.Utils` (or a sibling partition) with doc comments on the genus formula and non-manifold handling.
- [ ] Unit tests in `tests/unit/geometry`: torus (genus 1), two disjoint spheres (2 components, genus 0), disk (1 boundary loop), deleted faces ignored, empty mesh.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'HalfedgeMeshUtils|ConnectedComponents' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Duplicating boundary-loop or Euler computation that `Quality`/`Utils` already own.
