# RUNTIME-082 — `Extrinsic.Runtime.SpatialDebugAdapters` umbrella

## Status

- Status: done (all four slices landed). Retired to `tasks/done/` on
  2026-05-27 after Slice D landed on the current onboarding branch
  `claude/intrinsicengine-agent-onboarding-xnNIW`
  (`ECS::Components::SpatialDebugBinding` + cache-owned adapters +
  `RuntimeRenderSnapshotBatch::SpatialDebug*` spans + per-frame stats;
  five new integration tests pass under the default CPU/null gate,
  2245/2247 overall — the two pre-existing
  `IntrinsicBenchmarkSmoke.HalfedgeSmoke.{Run,Validate}` "Not Run"
  failures persist unchanged from prior commits and trace to the
  benchmark binary not being built by `IntrinsicTests`).
- Owner/agent: closed.
- Branches: `claude/intrinsicengine-agent-onboarding-k31Vm` (Slice A),
  `claude/intrinsicengine-agent-onboarding-Yrfon` (Slice B), prior
  onboarding branch (Slice C), and
  `claude/intrinsicengine-agent-onboarding-xnNIW` (Slice D).
- Slice A PR: #933. Slice B PR: #934. Slice C commit:
  `2697f86` on the prior onboarding branch
  (`RUNTIME-082 Slice C: add ConvexHullAdapter + adapter registry`).
  Slice D commit: see git log on
  `claude/intrinsicengine-agent-onboarding-xnNIW` for the
  `RUNTIME-082 Slice D` commit that retires this task.
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
  remaining contract tests on a small fixture hull. (Landed
  2026-05-26 on the current onboarding branch; five new contract
  tests bring the suite to 15/15 passing.)
