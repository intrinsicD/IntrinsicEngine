# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`RUNTIME-085`](RUNTIME-085-geometrysources-mesh-residency.md) —
  `GeometrySources` mesh residency bridge. Status: in-progress
  (Slices A + B landed; Slice C remains). Slice A landed on
  `claude/optimistic-hypatia-yJ5qw`; Slice B landed on
  `claude/intrinsicengine-agent-onboarding-FLLuF` 2026-05-28
  (11 new `contract;runtime` mesh-extraction tests pass; 115/115
  contract;runtime gate green; full default CPU gate 2339/2341
  with the two pre-existing `IntrinsicBenchmarkSmoke.HalfedgeSmoke`
  failures unchanged). Promoted from `tasks/backlog/runtime/` on
  2026-05-27 as the earliest unblocked Theme A working-sandbox
  leaf (all three active Theme B' rendering tasks
  GRAPHICS-076/077/078 parked on Vulkan-host blockers; HARDEN-065,
  GRAPHICS-030B, GRAPHICS-070/071 all retired in `tasks/done/`).
  Owner: unassigned for Slice C. Slice A added the new module
  `Extrinsic.Runtime.MeshGeometryPacker` that converts a mesh
  `GeometrySources` `ConstSourceView` into a triangle-list
  `GpuWorld::GeometryUploadDesc` with deterministic local-sphere
  bounds and a fail-closed `MeshPackStatus` taxonomy
  (`Success`/`WrongDomain`/`MissingPositions`/
  `MissingHalfedgeTopology`/`MissingFaceTopology`/`EmptyMesh`/
  `InvalidTopology`/`NonFinitePosition`/`DegenerateAllFaces`),
  together with 20 `contract;runtime` tests covering the happy
  path (single triangle + quad fan triangulation), determinism,
  scratch-buffer reuse, and every failure mode. Slice B wired
  `RenderExtractionCache::ExtractAndSubmit` to detect mesh-domain
  entities with `RenderSurface` and neither `ProceduralGeometryRef`
  nor an `AssetInstance::Source`, route them through the Slice A
  packer against a sidecar-owned `MeshPackBuffer`, upload via
  `GpuWorld::UploadGeometry`, store the resulting handle in a new
  sidecar `MeshGeometry` field, call
  `GpuWorld::SetInstanceGeometry`, and fold per-status counters
  into `RuntimeRenderExtractionStats` (`MeshGeometryUploads`,
  `MeshGeometryReuseHits`, `MeshGeometryFailedPack`,
  `MeshGeometryMissingPositions`, `MeshGeometryInvalidTopology`,
  `MeshGeometryReleases`). Subsequent extractions short-circuit
  to the cached handle (`MeshGeometryReuseHits`); retirement and
  shutdown free the runtime-owned upload through
  `GpuWorld::FreeGeometry` and increment `MeshGeometryReleases`.
  `RenderableSidecarView` gains `MeshGeometry`/`HasMeshResidency`
  so tests can confirm the right slot bound the entity. Slice C
  will drain `DirtyVertexPositions` / `DirtyFaceTopology` /
  `DirtyEdgeTopology` / `GpuDirty` for processed mesh entities,
  add `MeshGeometryReuploads`, and route the release through the
  same `framesInFlight` deferred-retire window the procedural
  cache uses. Next verification step:
  `ctest --test-dir build/ci --output-on-failure -L 'contract' -L 'runtime' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.

Previously-active
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
