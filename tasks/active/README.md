# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

_None._

Previously-active
[`RUNTIME-089`](../done/RUNTIME-089-selection-controller.md) — runtime selection
controller and snapshot handoff (Theme A working-sandbox path) retired to
`tasks/done/` on 2026-05-31 at maturity `CPUContracted`. Slice A landed the
standalone `Extrinsic.Runtime.SelectionController` module (input-facing
hover/click/programmatic APIs, per-frame pick coalescing, sequence-tracked
in-flight readback consumption, Replace/Add/Toggle `SelectedTag`/`HoveredTag`
mutation, stale/non-selectable rejection, the `uint32 ↔ entt::entity` lookup
seam, controller-owned selection-snapshot buffers, and the diagnostics block)
with pure-CPU `contract;runtime` tests in `Test.SelectionController.cpp`
(`Scaffolded`). Slice B (`claude/intrinsicengine-agent-onboarding-VBuRD`) closed
`Scaffolded → CPUContracted` by wiring the controller into the real runtime
frame path: `Engine` now owns a `SelectionController` (`GetSelectionController()`),
drains the coalesced pick into `RenderFrameInput::Pick`/`HasPendingPick` and
`SelectionSystem::RequestPick` before `ExtractRenderWorld`, consumes
`SelectionSystem::GetLastPickResult()` (oldest in-flight pick) in the maintenance
phase and clears it, and mirrors the controller snapshot into
`RenderWorld.Selection` through a new `const SelectionController*` argument to
`RenderExtractionCache::ExtractAndSubmit` → `RuntimeRenderSnapshotBatch::Selection*`
→ renderer stable storage → `ExtractRenderWorld` (graphics reporting-only, no
live ECS read). Five new `Test.SelectionSnapshotExtraction.cpp` `contract;runtime`
cases (selected/hovered/additive mirror, null-controller empty, cleared-empty)
plus the 23 Slice A cases pass; full contract gate 253/253 (221 runtime + 32
graphics); layering/test-layout/doc-links/task-policy/module-inventory (no diff)
checks clean. `Operational` outline/pick proof stays owned by `GRAPHICS-074` plus
the final working-sandbox acceptance task (`RUNTIME-095`); the real input→pick
binding is owned by a later editor/UI task.

Previously-active
[`RUNTIME-088`](../done/RUNTIME-088-mesh-primitive-view-lifecycle.md) — mesh
primitive view lifecycle (Theme A working-sandbox path) retired to `tasks/done/`
on 2026-05-31 at maturity `CPUContracted`. Slice A landed the standalone
`Extrinsic.Runtime.MeshPrimitiveViewPacker` (edge line-list + vertex point
derivation packers, `MeshPrimitiveViewSettings` control surface, fail-closed
status taxonomy, pure-CPU `contract;runtime` packer tests) at `Scaffolded`.
Slice B (commit `69b3fb4`, `claude/intrinsicengine-agent-onboarding-RQtst`)
closed `Scaffolded → CPUContracted` by wiring the `RenderExtractionCache`
residency: a cache-owned `MeshPrimitiveViewSettings` map (runtime/editor state,
never in ECS components), per-view `GpuWorld` instance + `GpuGeometryHandle`
sidecars rendering edges/vertices as extra `GpuRender_Line`/`GpuRender_Point`
unlit lanes over the one authoritative mesh `GeometrySources`, repack on the
shared mesh dirty signal, release on disable/eligibility-flip/destruction/
shutdown through the `TickMeshPrimitiveViewGeometry` deferred-retire window
(wired in `Engine::RunFrame`), and fifteen `Mesh{Edge,Vertex}View*` +
`MeshPrimitiveViewFreeRetires` counters. Resolved deferred decisions: views are
runtime sidecars (not child ECS entities), and the settings live in a
cache-owned map. `IntrinsicRuntimeContractTests` 193/193 (12 new
`MeshPrimitiveViewExtraction.*` cases); layering/test-layout/doc-links/
task-policy/module-inventory checks clean. `Operational` visual proof of the
three lanes stays owned by `RUNTIME-095`.