- **Slice D (this slice — landed 2026-05-27).** Wires
  `RenderExtractionCache::ExtractAndSubmit` to invoke the active
  adapter for entities that carry an `ECS::Components::SpatialDebugBinding`,
  accumulate snapshots into a shared `SpatialDebugSnapshotBatch`,
  attach the resulting spans to a new
  `RuntimeRenderSnapshotBatch::SpatialDebug{Bounds,HierarchyNodes,SplitPlanes,ConvexHullVertices,ConvexHullEdges,PointMarkers}`
  span family, and fold per-frame counters onto
  `RuntimeRenderExtractionStats`'s `SpatialDebug{BindingsObserved,
  AdaptersInvoked, MissingAdapterCount, BoundsCount, HierarchyNodeCount,
  SplitPlaneCount, ConvexHullVertexCount, ConvexHullEdgeCount,
  PointMarkerCount, LeafNodeAccumulator, InnerNodeAccumulator,
  EmptyNodeSkippedAccumulator, DepthCapTruncationAccumulator}` fields.
  The renderable↔geometry-tree binding is the new ECS component
  `ECS::Components::SpatialDebugBinding{Kind, RegistryKey, LeafOnly,
  OccupancyOnly, MaxDepth}` (pinned 2026-05-27 via a focused grilling
  pass on the user: option A1 — ECS component carrying typed kind +
  registry key — was selected over a runtime-only sidecar map).
  Adapter ownership is the cache via `std::unique_ptr`
  (`RenderExtractionCache::RegisterSpatialDebugAdapter` /
  `UnregisterSpatialDebugAdapter`; the prior raw-pointer
  `SpatialDebugAdapterRegistry` is now an embedded mirror the cache
  refreshes on every register/unregister). Five new integration tests
  in `tests/integration/runtime/Test.RuntimeRenderExtraction.cpp`
  cover the resolve-and-pump, missing-adapter, unregistration,
  re-registration replace, and per-binding LeafOnly/MaxDepth-cap
  paths against a deterministic 4-AABB BVH and a unit-cube
  ConvexHull fixture.

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
- Slice B (this slice) closes the KdTree + Octree adapters at
  `CPUContracted`: both compile against the existing umbrella,
  emit deterministic snapshot output through the same
  `ISpatialDebugAdapter::Append` shape, and are exercised by
  six new contract tests on the 4-AABB KDTree and 8-AABB Octree
  fixtures. The parent task itself stays at `Scaffolded` until
  Slice C lands the ConvexHull adapter + registry (per the
  `Scaffolded` closure rule, Slice C is still in-scope and
  named).
- Slice C closes the ConvexHull adapter + the registry at
  `CPUContracted` and closes the task at `CPUContracted` because
  Slice D (extraction wiring) is separately deferred behind the
  renderable↔geometry-tree binding pinning pass; the task therefore
  promotes from `Scaffolded` to `CPUContracted` with Slice C.
- Slice D (landed 2026-05-27) closes the extraction wiring at
  `Operational` via five new integration tests in
  `tests/integration/runtime/Test.RuntimeRenderExtraction.cpp` that
  exercise the full
  `RenderExtractionCache::ExtractAndSubmit` pump against a Null
  renderer; no `gpu`/`vulkan` test additions. The parent task
  promotes from `CPUContracted` to `Operational` with Slice D.

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

Slice B (this slice):

- [x] Extend `Runtime.SpatialDebugAdapters.cppm` with two new
      concrete adapter classes:
  - `class KdTreeAdapter final : public ISpatialDebugAdapter` —
    stores a non-owning `const Geometry::KDTree*`; mirrors the
    `BvhAdapter` shape (rvalue temporary constructor deleted).
  - `class OctreeAdapter final : public ISpatialDebugAdapter` —
    stores a non-owning `const Geometry::Octree*`; same rvalue
    rejection rule.
- [x] Extend `Runtime.SpatialDebugAdapters.cpp` with
      `KdTreeAdapter::Append` and `OctreeAdapter::Append`:
  - `KdTreeAdapter`: walks `kdTree.Nodes()` with an iterative DFS
    carrying depth (root at index 0); emits one
    `SpatialDebugSplitPlane{Bounds, Axis, Position}` per inner
    node using `node.SplitAxis` + `node.SplitValue`; honors
    `LeafOnly`, `OccupancyOnly` (`NumElements == 0u`), and
    `MaxDepth` truncation with the same semantics as
    `BvhAdapter`.
  - `OctreeAdapter`: walks `octree.m_Nodes` with an iterative DFS
    carrying depth (root at index 0); resolves child indices
    via `BaseChildIndex + presentOffset` per
    `Geometry::Octree::Node::ChildExists`, pushing children in
    reverse so octant 0 is popped first for deterministic
    ascending-octant DFS order. For each non-leaf node, emits
    *three* perpendicular `SpatialDebugSplitPlane`s — one per
    axis (X, Y, Z) at the parent AABB center. This is exact
    for `SplitPoint::Center` and an explicit approximation for
    `Mean`/`Median`, since `Geometry::Octree::Node` does not
    record the chosen split point. Honors `LeafOnly`,
    `OccupancyOnly` (`NumElements == 0u`), and `MaxDepth`
    truncation with the same semantics as `BvhAdapter`. Each
    truncated subtree root contributes
    `SplitPlaneCount += 3u` (planes were already emitted before
    the depth-cap check fires, matching the `BvhAdapter` ordering).
- [x] No new CMake entries; the new sources are part of the
      existing `Extrinsic.Runtime.SpatialDebugAdapters` module
      already registered by `src/runtime/CMakeLists.txt`.

Slice C:

- [x] Add `ConvexHullAdapter` and the `SpatialDebugAdapterRegistry`
      surface (registers adapters by stable key and resolves the
      active adapter for a renderable). `ConvexHullAdapter` stores a
      non-owning `const Geometry::ConvexHull*` plus an
      `IncidenceEpsilon` (default `1e-4f`); `Append` copies the
      hull's V-Rep into `out.ConvexHullVertices` and derives
      `out.ConvexHullEdges` via plane-incidence (two vertices form a
      `SpatialDebugWireEdge` when they share ≥2 face planes within
      `IncidenceEpsilon`). Edges are emitted deterministically in
      `(i, j)` ascending order with indices offset by the existing
      `ConvexHullVertices.size()` so multi-`Append` batches stay
      coherent. `ConvexHullAdapter` ignores `LeafOnly`/`OccupancyOnly`/
      `MaxDepth` and leaves the `SpatialDebugAdapterStats`
      accumulator untouched because none of the tree-shaped
      concepts apply to a flat hull (the rvalue constructor is
      deleted, matching every other adapter). The new
      `SpatialDebugAdapterRegistry` maps an opaque
      `using Key = std::uint64_t;` renderable key onto a non-owning
      `const ISpatialDebugAdapter*` via
      `Register(key, adapter)` / `Unregister(key)` /
      `Find(key)` / `Contains(key)` / `Size()` / `Empty()` /
      `Clear()`; re-registering the same key overwrites the prior
      entry. Callers own adapter lifetime and must `Unregister`
      before the adapter or its source geometry tree is destroyed.
      No new ECS components and no `RenderExtractionCache`
      wiring — the registry is the data-only seam Slice D will
      consume.

Slice D (landed 2026-05-27):

- [x] Add `src/ecs/Components/ECS.Component.SpatialDebugBinding.cppm`
      exporting `Extrinsic.ECS.Component.SpatialDebugBinding` with
      `enum class SpatialDebugGeometryKind : std::uint8_t {Bvh,
      KdTree, Octree, ConvexHull}` and
      `struct SpatialDebugBinding{Kind, RegistryKey (uint64_t),
      LeafOnly, OccupancyOnly, MaxDepth}`. The component imports
      `<cstdint>` only — no runtime/graphics imports — so the new
      ECS surface respects `ecs -> {core, geometry}` per `AGENTS.md`
      §2. Registered in `src/ecs/Components/CMakeLists.txt`.
- [x] Extend `Extrinsic::Graphics::RuntimeRenderSnapshotBatch`
      (`src/graphics/renderer/Graphics.Renderer.cppm`) with six new
      span fields:
      `SpatialDebugBounds`, `SpatialDebugHierarchyNodes`,
      `SpatialDebugSplitPlanes`, `SpatialDebugConvexHullVertices`,
      `SpatialDebugConvexHullEdges`, `SpatialDebugPointMarkers`.
      The renderer module gains a local (non-export)
      `import Extrinsic.Graphics.SpatialDebugVisualizers` so the
      packet types are visible to the span declarations.
- [x] Extend `Runtime::RuntimeRenderExtractionStats`
      (`src/runtime/Runtime.RenderExtraction.cppm`) with the
      `SpatialDebug{BindingsObserved, AdaptersInvoked,
      MissingAdapterCount, BoundsCount, HierarchyNodeCount,
      SplitPlaneCount, ConvexHullVertexCount, ConvexHullEdgeCount,
      PointMarkerCount, LeafNodeAccumulator, InnerNodeAccumulator,
      EmptyNodeSkippedAccumulator, DepthCapTruncationAccumulator}`
      fields. Per-batch counts come from the submitted batch span
      sizes; accumulator fields are summed from
      `SpatialDebugAdapterStats` across every invoked adapter.
- [x] Extend `RenderExtractionCache` with the adapter-ownership
      surface: `RegisterSpatialDebugAdapter(uint64_t,
      std::unique_ptr<ISpatialDebugAdapter>)`,
      `UnregisterSpatialDebugAdapter(uint64_t) noexcept`,
      `GetSpatialDebugAdapterCount()`, and
      `GetSpatialDebugRegistryForTest()`. The cache holds an owning
      `std::unordered_map<uint64_t, std::unique_ptr<ISpatialDebugAdapter>>`
      plus an embedded `SpatialDebugAdapterRegistry` mirror that is
      refreshed on every register (the registry's raw pointer is
      replaced atomically: erase-then-insert ordering guarantees no
      dangling pointer between the unique_ptr swap and the registry
      re-register on collision).
- [x] Wire `RenderExtractionCache::ExtractAndSubmit` to walk the
      `ECS::Components::SpatialDebugBinding` view independently of
      `HasRenderableHint` (a binding may attach to a renderable
      entity or to a debug-only entity), resolve the adapter through
      the cache-owned registry, call `Append` against the
      per-frame-cleared `m_SpatialDebugBatch`, fold per-adapter
      stats, count missing-adapter cases, fill the batch-derived
      stat counts, and attach the batch spans to
      `RuntimeRenderSnapshotBatch::SpatialDebug*` on the
      `SubmitRuntimeSnapshots` call.
- [x] Extend `RenderExtractionCache::Shutdown` to clear the registry
      mirror before destroying the owned-adapter map (so the
      registry never observes a dangling raw pointer mid-shutdown)
      and to `Clear()` the snapshot batch.
- [x] **Slice D follow-up (same PR #936)**: route the submitted
      `RuntimeRenderSnapshotBatch::SpatialDebug*` spans through the
      existing `Extrinsic.Graphics.SpatialDebugVisualizers` builders
      inside `Graphics::Renderer::SubmitRuntimeSnapshots` and merge
      the produced `Debug{Line,Point,Triangle}Packet` records into
      the renderer's debug-primitive collections so they reach
      `RenderWorld::DebugPrimitives` via `ExtractRenderWorld`. The
      original Slice D landing wired the pump and the stats but the
      renderer dropped the spans — counters were nonzero but no
      visible debug geometry reached the canonical debug-primitive
      pass. The follow-up: invoke `BuildSpatialDebugHierarchyWireframes`
      when `SpatialDebugHierarchyNodes` is non-empty (else
      `BuildSpatialDebugBoundsWireframes` over `SpatialDebugBounds`
      as a fallback — tree adapters populate both 1:1, preferring
      hierarchy avoids double-rendering; bare-AABB adapters still
      get a flat-color wireframe via the fallback),
      `BuildSpatialDebugSplitPlaneWireframes` for split planes,
      `BuildSpatialDebugConvexHullWireframe` when both
      `ConvexHullVertices` + `ConvexHullEdges` are non-empty, and
      `BuildSpatialDebugPointMarkers` for the marker span. Each
      produced packet goes through the same `IsValidDebug{Line,Point,
      Triangle}` + clamp loop the explicit `DebugLines/Points/Triangles`
      spans already use.

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

Slice B (this slice):

- [x] `contract;runtime` —
      `SpatialDebugAdapters.KdTreeAdapterAppendsDeterministicNodesAndPlanes`:
      mirrors the BVH-default test on a 4-AABB KDTree fixture
      with `KDTreeBuildParams{.LeafSize = 1u}`; asserts
      `HierarchyNodes.size() == kdTree.Nodes().size()`,
      `Bounds.size() == total`, `SplitPlanes.size() == InnerNodeCount`,
      and pins the root split plane's `(Axis, Position)` to
      `kdTree.Nodes()[0].SplitAxis`/`SplitValue`.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.KdTreeAdapterLeafOnlyFilterDropsInnerNodes`:
      same fixture, `options.LeafOnly = true`; asserts only leaf
      hierarchy nodes are emitted and `SplitPlanes` is empty.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.KdTreeAdapterDepthCapTruncatesAndCounts`:
      same fixture, `options.MaxDepth = 0u`; asserts only the
      root is emitted (`HierarchyNodes.size() == 1`,
      `SplitPlanes.size() == 1`) and
      `stats.DepthCapTruncationCount == 1u`.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.OctreeAdapterAppendsDeterministicNodesAndPlanes`:
      8-AABB Octree fixture (one box per octant) built with
      `SplitPolicy{.SplitPoint = Center, .TightChildren = false}`
      at `maxPerNode = 1u`, `maxDepth = 8u`; asserts
      `HierarchyNodes.size() == octree.m_Nodes.size()`,
      `SplitPlanes.size() == InnerNodeCount * 3u`,
      `stats.SplitPlaneCount == InnerNodeCount * 3u`, and pins
      the root's three split planes' `(Axis, Position)` to the
      root AABB center.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.OctreeAdapterLeafOnlyFilterDropsInnerNodes`:
      same fixture, `options.LeafOnly = true`; asserts only leaf
      hierarchy nodes are emitted and `SplitPlanes` is empty.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.OctreeAdapterDepthCapTruncatesAndCounts`:
      same fixture, `options.MaxDepth = 0u`; asserts only the
      root is emitted (`HierarchyNodes.size() == 1`,
      `SplitPlanes.size() == 3u`) and
      `stats.DepthCapTruncationCount == 1u`.
