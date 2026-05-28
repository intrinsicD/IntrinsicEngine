# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-076`](GRAPHICS-076-default-recipe-debug-view-and-present-wiring.md) —
  Default-recipe `Pass.DebugView` and canonical `Pass.Present` wiring.
  Status: blocked on operational default-recipe Vulkan bring-up
  (Slices A–C landed via PRs #921 + #922 (Slice A follow-up), #923,
  and #924; Slice D's recipe-selector fixture skeleton + build wiring
  landed 2026-05-26 on the current onboarding branch, but the
  fixture `GTEST_SKIP`s on a Vulkan-capable host because the default
  recipe still fails the operational Vulkan gate. The 2026-05-27
  BUG-012 slices fixed the synthetic-transient-handle/color-image
  depth-transition barrier, transfer-queue upload barrier stage masks,
  shader/pipeline-layout mismatches, per-frame command-buffer reuse by
  one-shot uploads, and missing default-recipe render-pass scopes. The
  diagnostic bypassed smoke now passes on the NVIDIA validation host;
  the remaining GRAPHICS-076 Slice D work is to intentionally remove or
  revise the cold-gate pre-check and decide the final default-recipe
  readback/parity acceptance. See the 2026-05-27 BUG-012 clarifications
  on the task file for exact evidence). Owner:
  unassigned; next pick-up by any agent willing to triage the
  upstream Vulkan default-recipe bring-up issues.
- [`GRAPHICS-077`](GRAPHICS-077-transient-debug-primitive-upload-helper.md) —
  Backend transient-debug-primitive upload helper. Status:
  in-progress (Slices A + B + C landed; only the optional Slice D
  remains, blocked on a Vulkan-capable host). Slice A landed on
  `claude/intrinsicengine-agent-onboarding-p1J2P` 2026-05-23;
  Slice B landed on `claude/intrinsicengine-agent-onboarding-MnHl0`
  2026-05-24; Slice C landed on
  `claude/intrinsicengine-agent-onboarding-WbeR9` 2026-05-24
  (235/235 graphics contract tests pass; 2209/2209 full CPU/null
  gate green). Owner: unassigned for Slice D; next pick-up by any
  agent on a Vulkan-capable host. Promoted from
  `tasks/backlog/rendering/` on 2026-05-23 on
  `claude/intrinsicengine-agent-onboarding-utAFW` as the next
  earliest unblocked Theme A leaf once GRAPHICS-076 parked on its
  Vulkan-host blocker. Four-slice plan: recipe/executor scaffold
  (A, scaffold-only — landed), triangle lane operational wiring
  (B, landed; introduces the `ITransientDebugUploadHelper` virtual
  interface + concrete default helper in
  `Extrinsic.Graphics.TransientDebugUploadHelper` and two triangle
  pipelines at call indices #26 + #27), line + point lane
  operational wiring (C, landed; adds four pipelines at call
  indices #28-#31, per-lane upload/record paths, and per-lane
  pipeline gating in `RecordTransientDebugSurfacePass`), and the
  optional `gpu;vulkan` smoke (D, deferred behind the same
  Vulkan-host gate as GRAPHICS-076 Slice D). The task now sits at
  `CPUContracted` on CPU-only hosts.
- [`GRAPHICS-078`](GRAPHICS-078-visualization-overlay-upload-helper.md) —
  Backend visualization-overlay upload helper (vector field +
  isoline). Status: in-progress (Slices A + B + C landed; only the
  optional Slice D `gpu;vulkan` smoke remains, blocked on a Vulkan-
  capable host). Slice A scaffolded the recipe + executor shape on
  `claude/intrinsicengine-agent-onboarding-3dLeQ` 2026-05-24
  (209/209 graphics contract tests pass). Slice B promoted the
  vector-field lane from `SkippedUnavailable` to `Recorded` on
  `claude/intrinsicengine-agent-onboarding-7IOiJ` 2026-05-24 and
  verified through the contract CPU/null gate (212/212 graphics
  contract tests pass). Slice C promoted the isoline lane from
  `SkippedUnavailable` to `Recorded` on
  `claude/intrinsicengine-agent-onboarding-2P6pn` 2026-05-25 and
  verified through the contract CPU/null gate (219/219 graphics
  contract tests pass; net +7 from Slice B: six new isoline
  contract tests + one prior baseline addition outside this task).
  Slice C introduces two isoline pipelines at call indices #34 +
  #35 via `BuildVisualizationIsolinePipelineDesc`, the new shader
  pair `assets/shaders/visualization_isoline.{vert,frag}`, an
  `UploadIsolines(...)` method on the helper with an independent
  per-lane host-visible vertex buffer, a `VisualizationIsolinePushConstants`
  push block (16 bytes carrying BDA + per-draw `FirstVertex`), a
  `bool DepthTested{true}` field on `IsolineOverlayPacket`, and
  per-lane gating semantics in `RecordVisualizationOverlayPass`
  matching GRAPHICS-077 Slice C exactly. Owner: unassigned for
  Slice D; next pick-up by any agent on a Vulkan-capable host.
  Promoted from `tasks/backlog/rendering/` on 2026-05-24 as the
  next earliest unblocked Theme A leaf once GRAPHICS-076 and
  GRAPHICS-077 each parked on their Vulkan-host blockers. Four-
  slice plan mirrors GRAPHICS-077 exactly: recipe/executor
  scaffold (A, landed), vector-field lane operational wiring (B,
  landed), isoline lane operational wiring (C, landed), and the
  optional `gpu;vulkan` smoke (D, deferred behind the same Vulkan-
  host gate as GRAPHICS-076 / 077 Slice D). The task now sits at
  `CPUContracted` on CPU-only hosts for the full two-lane
  visualization overlay.
- [`GEOM-012`](GEOM-012-symmetric-domain-views-property-sharing.md) —
  Symmetric mesh, graph, and point-cloud domain views. Status:
  in-progress (Slice A landed). Owner: unassigned. Branch: Slice A
  on `claude/funny-pascal-kTHxz`. Promoted from
  `tasks/backlog/geometry/` on 2026-05-28 as the next unblocked
  geometry task once GEOM-008 (Geometry.Linalg / Geometry.Sparse
  foundation) retired. Slice A adds the new `Geometry.DomainViews`
  module with `Geometry::DomainViews::BorrowMeshAsGraph` — a public
  factory that returns a `Graph::Graph` sharing the source mesh's
  vertex/halfedge/edge `PropertySet`s, deletion counters, and the
  canonical `v:point`/`v:connectivity`/`h:connectivity`/`v:deleted`/
  `e:deleted` slots with no compatibility-copy allocations. The
  `MakeMeshBackedGraphView` helper that previously lived in
  `tests/unit/geometry/Test_ShortestPath.cpp` is retired and all
  callers route through the public API. Six new tests in
  `Test_SubmeshViewDomainBorrows.cpp` cover shared-property
  identity, absence of `*_graph_*` shadow slots, deletion-counter
  sharing through direct counter mutation, mesh→view position-edit
  visibility, view→mesh position-edit visibility, and the
  empty-mesh case. Slice B (mesh-backed point-cloud) and Slice C
  (graph-backed point-cloud) follow the same factory pattern;
  Slice D introduces distinct const-view types; Slice E reviews
  the conversion/move/consume policy and closes at
  `CPUContracted`. Next verification step:
  `ctest --test-dir build/ci --output-on-failure -R 'SubmeshView|ShortestPath|PointCloud|RuntimeGraph|MeshOperations' --timeout 60`.
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