Previously-active
[`RUNTIME-087`](../done/RUNTIME-087-geometrysources-pointcloud-residency.md) —
`GeometrySources` point-cloud residency bridge (Theme A working-sandbox path)
retired to `tasks/done/` on 2026-05-30 at maturity `CPUContracted`. Landed as
one robust slice mirroring `RUNTIME-086`: standalone
`Extrinsic.Runtime.PointCloudGeometryPacker` plus `RenderExtractionCache`
residency wiring, deferred-retire window, and shutdown drain landed together
because the upload path is not leak-free without the retire/shutdown lifecycle.
`RenderExtractionCache` now routes `Domain::PointCloud` entities carrying
`RenderPoints` through `BindPointCloudGeometry` (upload/reuse/dirty-reupload),
owns the per-entity `PointCloudGeometry` handle, drains the cloud dirty-domain
tags (`DirtyVertexPositions`/`DirtyVertexAttributes`/`GpuDirty`), releases on
eligibility flip / destruction / shutdown through `EnqueuePointCloudRetire` +
the `TickPointCloudGeometry` deferred-retire window (maintenance-phase wired in
`Engine::RunFrame`), and reports eight `PointCloudGeometry*` counters. Only a
uniform float `RenderPoints::SizeSource` is supported (per-point size buffers
fail closed); a point-cloud-domain entity without `RenderPoints` is not a
renderable, so a mesh that loses topology to a bare vertex set is not re-bound
as points. New `contract;runtime` cases in `Test.PointCloudGeometryExtraction.cpp`
and `Test.PointCloudGeometryPacker.cpp` cover upload/reuse, `PopulateFromCloud`,
two-entity independence, deferred-retire on destruction, shutdown, procedural
preemption, fail-closed counters (missing positions, non-finite, unsupported
size source), eligibility flips, and parameterized dirty-tag reupload. Default
CPU gate: focused `-R 'PointCloudGeometry|GraphGeometry|MeshGeometry'` 96/96;
full gate 2394/2396 (only the two pre-existing unrelated
`IntrinsicBenchmarkSmoke.HalfedgeSmoke` Not-Run failures); layering/test-layout/
doc-links/task-policy/module-inventory checks clean. `Operational` visual proof
is owned by the final working-sandbox acceptance task (`RUNTIME-095`).

Previously-active
[`RUNTIME-086`](../done/RUNTIME-086-geometrysources-graph-residency.md) —
`GeometrySources` graph residency bridge (Theme A working-sandbox path) retired
to `tasks/done/` on 2026-05-30 at maturity `CPUContracted`. Slice A (standalone
`Extrinsic.Runtime.GraphGeometryPacker`) landed earlier; Slices B and C landed
together on `claude/intrinsicengine-agent-onboarding-c9ql3` because the
extraction upload path is not leak-free without the retire/shutdown lifecycle,
so the smallest robust slice is the full residency mirror of `RUNTIME-085`.
`RenderExtractionCache` now routes `Domain::Graph` entities carrying
`RenderLines`/`RenderPoints` through `BindGraphGeometry` (upload/reuse/
dirty-reupload), owns the per-entity `GraphGeometry` handle, drains the graph
dirty-domain tags, releases on eligibility flip / destruction / shutdown through
`EnqueueGraphRetire` + the `TickGraphGeometry` deferred-retire window
(maintenance-phase wired in `Engine::RunFrame`), and reports eight
`GraphGeometry*` counters. Fourteen new `contract;runtime` cases in
`Test.GraphGeometryExtraction.cpp` cover line/point/both-lane uploads, reuse,
two-entity independence, deferred-retire on destruction, shutdown, procedural
preemption, fail-closed counters, eligibility flips, and parameterized dirty-tag
reupload. Default CPU gate: focused `-R 'GraphGeometry|MeshGeometry'` 73/73;
full gate 2371/2373 (only the two pre-existing unrelated
`IntrinsicBenchmarkSmoke.HalfedgeSmoke` Not-Run failures); layering/test-layout/
doc-links/task-policy/module-inventory checks clean. `Operational` visual proof
is owned by the final working-sandbox acceptance task.

Previously-active
[`GEOM-012`](../done/GEOM-012-symmetric-domain-views-property-sharing.md) —
symmetric mesh/graph/point-cloud domain views retired to `tasks/done/` on
2026-05-29 after Slice E (conversion/move/consume policy) landed on
`claude/intrinsicengine-agent-onboarding-YjhiR`. Maturity `CPUContracted`. Slice E
reviewed the conversion coverage and added no new APIs: the container copy
constructor is the same-domain borrow→owning hard-copy seam (copy-assigning into
a borrowed destination instead writes through to the source, so promotion uses
copy construction or assignment into an owning destination),
`Geometry.Mesh.Conversion`/`Geometry.PointCloud.Conversion` own the cross-domain
hard copies, move-assign is the ownership-transfer seam, and the Slice D
`Const*View` types are already non-copyable/non-movable. Six new tests pin the
policy (three `SubmeshViewDomainBorrows.HardCopyOf*BorrowOwnsIndependentStorage`
cases, `SubmeshViewDomainBorrows.CopyAssignIntoBorrowedDestinationWritesThroughToSource`,
plus `MeshConversion.ConvertedHalfedgeMeshOutlivesSourceViaMoveOwnershipTransfer`
and `PointCloudConversion.ConvertedCloudOutlivesAndDecouplesFromSource`); the
focused geometry suite passed 181/181 with the layering, test-layout, doc-links,
and module-inventory (no diff) checks clean.

[`BUG-013`](../done/BUG-013-backbuffer-readback-contract-vtable-segv.md) —
backbuffer readback contract SEGV retired to `tasks/done/` on 2026-05-29 as
**not reproducible on a clean `ci` preset build**. On a freshly-cloned tree the
two `ConfiguredHandleRecordsReadbackTripletOnce` cases pass through the default
CPU gate (CTest #25/#87, label `contract`; 225/225 in
`IntrinsicGraphicsContractCpuTests`). The reported SEGV was a stale incremental
module-BMI artifact after `cc06edef`; the lasting prevention is the
clean-rebuild rule documented in `src/graphics/rhi/README.md`. Unblocks
`GRAPHICS-076E` CPU contract closure; no engine/test source was changed.

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