- [x] Compile-time `static_assert` block extending the Slice A
      lvalue/rvalue construction contract to `KdTreeAdapter` and
      `OctreeAdapter` (rvalue constructors deleted; const-lvalue
      construction preserved).

Slice C (this slice):

- [x] `contract;runtime` —
      `SpatialDebugAdapters.ConvexHullAdapterEmitsVerticesAndDerivedEdges`:
      unit-cube fixture (8 vertices, 6 axis-aligned face planes);
      asserts all 8 hull vertices are copied verbatim into
      `batch.ConvexHullVertices`, exactly 12 edges are derived, and
      every emitted edge connects vertex indices that differ in
      exactly one cube-octant bit (the canonical cube-edge invariant).
      Pins `SpatialDebugAdapterStats` accumulator fields to `0u`
      and the tree-shaped batch spans (`Bounds`, `HierarchyNodes`,
      `SplitPlanes`, `PointMarkers`) to empty.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.ConvexHullAdapterRemapsIndicesAcrossMultiAppend`:
      same fixture, two consecutive `Append` calls; asserts the
      second batch of edges references indices ∈ `[firstVertexCount,
      2*firstVertexCount)` so multi-`Append` batches stay coherent.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.ConvexHullAdapterHandlesEmptyHull`:
      default-constructed `Geometry::ConvexHull` (no vertices, no
      planes); asserts the adapter writes nothing.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.RegistryRegistersFindsAndUnregistersAdapters`:
      exercises `Register` / `Unregister` / `Find` / `Contains` /
      `Size` / `Empty` / `Clear` on a registry holding BVH/KdTree/
      ConvexHull adapters keyed by `std::uint64_t`; verifies missing-
      key lookups return `nullptr`, re-registering an existing key
      overwrites without growing the table, and `Unregister` of a
      missing key returns `false`.
- [x] `contract;runtime` —
      `SpatialDebugAdapters.RegistryResolvedAdapterRoundTripsThroughISpatialDebugAdapter`:
      resolves a registered `ConvexHullAdapter` through the
      pure-virtual `ISpatialDebugAdapter*` returned by `Find` and
      invokes `Append` on it; asserts the round-trip produces the
      same 8-vertex / 12-edge output as direct invocation.
- [x] Compile-time `static_assert` block extending the
      lvalue/rvalue construction contract to `ConvexHullAdapter`
      (rvalue constructors deleted; const-lvalue construction
      preserved).

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

Slice B (this slice):

- [x] Update the `Extrinsic.Runtime.SpatialDebugAdapters` row in
      `src/runtime/README.md` to record the new
      `KdTreeAdapter` / `OctreeAdapter` concretes, including the
      OctreeAdapter's three-plane-per-inner-node visualization
      choice and the `Center`-policy exactness note.
- [x] Update the `RUNTIME-082` entry in
      `tasks/active/README.md` so the slice plan reflects Slice A
      landed and Slice B in-progress.
- [x] Regenerate `docs/api/generated/module_inventory.md` —
      no new module surface but the slice touches the file
      anyway for consistency with Slice A's regenerate command.

Slice C (this slice):

- [x] Update the `Extrinsic.Runtime.SpatialDebugAdapters` row in
      `src/runtime/README.md` to record `ConvexHullAdapter`,
      its plane-incidence edge-derivation strategy, and the
      new `SpatialDebugAdapterRegistry` key→adapter table.
- [x] Update the `RUNTIME-082` entry in
      `tasks/active/README.md` to reflect Slice C landed and
      Slice D the only remaining slice.
- [x] Regenerate `docs/api/generated/module_inventory.md` —
      no new module surface (the registry + ConvexHullAdapter
      land inside the existing `Extrinsic.Runtime.SpatialDebugAdapters`
      module) but the slice touches the file anyway for
      consistency with Slices A/B.

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

Slice B (this slice):

- [x] `Extrinsic.Runtime.SpatialDebugAdapters` exports both new
      adapter classes alongside the existing `BvhAdapter`.
- [x] `KdTreeAdapter` produces deterministic snapshot counts for
      the 4-AABB KDTree fixture across default, leaf-only, and
      depth-cap=0 options configurations; the root split-plane
      pin matches the KDTree's own `(SplitAxis, SplitValue)`.
- [x] `OctreeAdapter` produces deterministic snapshot counts for
      the 8-AABB Octree fixture across default, leaf-only, and
      depth-cap=0 options configurations; `SplitPlaneCount` is
      exactly `InnerNodeCount * 3u` in the default case and the
      three root planes pin to the root AABB center.
- [x] No new graphics imports beyond the existing
      `runtime → graphics/renderer` edge; no `Geometry::Octree`
      / `Geometry::KDTree` references leak past
      `Runtime.SpatialDebugAdapters` (verified by
      `python3 tools/repo/check_layering.py --root src --strict`).
- [x] Default CPU/null gate stays green; no `gpu`/`vulkan` test
      additions.

Slices C–D acceptance criteria are recorded inside each slice's row
under `## Required changes` once that slice is in-progress.

