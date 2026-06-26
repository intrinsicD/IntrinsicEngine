---
id: GEOM-047
theme: none
depends_on: []
maturity_target: CPUContracted
---

# GEOM-047 — Graph and point-cloud query / sampling utilities

## Goal

- Add the graph-edge spatial queries and the Gaussian-noise augmentation utilities the engine currently lacks, plus cached graph edge lengths.
- Extend `Geometry.Graph.Utils` with nearest-edge queries against a point (closest-edge, k-closest edges, radius set, and closest-edge-within-1-ring), built on `Geometry.Queries::ClosestPointSegment` and an existing spatial index (KD-tree / BVH / Octree).
- Add a cached bulk graph edge-length property `e:length` (fill / cache) to `Geometry.Graph.Utils`.
- Add deterministic, seeded **true Gaussian** vertex/point displacement to both `Geometry.Graph.Utils` and `Geometry.PointCloud.Utils`, scaled to the AABB diagonal (graph) and the mean spacing (cloud).

## Non-goals

- No new graph algorithms (shortest path, traversal, layouts already exist in `Geometry.Graph.ShortestPath` and `Geometry.Graph.Utils`).
- No GPU backend and no compute-shader path for any of the new utilities.
- No UI, visualization, or editor wiring.
- No new point-cloud descriptors, registration, or normal-estimation work (owned by other GEOM tasks).
- No changes to the existing KNN-graph construction or layout entry points beyond additive surface.

## Context

- Building blocks already exist but are not applied to graph edges: `Geometry.Queries::ClosestPointSegment` (`src/geometry/Geometry.Queries.cppm:34`, returns `PointSegmentResult`), the `Geometry.Segment` primitive, and three spatial indices (`Geometry.KDTree`, `Geometry.BVH`, `Geometry.Octree`).
- `Geometry.Graph.Utils` (`src/geometry/Geometry.Graph.Utils.cppm`) today exposes only KNN-graph construction (`KNNBuildParams`/`KNNBuildResult`), KNN-from-indices, and layout helpers (force-directed, spectral, hierarchical, edge-crossing). It has no edge spatial queries and no cached edge-length property.
- `Geometry.Graph` (`src/geometry/Geometry.Graph.cppm`) has no `e:length` property; edge lengths are recomputed ad hoc by callers.
- `Geometry.PointCloud.Utils` (`src/geometry/Geometry.PointCloud.Utils.cppm`) already computes spacing statistics (`AverageSpacing`/`MinSpacing`/`MaxSpacing`), so the cloud-noise scale has a ready source, but no Gaussian augmentation exists on either domain.
- The AABB diagonal for a graph's vertex positions is available via `Geometry.AABB`.
- The legacy reference (`bcg_graph_gaussian_noise`, `bcg_point_cloud_gaussian_noise`) is a uniform-noise misnomer; this task implements a TRUE Gaussian (per-component normal displacement), seeded and deterministic.
- All new code lives in the geometry layer and must respect layering: geometry -> core only; no assets/runtime/graphics/rhi/ecs/app dependencies.

## Slice plan

- [ ] Slice 1 — Cached graph edge length: add `e:length` fill/cache helper in `Geometry.Graph.Utils` (`EnsureEdgeLengths` / `FillEdgeLengths`), fail-closed on empty graph / zero-length edges per policy.
- [ ] Slice 2 — Graph nearest-edge queries: closest-edge, k-closest edges, radius set, and closest-edge-within-1-ring, built on `ClosestPointSegment` + an existing spatial index.
- [ ] Slice 3 — Gaussian augmentation: true seeded Gaussian displacement for graphs (scaled to AABB diagonal) and point clouds (scaled to mean spacing).

## Required changes

- [ ] In `src/geometry/Geometry.Graph.Utils.cppm`, declare the edge-length cache surface: an `EdgeLengthParams` (or equivalent) struct and `[[nodiscard]] ... EnsureEdgeLengths(Graph&, ...)` plus a non-caching `FillEdgeLengths(const Graph&, std::span<float>)` returning a status/diagnostic. Keep exported declarations thin (decls + small inline only).
- [ ] In `src/geometry/Geometry.Graph.Utils.cpp`, implement `e:length` fill/cache: iterate edges, compute each length from endpoint `v:position`, write into the `e:length` edge property; fail closed with an explicit diagnostic on empty graph, missing positions, zero-length edges, or non-finite coordinates (no asserts, no NaNs).
- [ ] In `src/geometry/Geometry.Graph.Utils.cppm`, declare nearest-edge query types and entry points: `ClosestEdgeQueryResult` (edge handle, closest point, squared distance, parametric `t`), and `ClosestEdge`, `KClosestEdges`, `EdgesWithinRadius`, and `ClosestEdgeWithinOneRing` (the last taking a seed vertex and searching only edges incident to its 1-ring).
- [ ] In `src/geometry/Geometry.Graph.Utils.cpp`, implement the queries on top of `Geometry.Queries::ClosestPointSegment` and an existing spatial index (KD-tree / BVH / Octree) built over edge segments / AABBs; ensure deterministic tie-breaking by ascending edge handle; fail closed on empty graph, non-finite query point, or `k == 0` / negative radius.
- [ ] In `src/geometry/Geometry.Graph.Utils.cppm`, declare graph Gaussian noise: `GraphGaussianNoiseParams { float StdDevFraction; std::uint64_t Seed; }` and `[[nodiscard]] ... ApplyGaussianNoise(Graph&, const GraphGaussianNoiseParams&)` displacing each `v:position` by a per-component normal sample scaled to the vertex AABB diagonal.
- [ ] In `src/geometry/Geometry.Graph.Utils.cpp`, implement the graph Gaussian path: compute the AABB diagonal via `Geometry.AABB`, draw per-vertex per-component samples from a deterministic seeded normal distribution, scale by `StdDevFraction * diagonal`; `StdDevFraction == 0` is identity; fail closed on empty graph / single degenerate vertex with zero-extent AABB / non-finite positions.
- [ ] In `src/geometry/Geometry.PointCloud.Utils.cppm`, declare point-cloud Gaussian noise: `PointCloudGaussianNoiseParams { float StdDevFraction; std::uint64_t Seed; }` and `[[nodiscard]] ... ApplyGaussianNoise(PointCloud&, const PointCloudGaussianNoiseParams&)` scaled to the mean spacing.
- [ ] In `src/geometry/Geometry.PointCloud.Utils.cpp`, implement the cloud Gaussian path reusing the existing spacing statistics (`AverageSpacing`) as the scale source; deterministic seeded per-point per-component normal samples; `StdDevFraction == 0` is identity; fail closed on empty cloud / single point with undefined spacing / non-finite positions.
- [ ] Ensure determinism of the Gaussian generator does not depend on iteration parallelism: key the RNG per element (e.g. seed mixed with element index) so results are stable regardless of evaluation order.
- [ ] Register any new headers/property names and keep the module export list and `Geometry.cppm` aggregate consistent; run the module inventory generator.

