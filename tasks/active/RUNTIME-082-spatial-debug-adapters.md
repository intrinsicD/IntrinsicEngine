# RUNTIME-082 — `Extrinsic.Runtime.SpatialDebugAdapters` umbrella

## Status

- Status: in-progress (Slice A landing on
  `claude/intrinsicengine-agent-onboarding-k31Vm`; Slices B–D remain).
- Owner/agent: unassigned after Slice A; next pick-up by any agent for
  Slice B (KdTree + Octree adapters).
- Branch: `claude/intrinsicengine-agent-onboarding-k31Vm` for Slice A.
- Started: 2026-05-25. Promoted from
  `tasks/backlog/runtime/RUNTIME-082-spatial-debug-adapters.md` as the
  next earliest unblocked Theme A leaf after GRAPHICS-076, GRAPHICS-077,
  GRAPHICS-078 each parked on their Vulkan-host blockers and RUNTIME-080
  was deferred behind its undeclared dependency on ASSETIO-001's CPU
  texture payload type (clarification recorded inline on the RUNTIME-080
  backlog file). All four named upstream types
  (`Geometry::BVH`/`KDTree`/`Octree`/`ConvexHull`,
  `Extrinsic::Graphics::SpatialDebug{Aabb, HierarchyNode, SplitPlane,
  WireEdge}`) exist in the current tree; the task is a pure CPU/null
  translation seam with no graphics surface change and no RHI/Vulkan
  dependency.
- Next verification step: see `## Next verification step` below.

## Slice plan

The task spans (a) a runtime-local snapshot-batch container shape, (b)
an `ISpatialDebugAdapter` virtual interface, (c) four concrete adapters
(`BvhAdapter`, `KdTreeAdapter`, `OctreeAdapter`, `ConvexHullAdapter`),
(d) a registry surface for selecting an active adapter per renderable,
(e) wiring into `RenderExtractionCache::ExtractAndSubmit` with
adapter-side stats on `RuntimeRenderExtractionStats`. Each slice below
is independently reviewable and preserves the CPU/null correctness
gate.