Slice D (landed 2026-05-27):

- [x] `Extrinsic.ECS.Component.SpatialDebugBinding` compiles and
      exports the `SpatialDebugGeometryKind` discriminator + the
      `SpatialDebugBinding` POD; no runtime/graphics imports
      (verified by `python3 tools/repo/check_layering.py --root src
      --strict`).
- [x] `Extrinsic::Graphics::RuntimeRenderSnapshotBatch` exports the
      six new `SpatialDebug*` span fields; backends and tests that
      do not exercise the pump keep their default-empty span
      behavior unchanged.
- [x] `Extrinsic::Runtime::RuntimeRenderExtractionStats` exports the
      thirteen new spatial-debug counters; existing
      callers continue to compile because the additions are pure
      additions with default initializers.
- [x] `RenderExtractionCache` exposes the four new adapter-ownership
      accessors and clears adapter ownership + the registry mirror
      in `Shutdown`.
- [x] The five original `RuntimeRenderExtraction.SpatialDebug*`
      counter tests plus the three follow-up regression tests
      (`SpatialDebugBindingProducesVisibleDebugPrimitivesInRenderWorld`,
      `SpatialDebugMissingAdapterProducesNoDebugPrimitives`, and
      `SpatialDebugConvexHullBindingProducesEdgeLines` — each asserts
      `world.DebugPrimitives.LineCount` after
      `Renderer->ExtractRenderWorld({})`) pass under the default
      CPU/null gate (`./build/ci/bin/IntrinsicRuntimeGraphicsCpuTests
      --gtest_filter='*SpatialDebug*'` → 8/8 pass).
