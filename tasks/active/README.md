# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-013`](BUG-013-backbuffer-readback-contract-vtable-segv.md) —
  Default-recipe + minimal-debug backbuffer readback contract tests SEGV
  under clang-20 modules. Status: in-progress. Owner: local agent.
  Branch: local `main` (committed `7e00f5f9` filed the bug; fix in
  progress on the same branch). Promoted from `tasks/backlog/bugs/` on
  2026-05-28 because the regression blocks `GRAPHICS-076E` CPU contract
  closure and `GRAPHICS-081` scaffold retirement, and "reproducible
  regressions trump feature work" per the agent prompt. Next
  verification step: `cmake --build --preset ci --target
  IntrinsicGraphicsContractCpuTests && build/ci/bin/IntrinsicGraphicsContractCpuTests
  --gtest_filter='*BackbufferReadbackContract*'` must report 8/8 passing.

- [`GEOM-012`](GEOM-012-symmetric-domain-views-property-sharing.md) —
  Symmetric mesh, graph, and point-cloud domain views. Status:
  in-progress (Slices A + B landed). Owner: unassigned. Branch:
  Slice A on `claude/funny-pascal-kTHxz`; Slice B on
  `claude/eloquent-sagan-WhPAe`. Promoted from
  `tasks/backlog/geometry/` on 2026-05-28 as the next unblocked
  geometry task once GEOM-008 (Geometry.Linalg / Geometry.Sparse
  foundation) retired. Slice A added the new `Geometry.DomainViews`
  module with
  `Geometry::DomainViews::BorrowMeshAsGraphReadOnly(const HalfedgeMesh::Mesh&)`
  — a public factory that returns a `Graph::Graph` sharing the source
  mesh's vertex/halfedge/edge `PropertySet`s, the deleted-vertex/edge
  counters, and the canonical `v:point`/`v:connectivity`/`h:connectivity`/
  `v:deleted`/`e:deleted` slots with no compatibility-copy allocations.
  Face storage is deliberately excluded; topology mutation through the
  returned graph is UB on face-bearing meshes (graph methods cannot
  update face incidence) and the const-reference parameter is the
  safety signal. Slice B adds
  `Geometry::DomainViews::BorrowMeshAsCloud(HalfedgeMesh::Mesh&) ->
  PointCloud::Cloud`, which routes through a new
  `PointCloud::Cloud(PropertySet&)` constructor that shares the
  source mesh's vertex `PropertySet` while owning the cloud's own
  deletion counter; cloud-side deletes mark `p:deleted` on the
  shared `PropertySet` but do not increment
  `mesh.DeletedVertexCount()`, so the mesh's `VertexCount()` /
  `HasGarbage()` continue to reflect only mesh-side `v:deleted`
  semantics. The canonical `v:point` slot is reused with no
  `p:position` compatibility copy, and existing per-vertex
  attributes (e.g. `v:normal`) stay reachable through
  `Cloud::GetVertexProperty<T>`. `Cloud::AddPoint` through the view
  appends to the shared vertex `PropertySet` and is safe on
  face-bearing meshes because the new vertex is isolated (no
  incident halfedges); topology-aware deletion must still route
  through `Mesh::DeleteVertex` / `Mesh::GarbageCollection`, and
  `Cloud::GarbageCollection` on a mesh-backed borrow is documented
  UB on face-bearing meshes (it physically reshuffles vertex slots).
  Slice B also fixes the pre-existing `Cloud::CreateView` to clamp
  and bind against the cloud's bound `m_Vertices` /
  `m_DeletedVertices` rather than the owning `Properties`, so
  subviews of a mesh-backed borrow see the mesh's rows rather than
  clamping to size 0 (no-op rebinding for owned clouds).
  Eleven new tests in `Test.SubmeshViewDomainBorrows.cpp` cover
  `v:point` slot identity, absence of `p:position`, `v:normal`
  reuse, bidirectional position-edit visibility, point-addition
  propagation with face state untouched, the empty-mesh case,
  `Cloud::CreateView` over a mesh-borrowed cloud, the empty-mesh
  `CreateView` clamp, cloud-side `DeletePoint` not touching the
  mesh's deletion counter, and a follow-up mesh `GarbageCollection`
  after a cloud-side delete being a consistent no-op. Slice C
  (graph-backed point-cloud) follows the same factory pattern;
  Slice D introduces distinct const-view types; Slice E reviews
  the conversion/move/consume policy and closes at
  `CPUContracted`. Next verification step:
  `ctest --test-dir build/ci --output-on-failure -R 'SubmeshViewDomainBorrows|ShortestPath|PointCloud|MeshOperations' --timeout 60`.
