---
id: GEOM-026
theme: none
depends_on: [GEOM-012]
maturity_target: CPUContracted
---
# GEOM-026 — Cross-domain vertex normal recomputation contracts

## Goal
- Add geometry-owned CPU contracts for recomputing count-matched vertex/node/point normals for meshes, graphs, and point clouds, with deterministic diagnostics suitable for runtime/editor publication to `v:normal`.

## Non-goals
- No runtime, ECS, UI, asset, graphics, RHI, platform, or app integration.
- No mutation of engine entities or `Geometry::PropertySet` ownership from the geometry layer.
- No GPU backend, async scheduler, streaming executor, or benchmark performance claim.
- No tangent/bitangent generation, normal-map baking, texture baking, or shading-material changes.
- No paper-specific method package under `methods/`.

## Context
- Status: done (retired 2026-06-21; CPUContracted).
- Owner/agent: Codex.
- Branch / PR: current branch / TBD.
- Next verification step: none; retired after focused geometry tests, full
  CPU-supported CTest, strict structural checks, doc-link checks, docs-sync
  diff validation, and module inventory regeneration passed on 2026-06-21.
- Owning subsystem/layer: `src/geometry` (`geometry -> core` only).
- The algorithms should live beside their owning domain containers, not in one
  catch-all module:
  - mesh: `Geometry.HalfedgeMesh.Vertices.Normals.cppm` + `.cpp`
    (`export module Geometry.HalfedgeMesh.Vertices.Normals`);
  - graph: `Geometry.Graph.Vertex.Normals.cppm` + `.cpp`
    (`export module Geometry.Graph.Vertex.Normals`);
  - point cloud: `Geometry.PointCloud.Normals.cppm` + `.cpp`
    (`export module Geometry.PointCloud.Normals`).
- The existing `Geometry.NormalEstimation` point-cloud API should be renamed/
  migrated to `Geometry.PointCloud.Normals`. The new editor workflow requested
  by `UI-022` needs point-cloud normals computed through `Geometry.KDTree`
  and/or an explicitly supplied spatial index overload, graph normals computed
  from edge connectivity, and mesh normals computed from face normals using
  selectable averaging schemes.
- Runtime currently has a private mesh fallback in `src/runtime/Runtime.AssetMeshNormals.cpp`; this task promotes the reusable normal recomputation contract into geometry before editor commands depend on it.
- `GEOM-012` already established shared mesh/graph/point-cloud domain-view semantics. This task should expose property-set/container overloads that return the written normal `Property<glm::vec3>` / `VertexProperty<glm::vec3>` rather than importing ECS `GeometrySources` or runtime property names.
- `UI-022` depends on this task and owns the Sandbox EditorUI windows and `v:normal` publication.

## Slice plan
- **Slice A — mesh module.** Add `Geometry.HalfedgeMesh.Vertices.Normals`
  with APIs over `HalfedgeMesh::Mesh` and over the required mesh property sets.
  The methods write/get-or-add a vertex normal property, return the corresponding
  `VertexProperty<glm::vec3>` or raw `Property<glm::vec3>`, expose selectable
  uniform / area-weighted / angle-weighted / Max-style face-normal averaging, record
  diagnostics, and add mesh unit tests. `UI-022` remains responsible for ECS
  publication.
- **Slice B — graph module.** Add `Geometry.Graph.Vertex.Normals` with
  connectivity-derived local normal estimation over `Graph::Graph` and over
  the required graph property sets. The methods write/get-or-add a vertex/node
  normal property, return the corresponding `VertexProperty<glm::vec3>` or raw
  `Property<glm::vec3>`, and document that graph normals are ambiguous without
  a surface. The first contract uses incident-edge neighborhoods / PCA-style
  local frames, deterministic fallback normals for underconstrained vertices,
  and diagnostics for isolated, degree-one, collinear, duplicate, and
  invalid-edge cases.
- **Slice C — point-cloud module.** Rename/migrate `Geometry.NormalEstimation`
  to `Geometry.PointCloud.Normals` with KDTree-backed neighborhood search, PCA
  local plane fitting, optional orientation propagation, and diagnostics. The
  primary methods write/get-or-add a point/vertex normal property and return
  the corresponding `VertexProperty<glm::vec3>` or raw `Property<glm::vec3>`.
  Provide overloads that accept caller-owned `Geometry::KDTree` and/or
  `Geometry::Octree` spatial indexes, plus an overload that builds the selected
  index internally from a `PointCloud::Cloud` or `Vertices` property set.