- [x] Default CPU/null gate stays green (2248/2250 with the two
      pre-existing `IntrinsicBenchmarkSmoke.HalfedgeSmoke.{Run,
      Validate}` "Not Run" failures unchanged from prior commits and
      unrelated to this slice); no `gpu`/`vulkan` test additions.
- [x] `docs/api/generated/module_inventory.md` regenerated; total
      module count rises from 445 → 446 because of the new ECS
      component module.

Slice C (this slice):

- [x] `Extrinsic.Runtime.SpatialDebugAdapters` exports
      `ConvexHullAdapter` and `SpatialDebugAdapterRegistry` alongside
      the existing `BvhAdapter` / `KdTreeAdapter` / `OctreeAdapter`
      concretes and the umbrella value types.
- [x] `ConvexHullAdapter` copies the hull's V-Rep into
      `ConvexHullVertices` and derives `ConvexHullEdges` by
      plane-incidence; for a unit-cube fixture it emits the expected
      eight vertices and twelve edges with deterministic
      `(i, j)`-ascending ordering.
- [x] `ConvexHullAdapter` remaps derived edge indices by
      `ConvexHullVertices.size()` so multi-`Append` batches stay
      coherent (verified by the multi-Append regression test).
- [x] `ConvexHullAdapter` handles an empty `Geometry::ConvexHull`
      without writing anything to the batch.