Previously-active
[`RUNTIME-085`](../done/RUNTIME-085-geometrysources-mesh-residency.md) —
`GeometrySources` mesh residency bridge retired to `tasks/done/` on
2026-05-28 after the Slice D closure check. Slices A–C landed on
`claude/optimistic-hypatia-yJ5qw` / `claude/intrinsicengine-agent-onboarding-FLLuF`
/ `claude/gallant-knuth-Y4iFV`; Slice D closure ran on
`claude/serene-albattani-3KDrI`. Maturity is `CPUContracted`: the full
`IntrinsicTests` build under the `ci` preset and the default CPU gate
(`ctest -LE 'gpu|vulkan|slow|flaky-quarantine'`) report 2322/2324 passed,
with only the two pre-existing `IntrinsicBenchmarkSmoke.HalfedgeSmoke.Run`/
`.Validate` (Not Run) failures unchanged and unrelated to this task; all 44
`MeshGeometryExtraction`/`MeshGeometryPackerTest` cases pass. `Operational`
visual proof is deferred to `RUNTIME-095` (final working-sandbox acceptance).

[`GRAPHICS-077`](../done/GRAPHICS-077-transient-debug-primitive-upload-helper.md) —
transient-debug upload helper retired to `tasks/done/` on 2026-05-28 after
Slice D added `TransientDebugSurfaceGpuSmoke`; maturity is `CPUContracted` on
CPU-only hosts and command-stream `Operational` on Vulkan-capable hosts. Pixel
readback parity is tracked by
[`GRAPHICS-077E`](../backlog/rendering/GRAPHICS-077E-transient-debug-pixel-readback.md).

[`GRAPHICS-078`](../done/GRAPHICS-078-visualization-overlay-upload-helper.md) —
visualization-overlay upload helper retired to `tasks/done/` on 2026-05-28 after
Slice D added `VisualizationOverlaySurfaceGpuSmoke`; maturity is
`CPUContracted` on CPU-only hosts and command-stream `Operational` on
Vulkan-capable hosts. Pixel readback parity is tracked by
[`GRAPHICS-078E`](../backlog/rendering/GRAPHICS-078E-visualization-overlay-pixel-readback.md).

[`GEOM-015`](../done/GEOM-015-gjk-termination-diagnostics.md) — GJK
termination diagnostics and scale-aware tolerance policy retired to
`tasks/done/` on 2026-05-22 after all four slices landed (PRs #915,
#917, #919). The next slice was picked per the priority rules
in [`docs/agent/prompt/prompt.md`](../../docs/agent/prompt/prompt.md):
no reproducible bugs are open, so the earliest unblocked Theme A leaf
in [`tasks/backlog/README.md`](../backlog/README.md) won —
`GRAPHICS-076`, gated only by the retired `GRAPHICS-075`.

[`RUNTIME-082`](../done/RUNTIME-082-spatial-debug-adapters.md) —
`Extrinsic.Runtime.SpatialDebugAdapters` umbrella retired to
`tasks/done/` on 2026-05-27 after Slice D landed on
`claude/intrinsicengine-agent-onboarding-xnNIW`
(`ECS::Components::SpatialDebugBinding` + cache-owned adapters via
`std::unique_ptr` + `RuntimeRenderSnapshotBatch::SpatialDebug*` spans
+ per-frame stats; five new integration tests pass under the default
CPU/null gate; 2245/2247 overall, the two pre-existing
`IntrinsicBenchmarkSmoke.HalfedgeSmoke` failures unchanged).

[`GEOM-008`](../done/GEOM-008-linear-algebra-solver-infrastructure.md) —
Geometry linear algebra and solver infrastructure retired to
`tasks/done/` on 2026-05-27 after Slice A landed in commit `c1aeafb`
(merged into the working tree via `cfe2f0c`). Slice A introduced the
Eigen3 dependency, the narrow `Geometry.Linalg` Eigen-backed dense/
adapter module, the reusable `Geometry.Sparse` CSR/builder/diagnostics/CG
module, and bridged `Geometry.DEC` CSR/CG to the new sparse layer.
Closes maturity at `CPUContracted`; no GPU/SuiteSparse/CHOLMOD backend
is owed by this task (recorded as later optional follow-ups in
`docs/architecture/geometry.md`). Verified on 2026-05-27 against the
default CPU gate (`ctest -LE 'gpu|vulkan|slow|flaky-quarantine'`)
together with the layering, test-layout, docs-links, task-policy, and
module-inventory regeneration checks.