- **Slice D — compatibility and aggregation.** Update repo consumers/tests from
  `Geometry.NormalEstimation` to `Geometry.PointCloud.Normals`, add the new
  module files to `src/geometry/CMakeLists.txt`, export them from
  `Geometry.cppm`, and refresh generated module inventory. Any temporary
  `Geometry.NormalEstimation` compatibility shim must be documented with a
  removal follow-up; prefer deleting the old module once consumers are migrated.

## Implementation plan
- Identify the task: this active slice completes `GEOM-026`; `GRAPHICS-095` is
  the next unblocked rendering backlog task, but the requested attached task is
  the geometry prerequisite that unblocks `UI-022`.
- Preserve the completed mesh slice and keep its existing
  `Geometry.HalfedgeMesh.Vertices.Normals` API behavior unchanged except for
  any compile fixes forced by shared helper changes.
- Add `Geometry.Graph.Vertex.Normals` as a graph-owned CPU module with a
  container overload over `Graph::Graph` and a raw `Vertices`/topology-property
  overload. Both paths scan edge slots deterministically, write/get-or-add the
  configured output property, return the written normal handle, use PCA over
  incident-edge neighborhoods, and report isolated, degree-one, duplicate,
  collinear, non-finite, deleted, fallback, and invalid-topology counts.
- Rename/migrate `Geometry.NormalEstimation` to `Geometry.PointCloud.Normals`
  rather than keeping a compatibility shim. The new module defaults to KDTree
  KNN neighborhood search, supports caller-supplied `KDTree` and `Octree`
  overloads, preserves a span-based estimator for existing reconstruction
  callers, and exposes cloud/raw-property overloads that write canonical
  `v:normal`.
- Migrate `Geometry.SurfaceReconstruction`, geometry tests, and docs from
  `Geometry.NormalEstimation` to `Geometry.PointCloud.Normals`; update CMake,
  umbrella exports, and generated module inventory.
- Verify with focused geometry tests, CTest geometry target selection, layering,
  task policy, test layout, module inventory check, and docs links before
  retirement and commit.

## Required changes
- [x] Add `src/geometry/Geometry.HalfedgeMesh.Vertices.Normals.cppm` and `.cpp` with typed mesh options, result/status/diagnostics, and pure compute APIs over `Geometry::HalfedgeMesh::Mesh`.
- [x] Add `src/geometry/Geometry.Graph.Vertex.Normals.cppm` and `.cpp` with typed graph options, result/status/diagnostics, and pure compute APIs over `Geometry::Graph::Graph`.
- [x] Rename/migrate `src/geometry/Geometry.NormalEstimation.cppm` and `.cpp` to `src/geometry/Geometry.PointCloud.Normals.cppm` and `.cpp` with typed point-cloud options, result/status/diagnostics, and APIs over `Geometry::PointCloud::Cloud`, `Vertices` property sets, and supplied spatial indexes where appropriate.
- [x] Make every public recompute entry point write/get-or-add the normal property and return the respective property handle: `VertexProperty<glm::vec3>` for mesh/graph/cloud container overloads and `Property<glm::vec3>` for raw `PropertySet` overloads. Diagnostics may wrap the property in a result struct, but the property handle is the authoritative output.
- [x] For property-set overloads, take the necessary mutable target property set (for example `Vertices&`) plus the read-only topology/position inputs and explicit property names or descriptors needed to locate positions and write normals.
- [x] Implement point-cloud normal recomputation through `Geometry.KDTree` KNN/radius neighborhood queries by default, including minimum-neighbor validation, deterministic tie-breaking, finite-position checks, and orientation/fallback diagnostics.
- [x] Add overloads for point-cloud normal recomputation that can consume a caller-built `Geometry::KDTree` and/or `Geometry::Octree`, so callers that already own an index do not have to rebuild it.
- [x] Implement graph normal recomputation from `Graph::Graph` vertex positions plus edge connectivity, using incident-edge neighborhoods with documented behavior for isolated, degree-one, collinear, duplicate, and non-finite inputs.
- [x] Implement mesh normal recomputation from `HalfedgeMesh::Mesh` positions plus face connectivity/topology with selectable face-normal averaging schemes: uniform face-normal averaging, area-weighted averaging, angle-weighted averaging, and Max-style sine/reciprocal-edge weighting.
- [x] Add the new module interfaces/private implementation units to `src/geometry/CMakeLists.txt` and export the new modules from `src/geometry/Geometry.cppm` without creating import cycles through `Geometry.HalfedgeMesh`, `Geometry.Graph`, or `Geometry.PointCloud`.
- [x] Keep non-trivial covariance, face traversal, neighborhood, diagnostics, and normalization logic in `.cpp` implementation units; keep `.cppm` surfaces declarative.
- [x] Fill normalized finite `glm::vec3` normals count-matched to the input point/vertex/node property set, plus diagnostics for degenerate inputs, zero fallback normals, flipped orientations, and the selected backend/scheme.