## Tests

- [ ] Add `tests/geometry/` coverage (label `unit;geometry`) for `ClosestEdge`: for random points and a random graph, the returned edge and closest point match a brute-force scan over all edge segments using `ClosestPointSegment`.
- [ ] `KClosestEdges` ordering: returned edges are sorted by ascending squared distance with deterministic handle tie-breaking, and the set equals the brute-force top-k.
- [ ] `EdgesWithinRadius`: the returned set exactly equals the brute-force set of edges whose segment-distance to the query point is `<= radius` (boundary inclusive), with no duplicates.
- [ ] `ClosestEdgeWithinOneRing`: result is restricted to edges incident to the seed vertex's 1-ring and matches a brute-force scan over that restricted edge set.
- [ ] Cached `e:length`: after `EnsureEdgeLengths`, every `e:length` value equals the directly computed endpoint distance within tolerance; recomputing is idempotent.
- [ ] Graph Gaussian noise determinism: two runs with the same seed and `StdDevFraction` produce bit-identical displacements; different seeds differ.
- [ ] Graph Gaussian noise statistics: over many vertices, the empirical per-component standard deviation is approximately `StdDevFraction * AABBdiagonal` within a stated tolerance.
- [ ] Identity: `StdDevFraction == 0` leaves all positions unchanged for both graph and cloud.
- [ ] Point-cloud Gaussian noise: deterministic for a fixed seed and empirical std approximately `StdDevFraction * AverageSpacing`.
- [ ] Degenerate fail-closed: empty graph, empty cloud, single vertex / single point, and zero-length edges each return an explicit diagnostic status (not a crash, not a NaN, not an assert) across all new entry points.

## Docs

- [ ] Update `src/geometry/README.md` (or the geometry module index) to list the new `Geometry.Graph.Utils` edge-query / edge-length / noise surface and the `Geometry.PointCloud.Utils` noise surface.
- [ ] Regenerate `docs/api/generated/module_inventory.md` via `tools/repo/generate_module_inventory.py`.
- [ ] Document the `e:length` edge property name and lifetime in the property-name reference consistent with GEOM-027.
- [ ] Note in the relevant geometry doc that the Gaussian utilities are a corrected (true-Gaussian) replacement for the legacy uniform-noise misnomer, with the AABB-diagonal (graph) and mean-spacing (cloud) scaling conventions.

## Acceptance criteria

- [ ] `ClosestEdge`, `KClosestEdges`, `EdgesWithinRadius`, and `ClosestEdgeWithinOneRing` exist in `Geometry.Graph.Utils`, are `[[nodiscard]]`, and match brute-force references in tests.
- [ ] `e:length` is filled/cached by an exported helper and matches direct endpoint distances; the operation is idempotent.
- [ ] Graph and point-cloud `ApplyGaussianNoise` produce deterministic output for a fixed seed, empirical std within the stated tolerance of the requested scale, and exact identity at `StdDevFraction == 0`.
- [ ] All new entry points fail closed on empty / single-element / zero-length-edge / non-finite input with explicit diagnostics and no NaNs or asserts.
- [ ] No new CTest labels are introduced; new tests use existing `unit;geometry` labels.
- [ ] `tools/repo/check_layering.py --root src --strict` passes; the geometry layer gains no assets/runtime/graphics/rhi/ecs/app dependency.
- [ ] The full Verification block passes.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Graph|PointCloud|Queries' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work (new graph algorithms, point-cloud descriptors, registration, normals).
- Introducing renderer/runtime/ECS/assets/platform/app dependencies into the geometry layer.
- Adding a GPU/compute backend or UI wiring for any of the new utilities.
- Claiming performance improvements without a baseline comparison.
- Reintroducing or preserving the legacy uniform-noise behavior under the "Gaussian" name.

## Maturity

- Stops at **CPUContracted**: deterministic, fail-closed CPU implementations of the graph edge queries, cached `e:length`, and true Gaussian augmentation for graphs and point clouds, with property-tested correctness against brute-force references and explicit diagnostics on degenerate input. No GPU backend, no parity proof against an external implementation, and no integration beyond the geometry library are in scope.