- [x] `SpatialDebugAdapterRegistry` exposes
      `Register`/`Unregister`/`Find`/`Contains`/`Size`/`Empty`/`Clear`;
      re-registering an existing key overwrites the prior entry
      without growing the table; the resolved
      `const ISpatialDebugAdapter*` round-trips through the
      pure-virtual `Append` interface.
- [x] No new graphics imports beyond the existing
      `runtime → graphics/renderer` edge; no `Geometry::ConvexHull`
      reference leaks past `Runtime.SpatialDebugAdapters` (verified by
      `python3 tools/repo/check_layering.py --root src --strict`).
- [x] Default CPU/null gate stays green (2286/2286 tests pass on
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`);
      no `gpu`/`vulkan` test additions.

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

- Slice A: completed 2026-05-25 via PR #933 on
  `claude/intrinsicengine-agent-onboarding-k31Vm`.
- Slice B: completed 2026-05-26 via PR #934 on
  `claude/intrinsicengine-agent-onboarding-Yrfon`.
- Slice C: completed 2026-05-26 on the prior onboarding branch
  (`ConvexHullAdapter` + `SpatialDebugAdapterRegistry`, five new
  contract tests, 2286/2286 default CPU gate pass with
  `CCACHE_DISABLE=1` — see Notes below).
- Slice D: completed 2026-05-27 on
  `claude/intrinsicengine-agent-onboarding-xnNIW` (PR #936)
  (`ECS::Components::SpatialDebugBinding` + cache-owned adapters
  + `RuntimeRenderSnapshotBatch::SpatialDebug*` spans + per-frame
  stats + five integration tests). Default CPU/null gate:
  `cmake --preset ci`, `cmake --build --preset ci --target
  IntrinsicTests`, `ctest --test-dir build/ci --output-on-failure
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` → 2245/2247
  pass (two pre-existing `IntrinsicBenchmarkSmoke.HalfedgeSmoke`
  failures unchanged). Layering / test-layout / doc-link /
  task-policy validators all clean.
- Slice D follow-up (same PR #936): renderer-side routing of
  `RuntimeRenderSnapshotBatch::SpatialDebug*` spans through the
  existing wireframe builders inside
  `Graphics::Renderer::SubmitRuntimeSnapshots`, plus three new
  regression tests that observe `world.DebugPrimitives.LineCount`
  after `Renderer->ExtractRenderWorld({})`. Without this follow-up
  the pump shipped counters but no visible geometry — a "documented
  but not tested" failure that the original Slice D commit missed.
  Default CPU/null gate after the follow-up: 2248/2250 pass (same
  two pre-existing benchmark-binary-path failures unchanged); 8/8
  `*SpatialDebug*` integration tests pass.
- Task retired to `tasks/done/`.

## Notes

- During Slice C verification a stale `ccache` cache surfaced as a
  link-time `undefined reference to ICommandContext::CopyTextureToBuffer`
  on `Backends.Null.cpp.o`: the cached object referenced the pre-
  GRAPHICS-074-Slice-D.2 6-argument signature while the rebuilt BMI
  exposes the 10-argument one. Rebuilding with `CCACHE_DISABLE=1`
  resolves it (Backends.Null's vtable then mangles to the current
  `mjjjj` signature). This is unrelated to RUNTIME-082 scope and is
  preserved here as a non-blocking diagnostic note for the next
  agent — a separate task should harden the ccache + clang-modules
  invalidation policy.