- **Slice A (this slice).** Scaffold the umbrella module + the
  `SpatialDebugSnapshotBatch` / `SpatialDebugAdapterOptions` /
  `SpatialDebugAdapterStats` value types + the `ISpatialDebugAdapter`
  interface + the `BvhAdapter` concrete adapter (the earliest leaf
  named by the task's `## Next verification step`: "Land the umbrella +
  at least the BVH adapter"). Adds contract tests on a deterministic
  4-AABB fixture covering deterministic node counts, leaf-only
  filtering, and depth-cap truncation. Does **not** introduce
  KdTree/Octree/ConvexHull adapters, does **not** add a registry
  surface, does **not** wire `RenderExtractionCache::ExtractAndSubmit`,
  and does **not** add any fields to `RuntimeRenderExtractionStats` or
  to graphics-side `RenderWorld` (the snapshot pump is Slice D scope).
  No new ECS components in this slice.
- **Slice B.** Add `KdTreeAdapter` + `OctreeAdapter` mirroring the
  Slice A pattern, with parallel contract tests on small fixture
  trees. No registry, no extraction wiring.
- **Slice C.** Add `ConvexHullAdapter` (translates a
  `Geometry::ConvexHull` into `Graphics::SpatialDebugWireEdge` records
  plus the vertex span consumed by
  `BuildSpatialDebugConvexHullWireframe`) plus the runtime registry
  surface for selecting an active adapter per renderable. Adds the
  remaining contract tests on a small fixture hull.
- **Slice D.** Wire `RenderExtractionCache::ExtractAndSubmit` to
  invoke the active adapter for entities that carry the relevant
  geometry-tree binding, accumulate snapshots into a new
  `RuntimeRenderSnapshotBatch::SpatialDebug*` span family, and report
  adapter-side stats in `RuntimeRenderExtractionStats`. This slice
  introduces the renderable↔geometry-tree binding mechanism (ECS
  component or runtime sidecar — design choice pinned at slice start
  via a focused grilling pass on the user, since it crosses the ECS
  boundary). Owns the integration test against
  `Test.RuntimeRenderExtraction`.

## Maturity

- Target: `CPUContracted` after Slice C on every host (all four
  adapter kinds compile and produce deterministic snapshot output
  through CPU-only contract tests). `Operational` is owned by Slice D
  once the extraction wiring lands and the snapshot pump is observable
  through `RuntimeRenderExtractionStats` in a runtime integration
  test.
- Slice A closes `Scaffolded → CPUContracted` for the umbrella + the
  BVH adapter (the seam compiles and the BVH adapter is contract-
  verified). The remaining three adapter kinds + the extraction
  wiring are not present yet, so the parent task itself stays at
  `Scaffolded` until Slice C completes. Per the `Scaffolded` closure
  rule (see `docs/agent/task-maturity.md`), Slices B/C are named here
  and remain in-scope, so the rule is honored.
- Slice B closes the KdTree + Octree adapters at `CPUContracted`.
- Slice C closes the ConvexHull adapter + the registry at
  `CPUContracted` and closes the task at `CPUContracted` if Slice D is
  separately deferred; otherwise the task closes at `Operational` once
  Slice D lands.
- Slice D closes the extraction wiring at `Operational` via a runtime
  integration test (no `gpu`/`vulkan` requirement).

## Goal

- Open the runtime-side adapter umbrella declared by `GRAPHICS-011Q`:
  a new module `Extrinsic.Runtime.SpatialDebugAdapters` (home:
  `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cppm`)
  hosting concrete BVH/KD-tree/octree/convex-hull adapters that
  translate geometry-tree implementations into the data-only bounds,
  hierarchy-node, split-plane, convex-hull edge, and point-marker
  snapshot records consumed by
  `Extrinsic.Graphics.SpatialDebugVisualizers`.

## Non-goals

- No graphics-side ownership of geometry tree implementations.
- No mutation of the existing `SpatialDebugVisualizerOptions` /
  `SpatialDebugVisualizerDiagnostics` graphics-visible budget /
  diagnostics surfaces.
- No editor UI for spatial debug toggles (lives elsewhere, consumes
  adapter pre-filter inputs).
- No new ECS components in Slices A–C (the renderable↔geometry-tree
  binding mechanism is pinned at Slice D start).
- No new `RenderWorld` field in Slices A–C (the
  `RenderWorld::SpatialDebug` span family is introduced by Slice D
  alongside the extraction pump).
- No `Runtime.RenderExtraction.cpp` change in Slice A (the snapshot
  pump and `RuntimeRenderExtractionStats` additions land in Slice D).

## Context

- Status: Slice A in-progress on
  `claude/intrinsicengine-agent-onboarding-k31Vm`.
- Owner/layer: `runtime`. Runtime is the only layer permitted to
  import both `Geometry.{BVH,KDTree,Octree,ConvexHull}` *and*
  `Extrinsic.Graphics.SpatialDebugVisualizers`, per the layering
  contract in [`AGENTS.md`](../../AGENTS.md) §2.
- Planning anchor:
  `tasks/done/GRAPHICS-011Q-spatial-debug-adapter-clarifications.md`
  ("concrete BVH/KD-tree/octree/convex-hull adapters live in **runtime
  extraction** … `Extrinsic.Runtime.SpatialDebugAdapters` — not in
  `src/geometry` and not in `src/graphics` — because runtime is the
  only layer permitted to import both geometry tree implementations
  and the graphics packet types").
- Today: `Geometry.BVH`, `Geometry.KDTree`, `Geometry.Octree`, and
  `Geometry.ConvexHull` all expose CPU-side node enumeration via
  public `Nodes()` accessors or equivalent (BVH:
  `src/geometry/Geometry.BVH.cppm:107`). The graphics-side packet
  types (`SpatialDebugAabb`, `SpatialDebugHierarchyNode`,
  `SpatialDebugSplitPlane`, `SpatialDebugWireEdge`) are exported from
  `src/graphics/renderer/Graphics.SpatialDebugVisualizers.cppm`.
  No runtime-side bridge exists yet; the visualizer builder
  functions (`BuildSpatialDebugHierarchyWireframes` etc.) currently
  have no production caller.
- Adapter pre-filter defaults are recorded inline on the adapter
  options struct (Slice A: `LeafOnly=false`, `OccupancyOnly=false`,
  `MaxDepth=32`, matching the graphics-side
  `SpatialDebugVisualizerOptions::MaxDepth` default).
- Adapter-side statistics surface through a Slice-A-local
  `SpatialDebugAdapterStats` struct; Slice D will fold the
  per-adapter accumulators into `RuntimeRenderExtractionStats` so
  consumers see one combined view.
- Test placement: `tests/contract/runtime/` next to existing
  runtime contract tests (`Test.ProceduralGeometryCache.cpp`,
  `Test.ProceduralGeometryExtraction.cpp`,
  `Test.RuntimeCameraControllers.cpp`). The Slice A umbrella does
  not require `IRenderer`, GLFW, or Vulkan, so the existing
  `RuntimeContractTestObjs` target is the right home rather than
  the headless-gated `IntrinsicRuntimeTests` umbrella.

## Required changes

Slice A (this slice):

- [x] Add `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cppm`
      exporting `Extrinsic.Runtime.SpatialDebugAdapters` with:
  - `struct SpatialDebugSnapshotBatch` — mutable output container
    aggregating `std::vector<Extrinsic::Graphics::SpatialDebugAabb>`
    (`Bounds`), `std::vector<…SpatialDebugHierarchyNode>`
    (`HierarchyNodes`), `std::vector<…SpatialDebugSplitPlane>`
    (`SplitPlanes`), `std::vector<glm::vec3>` (`ConvexHullVertices`),
    `std::vector<…SpatialDebugWireEdge>` (`ConvexHullEdges`), and
    `std::vector<glm::vec3>` (`PointMarkers`). Plus a `Clear()`
    helper that empties every span. Each adapter `Append`s to the
    same batch so multiple adapters can share one output frame.
  - `struct SpatialDebugAdapterOptions` — `bool LeafOnly{false}`,
    `bool OccupancyOnly{false}`, `std::uint32_t MaxDepth{32}`. Same
    shape across all adapter kinds.
  - `struct SpatialDebugAdapterStats` — `std::uint32_t LeafNodeCount`,
    `InnerNodeCount`, `SplitPlaneCount`, `EmptyNodeSkippedCount`,
    `DepthCapTruncationCount`. Accumulator updated per
    `Append(...)` call.
  - `class ISpatialDebugAdapter` — pure virtual `Append(out, options,
    stats) const`. Virtual destructor (defaulted). Adapters hold
    non-owning references to their geometry tree source.
- [x] Add `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cpp`
      with the `BvhAdapter` implementation: stores a non-owning
      `const Geometry::BVH*`; `Append` walks `bvh.Nodes()`, converts
      each node's `Geometry::AABB` into `SpatialDebugAabb` + a
      `SpatialDebugHierarchyNode{Bounds, Depth, IsLeaf}`, and emits a
      `SpatialDebugSplitPlane{Bounds, Axis, Position}` for non-leaf
      nodes. Depth is computed via a stack walk from the root
      (`Nodes()[0]`). Leaf-only filter skips inner nodes. Occupancy-
      only filter skips leaf nodes whose `NumElements == 0`. The
      `MaxDepth` cap truncates traversal at the depth limit and
      increments `DepthCapTruncationCount` once per truncated
      subtree root.
- [x] Add `src/runtime/SpatialDebug/CMakeLists.txt` registering the
      new module/source files under the existing `ExtrinsicRuntime`
      target (via `target_sources`).
- [x] Update `src/runtime/CMakeLists.txt` to `add_subdirectory(SpatialDebug)`
      after the existing `Cameras` subdirectory pattern.
- [x] Update `src/runtime/CMakeLists.txt` PUBLIC link list to add
      `ExtrinsicGeometry` — runtime did not previously link
      `ExtrinsicGeometry` directly (it only consumed it transitively
      via `ExtrinsicGraphics`). The new adapter module imports
      `Geometry.BVH`, so the direct edge must be declared.

Slice B:

- [ ] Add `KdTreeAdapter` and `OctreeAdapter` to the umbrella module
      with parallel contract tests.

Slice C:

- [ ] Add `ConvexHullAdapter` and the `SpatialDebugAdapterRegistry`
      surface (registers adapters by stable key and resolves the
      active adapter for a renderable).

Slice D:

- [ ] Wire `RenderExtractionCache::ExtractAndSubmit` to invoke the
      active adapter per relevant entity, accumulate snapshots into a
      new `RuntimeRenderSnapshotBatch::SpatialDebug*` span family,
      and surface per-frame adapter stats on
      `RuntimeRenderExtractionStats`. Introduces the renderable↔
      geometry-tree binding (ECS component or runtime sidecar — pin
      at slice start).

## Tests

Slice A (this slice):

- [x] `contract;runtime` —
      `SpatialDebugAdapters.BvhAdapterAppendsDeterministicNodesAndPlanes`:
      build a `Geometry::BVH` from four AABBs with `BVHBuildParams{}`
      defaults; call `BvhAdapter::Append` with default options; assert
      `HierarchyNodes.size() == bvh.Nodes().size()`, the root bounds
      match the union AABB, the leaf:inner counts match
      `Geometry::BVH::Nodes()` walk, and
      `SplitPlanes.size() == InnerNodeCount`. Pin a sample node's
      `Depth`/`IsLeaf`/`Axis`/`Position` to its expected value.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.BvhAdapterLeafOnlyFilterDropsInnerNodes`:
      same fixture, `options.LeafOnly = true`; assert
      `HierarchyNodes.size() == LeafNodeCount` (count from a manual
      walk) and `SplitPlanes` is empty.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.BvhAdapterDepthCapTruncatesAndCounts`:
      same fixture, `options.MaxDepth = 0`; assert only the root is
      emitted (`HierarchyNodes.size() == 1`) and
      `stats.DepthCapTruncationCount == 1` (one truncated subtree
      root). With `options.MaxDepth = 1` and a multi-level fixture,
      assert the count matches the expected truncated-subtree-root
      count.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.SnapshotBatchClearResetsAllSpans`:
      populate every span field in a `SpatialDebugSnapshotBatch`,
      call `Clear()`, assert every span is empty.

## Docs

Slice A (this slice):

- [x] Update `src/runtime/README.md` to add a row for the new
      `Extrinsic.Runtime.SpatialDebugAdapters` module under the
      "Public module surface" table, recording the Slice A scope
      (umbrella + BvhAdapter + value types).
- [x] Update `tasks/active/README.md` to add an entry for
      RUNTIME-082 alongside the existing GRAPHICS-076/077/078 rows.
- [x] Update `tasks/backlog/runtime/README.md` to point at the
      active task location (move the entry from the backlog list).
- [x] Regenerate `docs/api/generated/module_inventory.md` after
      adding the module.

## Acceptance criteria

Slice A (this slice):

- [x] `Extrinsic.Runtime.SpatialDebugAdapters` compiles and exports
      the four value types + the interface + the BVH adapter.
- [x] The BVH adapter produces deterministic snapshot counts for
      the fixture tree across three options configurations (default,
      leaf-only, depth-cap=0).
- [x] No new graphics imports beyond the existing
      `runtime → graphics/renderer` edge (consuming
      `Extrinsic.Graphics.SpatialDebugVisualizers` types is
      permitted; producing graphics state is not).
- [x] `SpatialDebugVisualizerOptions` /
      `SpatialDebugVisualizerDiagnostics` surfaces remain unchanged.
- [x] Default CPU/null gate stays green; no `gpu`/`vulkan` test
      additions.

Slices B–D acceptance criteria are recorded inside each slice's row
under `## Required changes` once that slice is in-progress.

## Verification

For each slice, run the focused contract subset before the full gate:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes

- Mixing mechanical file moves with semantic refactors.
- Importing geometry-tree implementations from `src/graphics/*`.
- Adding graphics-visible adapter-specific knowledge to
  `SpatialDebugVisualizerOptions` or
  `SpatialDebugVisualizerDiagnostics`.
- Adding any new ECS component in Slices A–C (binding mechanism is
  Slice D).
- Adding any new field to `RuntimeRenderExtractionStats` in Slices
  A–C (extraction-stat folding is Slice D).
- Wiring the BVH adapter into `RenderExtractionCache::ExtractAndSubmit`
  in Slice A — that flips the umbrella from `Scaffolded` straight to
  `Operational` without the intermediate `CPUContracted` review for
  the remaining three adapter kinds and the registry surface.

## Next verification step

- Slice A: complete the umbrella + BvhAdapter scaffold on
  `claude/intrinsicengine-agent-onboarding-k31Vm`; run
  `cmake --preset ci`,
  `cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicTests`,
  `ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`,
  `python3 tools/repo/check_layering.py --root src --strict`,
  `python3 tools/repo/check_test_layout.py --root . --strict`,
  `python3 tools/docs/check_doc_links.py --root .`,
  `python3 tools/agents/check_task_policy.py --root . --strict`,
  and `python3 tools/repo/generate_module_inventory.py --root src
  --out docs/api/generated/module_inventory.md` before commit.
- Slice B pick-up: add `KdTreeAdapter` + `OctreeAdapter` mirroring
  the Slice A `BvhAdapter` shape; reuse the contract-test fixture
  scaffolding.
- Slice C pick-up: add `ConvexHullAdapter` + registry; close the
  task at `CPUContracted` if Slice D is independently deferred.
- Slice D pick-up: pin the renderable↔geometry-tree binding via a
  focused grilling pass, then wire `ExtractAndSubmit` + integration
  test.