## Tests
- [x] Add `tests/unit/geometry/Test.HalfedgeMeshVertexNormals.cpp` for mesh module coverage and wire it into `tests/CMakeLists.txt`.
- [x] Add `tests/unit/geometry/Test.GraphVertexNormals.cpp` for graph module coverage and wire it into `tests/CMakeLists.txt`.
- [x] Add `tests/unit/geometry/Test.PointCloudNormals.cpp` for point-cloud KDTree/spatial-index overload coverage and wire it into `tests/CMakeLists.txt`.
- [x] Rename or migrate `tests/unit/geometry/Test_NormalEstimation.cpp` coverage to `Test.PointCloudNormals.cpp`; keep old-test compatibility only if a documented temporary shim remains.
- [x] Add mesh fixtures that distinguish uniform, area-weighted, angle-weighted, and Max-style face-normal averaging and cover degenerate/zero-area faces.
- [x] Add graph fixtures for planar connected graphs, isolated vertices, degree-one vertices, collinear incident edges, duplicate positions, and invalid edge indices.
- [x] Add point-cloud fixtures proving the KDTree-backed path produces stable normals for planar and curved samples, reports KDTree diagnostics, accepts a caller-supplied spatial index, and fails closed for too-few or non-finite points.

## Docs
- [x] Update geometry architecture/API docs for the `Geometry.NormalEstimation` to `Geometry.PointCloud.Normals` rename and for the property-returning API contract.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.
- [x] Keep this task and [`tasks/backlog/geometry/README.md`](../backlog/geometry/README.md) aligned if the task scope changes.

## Acceptance criteria
- [x] Mesh, graph, and point-cloud recomputation are available from domain-owned geometry CPU modules without importing higher layers.
- [x] The primary public modules are `Geometry.HalfedgeMesh.Vertices.Normals`, `Geometry.Graph.Vertex.Normals`, and `Geometry.PointCloud.Normals`; `Geometry.NormalEstimation` is removed or left only as a documented temporary compatibility shim with a removal task.
- [x] Public recompute calls return the written normal property handle (`VertexProperty<glm::vec3>` / `Property<glm::vec3>`) with diagnostics, rather than returning only a detached vector.
- [x] Point-cloud recomputation uses `Geometry.KDTree` by default for the editor-facing contract and offers explicit supplied-index overloads for KDTree and/or Octree.
- [x] Mesh averaging scheme selection is observable in tests and diagnostics.
- [x] Graph recomputation uses connectivity, fails closed for invalid topology, and reports degeneracy without producing NaN normals.
- [x] Focused geometry tests and layering checks pass.

## Progress notes
- 2026-06-21: Completed the graph and point-cloud slices. Added
  `Geometry.Graph.Vertex.Normals` with graph/container and raw-property
  recompute overloads, PCA incident-neighborhood fitting, deterministic
  fallback normals, and degeneracy/invalid-topology diagnostics.
- 2026-06-21: Renamed `Geometry.NormalEstimation` to
  `Geometry.PointCloud.Normals` without a compatibility shim. The replacement
  keeps span-based estimation for reconstruction callers, adds cloud and
  raw-property recompute overloads that write canonical `v:normal`, uses KDTree
  KNN/radius search by default, and accepts caller-owned KDTree or Octree
  indexes.
- 2026-06-21: Migrated reconstruction callers, CMake/module exports, tests,
  architecture docs, backlog references, and the generated module inventory.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
./build/ci/bin/IntrinsicGeometryTests --gtest_filter='HalfedgeMeshVertexNormals.*:GraphVertexNormals.*:PointCloudNormals.*:KDTree.*:Octree.*'
ctest --test-dir build/ci --output-on-failure -R 'HalfedgeMeshVertexNormals|GraphVertexNormals|PointCloudNormals|KDTree|Octree' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/docs/check_docs_sync.py --root . --files $(git diff --name-only origin/main) $(git ls-files --others --exclude-standard)
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing runtime, ECS, assets, graphics, RHI, platform, app, or method packages into `src/geometry`.
- Claiming rendering or editor behavior; `UI-022` owns runtime/editor integration.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed by this geometry task.
- This task closes the algorithm contract only. `UI-022` owns the runtime/editor command integration; no `Operational` GPU/backend follow-up is owed by this geometry task.
