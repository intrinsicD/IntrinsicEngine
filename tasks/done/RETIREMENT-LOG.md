# Retirement Log

Append-only narrative record of retired tasks, newest first. When retiring a
task, append its summary block here (see `docs/agent/task-format.md`,
"Retiring a task") instead of editing `tasks/active/README.md`. Links are
relative to `tasks/done/`, which sits at the same depth as `tasks/active/`,
so blocks moved from the old active-README history work verbatim.

## Retired task narratives

Backlog
[`GEOM-034`](GEOM-034-geometry-property-api-doc-audit.md) — Geometry property
API documentation audit — retired on 2026-06-28 at maturity `Scaffolded`
(documentation synchronization endpoint). The audit made
`docs/architecture/geometry-api-style.md` the coherent source for property
name lifetime, domain-prefix naming, validity, const lookup, bool/proxy access,
and descriptors, and replaced stale higher-layer shared-ownership `MeshView`
wording in `rendering-target-architecture.md` with links to the current
geometry-owned property/domain-view contracts.

Backlog
[`GEOM-042`](GEOM-042-mesh-normal-bilateral-denoiser.md) — Mesh normal-based
bilateral denoiser — retired on 2026-06-28 at maturity `CPUContracted`.
`Geometry::Smoothing` now exports the two-stage bilateral mesh denoiser with
face-normal filtering, normal-projection vertex updates, deterministic
diagnostics, and fail-closed handling for empty, non-manifold, degenerate,
non-finite, and invalid-parameter inputs. The default CPU gate passed after the
landed implementation, unblocking `UI-024`.

Backlog
[`GEOM-041`](GEOM-041-fem-laplacian-mass-stiffness-variants.md) — FEM
Laplacian mass/stiffness variants and edge-weight modes — retired on
2026-06-28 at maturity `CPUContracted`. DEC assembly now has Graph, Fujiwara,
and ModifiedNormal stiffness modes plus Sum, Barycentric, Voronoi, and
Galerkin mass modes, with `ClampedHalfedgeCotan` in mesh utilities and
row-sum/symmetry/SPD/fail-closed tests.

Backlog
[`GEOM-040`](GEOM-040-curvature-tensor-principal-directions.md) — Mesh
curvature tensor and principal directions — retired on 2026-06-28 at maturity
`CPUContracted`. `Geometry.Curvature` publishes `v:principal_dir1` and
`v:principal_dir2`, exposes `ComputeCurvatureTensor`, and reuses exported
`Geometry::PCA::SymmetricEigen3`; analytic curvature tensor tests and the
default CPU gate passed. This unblocks `UI-026`.

Active
[`RUNTIME-128`](RUNTIME-128-default-lit-material-for-materialless-imports.md)
— Default lit material for material-less imported primitives — retired on
2026-06-28 at maturity `CPUContracted`. Runtime model-scene materialization now
binds material-less primitives to a lazily created neutral lit StandardPBR
material while preserving slot 0 as the unlit missing/invalid material
indicator. The regression is covered by the runtime handoff contract test and
the default CPU gate.

Active
[`METHOD-012`](METHOD-012-progressive-poisson-disk-cpu-reference.md) —
Progressive Poisson-disk sampling: paper intake + CPU reference backend —
retired on 2026-06-28 at maturity `CPUContracted`. The method package now has
a deterministic CPU reference backend, manifest, docs, correctness tests, and a
smoke benchmark with quality metrics. Method and benchmark manifests validate,
and the default CPU gate passed, unblocking `METHOD-013` and `RUNTIME-134`.

Backlog
[`PROC-012`](PROC-012-resolve-duplicate-geom-027-id.md) — Resolve duplicate
`GEOM-027` task ID — retired on 2026-06-27 at maturity `Retired`. Two files
declared `id: GEOM-027`: the canonical property-name-lifetime contract (depended
on by `GEOM-033`/`GEOM-034`) and an unrelated research-control-surface /
shared-CPU-GPU backend-seam KMeans-exemplar task seeded by `9ed14b4`. The latter
was renumbered to `GEOM-052` (file rename via `git mv`, front-matter `id:`,
title, and every inbound reference in `tasks/backlog/README.md`, `PROC-010`,
`METHOD-013`, and `DOCS-003`), leaving `GEOM-027` bound to the property sequence
so its dependency graph is intact. `tools/agents/check_task_policy.py --root .
--strict` now reports 0 findings (was 1), and `check_doc_links.py` reports no
broken links.
[`BUG-046`](BUG-046-flaky-coretaskgraph-mainthread-ready-queue-ordering.md) —
Flaky `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering` — retired
on 2026-06-24 at maturity `CPUContracted`. `TaskGraph::Execute()` now collects
newly-ready successors into a batch and publishes all main-thread-ready entries
under one queue lock before dispatch can drain them, preserving priority/cost
ordering for simultaneously-ready main-thread work. The regression no longer
uses the fixed `40ms` sleep and the focused repeat run plus default CPU gate
passed.

Backlog
[`BUG-049`](BUG-049-gpuworld-geometry-rebind-upload-barriers.md) — GpuWorld
geometry rebind lacks upload-to-read barriers — retired from stale backlog on
2026-06-22 at maturity `CPUContracted`; implementation landed in `843e4fb3`.
The audit confirmed `GpuWorld` tracks one-shot pending upload barriers for
direct `IDevice::WriteBuffer` paths, `Renderer` drains them before culling and
draw consumers, and the focused geometry-rebind plus dirty-extraction coverage
still passes. The task was already complete but remained listed under active
bugs.

Backlog
[`BUG-048`](BUG-048-direct-mesh-postprocess-overwrites-recomputed-normals.md) —
Direct mesh post-process overwrites recomputed normals — retired from stale
backlog on 2026-06-22 at maturity `CPUContracted`; implementation landed in
`843e4fb3`. The audit confirmed direct mesh post-process apply preserves
count-matched current `v:normal` data so editor-authored normals survive
deferred materialization, while generated normal texture registration remains
intact. Focused sandbox editor normal recompute regressions still pass.

Backlog
[`BUG-047`](BUG-047-surface-normal-texture-overrides-vertex-normals.md) —
Surface normal texture overrides vertex-normal shading — retired from stale
backlog on 2026-06-22 at maturity `CPUContracted`; implementation landed in
`843e4fb3`. The audit confirmed promoted forward/GBuffer shader contracts use
packed vertex normals for current surface shading and assert absence of
`mat.NormalID` / `normalTex` sampling. Tangent-space normal-map support remains
out of scope for this temporary attribute-normal policy.

Active
[`BUG-051`](BUG-051-mesh-color-visualization-property-buffer.md) — Mesh color
visualization lacks automatic property-buffer extraction — retired on
2026-06-22 at maturity `CPUContracted`. Runtime render extraction now
auto-emits mesh `glm::vec4` color property-buffer packets from mesh
`GeometrySources` for per-vertex/per-edge/per-face color-buffer
visualizations, including `v:color`, without requiring an explicit adapter
binding. Missing and unsupported color sources fail closed through adapter
diagnostics, and `VisualizationSyncSystem` now forwards the selected
per-element color-buffer domain into `GpuEntityConfig::VisDomain` so shader
lookup uses the configured vertex/face/edge domain. The structural vertex-color
stream and GPU SoA migration remain owned by RUNTIME-121/RUNTIME-122.

Active
[`BUG-050`](BUG-050-direct-mesh-first-upload-normals.md) — Direct mesh first
upload lacks computed normals — retired on 2026-06-22 at maturity
`CPUContracted`. The geometry-only runtime mesh materialization helper now
writes explicit or deterministic area-weighted fallback `v:normal` values
before ECS `GeometrySources` publication, so direct mesh imports and
progressive raw model-scene primitives carry count-matched normals on their
first upload. Authored normals remain authoritative, deferred UV/texture-bake
post-processing still applies back to the same entity through dirty extraction,
and generated normal texture bindings remain data-only until the promoted
texture path consumes them.

Active
[`PROC-009`](PROC-009-import-productivity-skills.md) — Import productivity
skills into repo skill surface — retired on 2026-06-22. The repo-local skill
surface now includes the third-party `teach`, `grilling`, and `grill-me`
productivity skills imported from `mattpocock/skills` commit
`6eeb81b5fcfeeb5bd531dd47ab2f9f2bbea27461`; `.claude/skills` and
`.codex/skills` see them through their existing symlinks to
`tools/agents/skills`. The import preserved the upstream MIT license notice in
`tools/agents/skills/THIRD_PARTY_LICENSES.md`, documents the skills as
standalone manual imports rather than generated `docs/agent/*` mirrors, and
adds a local `teach` guardrail so learning-workspace files are not created at
the IntrinsicEngine repo root without an explicit workspace. The existing
`sync_skills.py --check` gate remains scoped to IntrinsicEngine canonical-doc
mirrors and passed after the import.

Active
[`GRAPHICS-098`](GRAPHICS-098-gpu-transfer-facade.md) — High-level
`GpuTransfer` facade with correct barrier brackets — retired on 2026-06-22 at
maturity `Operational` on Vulkan-capable hosts (`CPUContracted` elsewhere).
`Extrinsic.Graphics.GpuTransfer` now composes existing RHI seams without adding
new RHI surface: async uploads return facade tickets over `TransferToken`s and
emit their one-shot `TransferWrite -> ShaderRead` ready barrier only from
`DrainCompleted(...)` after completion; the in-command path records
`CopyBuffer` plus the ready barrier on one submitted timeline; readbacks record
the caller-owned `TransferRead` bracket before entering the GRAPHICS-096
readback ring. CPU contract evidence covers completion-gated barriers,
same-timeline copies, readback delivery, fail-closed range validation through
GRAPHICS-095, and diagnostics. Promoted Vulkan evidence covers a device-local
upload/readback round-trip through the facade without `WaitIdle`. Runtime-owned
GPU readback jobs and property write-back remain tracked by `RUNTIME-126`.

Active
[`GRAPHICS-097`](GRAPHICS-097-async-texture-readback.md) — Async GPU-to-CPU
texture readback through the readback ring on `ITransferQueue` — retired on
2026-06-22 at maturity `Operational` on Vulkan-capable hosts
(`CPUContracted` elsewhere). `Extrinsic.RHI.TransferQueue` now exposes
`DownloadTexture(TextureHandle, TextureLayout, mip, layer, ReadbackSink)` as an
append-only non-blocking readback virtual over the GRAPHICS-096
`ReadbackToken`/`ReadbackSink` drain. Null and non-operational Vulkan fallback
queues fail closed with dropped-readback diagnostics. The live Vulkan queue
validates color `Tex2D` arrays and six-face cubemaps through
`Extrinsic.RHI.TextureUpload`, rejects depth-stencil/unsupported formats,
invalid sinks, missing `TransferSrc` usage, out-of-range subresources, and
non-`TransferSrc` source layouts, records `vkCmdCopyImageToBuffer` into a
recycled mapped readback slot without auto-transitioning the source texture,
and delivers exactly the requested mip/layer bytes from `CollectCompleted()`.
CPU evidence covers Null fail-closed behavior, mock subresource delivery, and
bad-format/subresource/layout drops; opt-in `gpu;vulkan` evidence covers a
multi-mip 2D-array texture upload, caller-owned `ShaderReadOnly -> TransferSrc`
barrier, mip/layer readback, and caller-owned transition back without using
`WaitIdle`. High-level barrier/facade ergonomics remain owned by GRAPHICS-098.

Active
[`GRAPHICS-096`](GRAPHICS-096-async-buffer-readback-ring.md) — Async GPU-to-CPU
buffer readback ring on `ITransferQueue` — retired on 2026-06-22 at maturity
`Operational` on Vulkan-capable hosts (`CPUContracted` elsewhere).
`Extrinsic.RHI.TransferQueue` now exposes `ReadbackToken`, `ReadbackSink`,
`DownloadBuffer(...)`, readback completion polling, and transfer diagnostics for
queued/completed/dropped downloads, staged bytes, and ring high-water. Null and
non-operational Vulkan fallback queues fail closed with dropped-readback
diagnostics, while the live Vulkan queue validates ranges through
`Extrinsic.RHI.BufferTransfer`, copies device buffers into recycled mapped
host-visible staging slots on the transfer timeline, and delivers sink bytes
only from `CollectCompleted()`. CPU evidence covers Null fail-closed behavior,
mock drain delivery, out-of-range rejection, and drain-gated completion; opt-in
`gpu;vulkan` evidence covers a device-local buffer round-trip through the ring
without routing through the legacy blocking `IDevice::ReadBuffer()` helper.
Texture readback remains owned by GRAPHICS-097, and high-level barrier/facade
ergonomics remain owned by GRAPHICS-098.

Active
[`GRAPHICS-095`](GRAPHICS-095-buffer-transfer-math-helper.md) — CPU-testable
buffer transfer math and validation helper — retired on 2026-06-22 at maturity
`CPUContracted`. `Extrinsic.RHI.BufferTransfer` now provides CPU-pure buffer
sub-range validation, non-power-of-two alignment helpers, destination dirty
range planning with optional coalescing and source-offset packing, and a
property-agnostic typed dimension/range validator for downstream property
binding. The module imports only `core` and RHI descriptors, has no backend or
device surface, is listed in the RHI README and generated module inventory, and
is covered by a 14-case `unit;graphics` BufferTransfer test suite. Operational
GPU readback/upload use remains owned by GRAPHICS-096, GRAPHICS-098, and
RUNTIME-126.

Active
[`UI-020`](UI-020-visualization-lane-uniform-color.md) — Visualization lane
uniform color controls — retired on 2026-06-19 at maturity `CPUContracted`.
The sandbox visualization command/model seam now distinguishes the selected
entity's default visualization config from optional surface, edge, and point
lane overrides. Domain visualization windows target their render lanes by
source-row presence instead of only by the mutually exclusive active domain, so
mesh vertices and graph nodes rendered as points can take an independent
uniform color. Runtime extraction resolves those lane overrides for mesh
surface/edge/vertex sidecars, graph line/point instances, and point-cloud
points; scene JSON persists the optional lane descriptors as CPU-only data.
Focused evidence passed the `IntrinsicRuntimeContractTests` build and 93/93
CPU/null contract tests across `SandboxEditorUi`,
`MeshPrimitiveViewExtraction`, `GraphGeometryExtraction`, and
`RuntimeSceneSerialization`.

Active
[`UI-019`](UI-019-visualization-uniform-color-edit.md) — Visualization uniform
color edit widget — retired on 2026-06-19 at maturity `CPUContracted`. Mesh,
graph, point-cloud, and top-level geometry visualization UI windows now expose
an ImGui `ColorEdit4` control when the selected entity's
`VisualizationConfig::ColorSource` is `UniformColor`. The edit path reuses the
runtime-owned visualization config command, preserves the rest of the config
payload when switching/editing uniform color, and does not add renderer/RHI/
asset ownership to UI. Focused evidence passed the
`IntrinsicRuntimeContractTests` build and all 51 `SandboxEditorUi` CPU/null
contract tests.

Backlog
[`GRAPHICS-089`](GRAPHICS-089-generated-uv-texture-sampling-vulkan-smoke.md) —
Generated-UV texture sampling Vulkan smoke — retired on 2026-06-19 at maturity
`Operational` on Vulkan-capable hosts. The opt-in runtime sandbox
`gpu;vulkan` smoke now imports an OBJ that omitted authored `vt` coordinates,
waits for the promoted Vulkan default recipe to become operational, uploads a
generated albedo texture through `Runtime.AssetModelTextureHandoff` and
`Graphics.GpuAssetCache`, binds it through the progressive material texture
path, and asserts the rendered surface samples that generated texture using
ASSETIO-008 generated `v:texcoord` values rather than default zero UVs or
shader-side UV fabrication. Final evidence passed `ci-vulkan` configure,
`cmake --build --preset ci-vulkan --target IntrinsicTests`, the targeted
`gpu;vulkan` CTest, and the default CPU-supported CTest gate.

Backlog
[`GRAPHICS-091`](GRAPHICS-091-unify-scalar-colormap-across-surface-line-point.md) —
Unify scalar-field / colormap visualization across surface, line, and point
passes — retired on 2026-06-19 at maturity `Operational` on Vulkan-capable
hosts (`CPUContracted` elsewhere). Promoted surface, line, and point shaders now
share `common/gpu_scene.glsl` visualization color resolution for material,
uniform, scalar-field, and per-element RGBA modes. `VisualizationSyncSystem`
writes matching `GpuEntityConfig` scalar/color contracts for surface, line, and
point, the opt-in runtime sandbox `gpu;vulkan` smoke proves line/point
scalar-field colormap pixels, and final CPU/non-GPU retirement evidence reran
the forward/deferred surface pipeline survival checks plus the shared helper
shader check.

Previously-active
[`RUNTIME-116`](RUNTIME-116-focus-camera-on-selection-command.md) —
Focus-camera-on-selection command (F key) — retired on 2026-06-19 at maturity
`CPUContracted`. Runtime now owns `Extrinsic.Runtime.CameraFocusCommand`, a
deterministic command surface that aggregates selected entities' refreshed
`World::Bounds` into a center-of-mass focus sphere, applies it through
`CameraControllerRegistry`, and marks the camera transition. `Engine::RunFrame`
binds the command to `F` after `FlushPreRenderTransformState`, suppresses it
while ImGui owns the keyboard, and rebuilds the render camera on success so the
same frame sees the reframed camera. PR #983 merged the command, the post-flush
review fix, runtime architecture docs, module inventory refresh, and the 13-case
`contract;runtime` `Test.RuntimeCameraFocusCommand.cpp` suite. No `Operational`
follow-up is owed; the thin key binding composes already-operational input and
camera-controller paths, and the reusable command closes at `CPUContracted`.

Backlog
[`GRAPHICS-092`](GRAPHICS-092-group-per-domain-params-and-line-width-residency.md) —
Group per-domain params in `GpuEntityConfig` and add line-width residency —
retired on 2026-06-18 at maturity `Operational` on Vulkan-capable hosts
(`CPUContracted` elsewhere). `GpuEntityConfig` now groups point and line
domain-specific settings into named sub-blocks while preserving shared
visualization fields; `RenderEdges::WidthSource` populates
`Line.LineWidth` / `Line.LineWidthBDA`; and the retained forward line shader
consumes those values while expanding the non-indexed `LineQuads` topology.
The final slice added an opt-in runtime sandbox `gpu;vulkan` smoke that authors
a 12 px reference-triangle line width, reads back the GPU config, confirms the
edge/point draw lanes remain emitted, and samples the default-recipe
backbuffer for the configured line overlay.

Backlog
[`GRAPHICS-094`](GRAPHICS-094-retained-point-size-bda-consumption.md) —
Consume per-point size BDA in retained point shader — retired on 2026-06-18
at maturity `CPUContracted`. The retained forward point shader now resolves
pixel size from `GpuEntityConfig::Point.PointSizeBDA[sourceVertexIndex]` when
that BDA is populated, otherwise falling back to uniform
`GpuEntityConfig::Point.PointSize`, with the existing clamp applied to both
paths. The slice added shader-source contract coverage, preserved the
`GpuEntityConfig` layout, updated renderer/architecture docs, and leaves no
required `Operational` follow-up unless future visual point-size readback smoke
coverage is explicitly opened.

Backlog
[`LEGACY-042`](LEGACY-042-retire-legacy-asset-pipeline-test.md) — Retire
legacy Asset.Pipeline test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy `tests/unit/assets/Test_AssetPipeline.cpp` and the
dedicated `IntrinsicRuntimeTests.AssetPipelineHeadlessGrouped` CTest were
removed because they verified the old `Runtime::AssetPipeline`
main-thread-queue, material-list, `RHI::TransferToken` polling, and direct
`AssetManager` finalization surface. Promoted asset streaming/upload ownership
is split across `Extrinsic.Asset.LoadPipeline`, `AssetService`,
`Graphics.GpuAssetCache`, and runtime model/texture handoffs. `LEGACY-004`
remains blocked by 6 remaining test consumers and 50 legacy-internal consumers;
`LEGACY-005` remains blocked by 18 remaining test consumers and 133
legacy-internal consumers; `LEGACY-009` remains blocked by 14 remaining test
consumers and 83 legacy-internal consumers; `LEGACY-012` owns the remaining
test cleanup.

Backlog
[`LEGACY-041`](LEGACY-041-retire-legacy-asset-manager-core-test.md) — Retire
legacy Asset.Manager core test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy `tests/unit/assets/Test_CoreAssets.cpp` was removed
because it verified the old `Core::Assets::AssetManager`
async/cache/lease/clear/TryGetFast compatibility surface, including a
compile-only dependency on legacy `Graphics::Material`. Promoted asset
ownership uses `Extrinsic.Asset.Service`, `Asset.Registry`,
`Asset.PayloadStore`, `Asset.LoadPipeline`, `Asset.EventBus`, and runtime-owned
asset-to-graphics handoff contracts rather than the old pointer-returning
manager/lease API. `LEGACY-004` remains blocked by 7 remaining test consumers
and 50 legacy-internal consumers; `LEGACY-005` remains blocked by 19 remaining
test consumers and 133 legacy-internal consumers; `LEGACY-008` remains blocked
by 36 remaining test consumers and 22 legacy-internal consumers; `LEGACY-009`
remains blocked by 15 remaining test consumers and 83 legacy-internal
consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-040`](LEGACY-040-retire-legacy-asset-manager-safety-test.md) —
Retire legacy Asset.Manager safety test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy `tests/unit/assets/Test_CoreAssetSafety.cpp` was
removed because it verified the old `Core::Assets::AssetManager`
loader-safety/error-path compatibility surface: copyable loader constraints,
pointer-returning `GetRaw` / `AcquireLease` errors, and null pointer load
failures. Promoted asset ownership is split across `Extrinsic.Asset.Service`,
`Asset.Registry`, `Asset.PayloadStore`, and `Asset.LoadPipeline`, whose tests
already cover retained captured-loader reload, reload failure preservation,
wrong-type reads, dead-handle errors, failed-load cleanup, load-state
transitions, and event ordering. `LEGACY-004` remains blocked by 8 remaining
test consumers and 50 legacy-internal consumers; `LEGACY-005` remains blocked
by 20 remaining test consumers and 133 legacy-internal consumers; `LEGACY-012`
owns the remaining test cleanup.

Backlog
[`LEGACY-039`](LEGACY-039-retire-legacy-element-selection-test.md) — Retire
legacy element-selection test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy
`tests/integration/runtime/Test_ElementSelection.cpp` was removed because the
old mutable `Runtime.Selection::SubElementSelection` vertex/edge/face set API is
not promoted. Current sub-primitive workflows use
`Extrinsic.Runtime.PrimitiveSelectionRefinement`, engine-owned refined-pick
caching, and editor selection models. `LEGACY-006` remains blocked by 19
remaining test consumers and 37 legacy-internal consumers; `LEGACY-010` remains
blocked by 11 remaining test consumers; `LEGACY-012` owns the remaining test
cleanup.

Backlog
[`LEGACY-038`](LEGACY-038-retire-runtime-selection-modes-test.md) — Retire
legacy runtime selection modes test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy
`tests/contract/runtime/Test.RuntimeSelectionModes.cpp` was removed after the
retained add/toggle/replace/background multi-selection behavior was covered by
promoted `Extrinsic.Runtime.SelectionController` tests. The old
`Runtime.SelectionModule::GetSelectedEntities` raw-entity helper is not a
promoted endpoint; promoted consumers use stable-id snapshots and explicit
selection queries. `LEGACY-006` remains blocked by 20 remaining test consumers
and 37 legacy-internal consumers; `LEGACY-010` remains blocked by 12 remaining
test consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-037`](LEGACY-037-retire-legacy-asset-ingest-service-test.md) —
Retire legacy AssetIngestService test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy
`tests/unit/assets/Test_AssetIngestService.cpp` was removed instead of migrated
because it verified only the old dependency-heavy
`Runtime.AssetIngestService` constructor and copy/move trait shape. Promoted
asset ingest behavior is owned by `Extrinsic.Runtime.AssetIngestStateMachine`,
promoted asset import bridges, and runtime model/texture handoffs from
`RUNTIME-101` and related asset/runtime tasks. `LEGACY-004` remains blocked by
9 remaining test consumers and 50 legacy-internal consumers; `LEGACY-005`
remains blocked by 21 remaining test consumers and 133 legacy-internal
consumers; `LEGACY-008` remains blocked by 37 remaining test consumers and 22
legacy-internal consumers; `LEGACY-009` remains blocked by 16 remaining test
consumers and 83 legacy-internal consumers; `LEGACY-010` remains blocked by 13
remaining test consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-036`](LEGACY-036-retire-legacy-event-bus-test.md) — Retire legacy
EventBus test — retired on 2026-06-18 at maturity `CPUContracted`. Legacy
`tests/unit/core/Test_EventBus.cpp` was removed instead of migrated because the
promoted ECS layer owns CPU-only event payloads, not the old
`ECS::Scene::GetDispatcher()` delivery surface, while promoted runtime owns
selection/hover mutation through `SelectionController`. Legacy
`GpuPickCompleted` and `GeometryUploadFailed` stay runtime/graphics-owned
diagnostics rather than ECS events. `LEGACY-006` remains blocked by 21
remaining test consumers and 37 legacy-internal consumers; `LEGACY-010` remains
blocked by 14 remaining test consumers; `LEGACY-012` owns the remaining test
cleanup.

Backlog
[`LEGACY-035`](LEGACY-035-resolve-legacy-rhi-deferred-destruction-tests.md) —
Resolve legacy RHI deferred-destruction tests — retired on 2026-06-18 at
maturity `CPUContracted`. The Vulkan `SafeDestroy*` cases split out of the
legacy runtime maintenance-lane test were retired as legacy RHI implementation
detail: promoted Vulkan keeps deferred deletion behind private backend-local
`DeferDelete` / frame-slot queues and does not expose legacy timeline-value or
unconditional-flush semantics through promoted `RHI::IDevice`. `LEGACY-009`
remains blocked by 17 test consumers and 83 legacy-internal consumers; no new
legacy RHI test consumer was added.

Backlog
[`LEGACY-034`](LEGACY-034-resolve-runtime-frame-loop-maintenance-tests.md) —
Resolve legacy runtime frame-loop and maintenance tests — retired on 2026-06-18
at maturity `CPUContracted`. Legacy
`tests/unit/runtime/Test_RuntimeFrameLoop.cpp` and
`tests/unit/runtime/Test_MaintenanceLane.cpp` were removed after retained
CPU/null frame-loop, platform, render-frame, maintenance, operational
transition, and shutdown ordering mapped to promoted
`Extrinsic.Core.FrameLoop` / `Extrinsic.Runtime.Engine` contracts. The legacy
feature-catalog rollback mode remains retired, and the backend-facing Vulkan
deferred-destruction checks were split to `LEGACY-035`. `LEGACY-005` remains
blocked by 22 remaining test consumers and 133 legacy-internal consumers;
`LEGACY-008` remains blocked by 38 remaining test consumers and 22
legacy-internal consumers; `LEGACY-009` remains blocked by 17 remaining test
consumers and 83 legacy-internal consumers; `LEGACY-010` remains blocked by 15
remaining test consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-033`](LEGACY-033-retire-runtime-engine-config-test.md) — Retire legacy
RuntimeEngineConfig test — retired on 2026-06-18 at maturity `CPUContracted`.
Legacy `tests/unit/runtime/Test_RuntimeEngineConfig.cpp` coverage was removed
instead of migrated because its `Runtime::EngineConfig` scalar validation fields
do not map to the promoted `Extrinsic.Core.Config.Engine` value-type surface.
Promoted config defaults and runtime engine/device-selection behavior remain
covered by promoted core/runtime tests. `LEGACY-010` remains blocked by 17
remaining test consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-032`](LEGACY-032-resolve-runtime-system-bundles-test.md) — Resolve
legacy `Runtime.SystemBundles` test migration — retired on 2026-06-18 at
maturity `CPUContracted`. Legacy
`tests/unit/runtime/Test_RuntimeSystemBundles.cpp` coverage was removed instead
of migrated after its assertions were mapped: retained fixed-step ECS activation
is covered by `Extrinsic.Runtime.EcsSystemBundle`, named graphics lifecycle
contracts are covered by existing graphics/runtime tests, and the old global
`Core.SystemFeatureCatalog` ordering/toggle behavior is not promoted.
`LEGACY-005` remains blocked by 24 remaining test consumers and 133
legacy-internal consumers; `LEGACY-006` remains blocked by 22 remaining test
consumers and 37 legacy-internal consumers; `LEGACY-010` remains blocked by 18
remaining test consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-031`](LEGACY-031-retire-ecs-framegraph-systems-test.md) — Retire
legacy ECS frame-graph systems test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy `tests/unit/ecs/Test_FrameGraphSystems.cpp` coverage was
removed instead of migrated because promoted ECS transform hierarchy, bounds
propagation, render-sync, and `Extrinsic.Runtime.EcsSystemBundle` contract tests
cover the retained fixed-step bundle behavior, while `AxisRotator` is recorded
as sample/demo behavior rather than canonical ECS. `LEGACY-005` remains blocked
by 25 remaining test consumers and 133 legacy-internal consumers; `LEGACY-006`
remains blocked by 23 remaining test consumers and 37 legacy-internal
consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-030`](LEGACY-030-retire-ecs-entity-commands-test.md) — Retire legacy
ECS entity-command test — retired on 2026-06-18 at maturity `CPUContracted`.
Duplicate legacy `tests/unit/ecs/Test_EntityCommands.cpp` coverage was removed
instead of migrated because promoted `Extrinsic.Runtime.EditorCommandHistory`
contract tests cover undo/redo and hierarchy delete planning, while promoted
ECS scene/bootstrap/hierarchy tests cover typed lifecycle and hierarchy
mutation. `LEGACY-005` remains blocked by 26 remaining test consumers and 133
legacy-internal consumers; `LEGACY-006` remains blocked by 24 remaining test
consumers and 37 legacy-internal consumers; `LEGACY-012` owns the remaining
test cleanup.

Backlog
[`LEGACY-029`](LEGACY-029-retire-core-benchmark-test.md) — Retire legacy
Core.Benchmark test — retired on 2026-06-18 at maturity `CPUContracted`.
Legacy-only `tests/benchmark/Test_Benchmark.cpp` coverage for
`Core.Benchmark::BenchmarkRunner` was removed rather than promoted; its retained
pass-timing telemetry assertions now live in promoted
`tests/unit/core/Test.CoreProfiling.cpp`. Benchmark manifests, runner JSON, SLO
thresholds, and baselines were not changed. `LEGACY-005` remains blocked by 27
remaining test consumers and 133 legacy-internal consumers; `LEGACY-012` owns
the remaining test cleanup.

Backlog
[`LEGACY-028`](LEGACY-028-architecture-slo-test-promoted.md) — Migrate
ArchitectureSLO test to promoted Core — retired on 2026-06-18 at maturity
`CPUContracted`. The benchmark/SLO test now imports promoted
`Extrinsic.Core.FrameGraph`, `Extrinsic.Core.Tasks`, and
`Extrinsic.Core.Tasks.CounterEvent` as
`tests/benchmark/slo/Test.ArchitectureSLO.cpp`; thresholds, warmup counts, and
measured workload sizes were left unchanged. `LEGACY-005` remains blocked by 28
remaining test consumers and 133 legacy-internal consumers; `LEGACY-012` owns
the remaining test cleanup.

Backlog
[`LEGACY-027`](LEGACY-027-core-memory-test-promoted.md) — Migrate CoreMemory
test to promoted Core — retired on 2026-06-18 at maturity `CPUContracted`.
Retained memory allocator coverage from
`tests/unit/core/Test_CoreMemory.cpp` now imports promoted
`Extrinsic.Core.Memory`, `Extrinsic.Core.Error`, and
`Extrinsic.Core.Telemetry` as `tests/unit/core/Test.CoreMemory.cpp`. The
smaller `Test.Core.MemoryLegacy.cpp` parity file was folded into the expanded
promoted test, and the legacy-linked core aggregate no longer builds the old
memory consumer. `LEGACY-005` remains blocked by 29 remaining test consumers
and 133 legacy-internal consumers; `LEGACY-012` owns the remaining test
cleanup.

Backlog
[`LEGACY-026`](LEGACY-026-retire-core-dagscheduler-test.md) — Retire legacy
Core.DAGScheduler test — retired on 2026-06-18 at maturity `CPUContracted`.
`tests/unit/core/Test_DAGScheduler.cpp` was removed because it only exercised
the old `Core::DAGScheduler` compatibility API; promoted
`Extrinsic.Core.Dag.Scheduler`, graph-compiler, and task-graph tests cover the
retained scheduling contract, including explicit dependencies, resource
hazards, weak reads, duplicate-access handling, reset behavior, deterministic
compiles, diagnostics, and stress cases. `LEGACY-005` remains blocked by 30
remaining test consumers and 133 legacy-internal consumers; `LEGACY-012` owns
the remaining test cleanup.

Backlog
[`LEGACY-025`](LEGACY-025-retire-core-inplace-function-test.md) — Retire
legacy Core.InplaceFunction test — retired on 2026-06-18 at maturity
`CPUContracted`. `tests/unit/core/Test_InplaceFunction.cpp` was removed because
`Core.InplaceFunction` has no promoted `Extrinsic.Core` endpoint and is
recorded in the parity matrix as legacy-only cleanup. Remaining legacy
runtime/graphics/RHI consumers are owned by their subtree cleanup tasks.
`LEGACY-005` remains blocked by 31 remaining test consumers and 133
legacy-internal consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-024`](LEGACY-024-retire-core-feature-catalog-tests.md) — Retire
legacy Core feature-catalog tests — retired on 2026-06-18 at maturity
`CPUContracted`. `tests/unit/core/Test_FeatureRegistry.cpp` and
`tests/unit/core/Test_SystemFeatureCatalog.cpp` were removed because
`CORE-002` retired the global feature catalog shape instead of promoting it
under `src/core`. Runtime/graphics/app legacy feature-registry consumers remain
owned by their subtree cleanup tasks. `LEGACY-005` remains blocked by 32
remaining test consumers and 133 legacy-internal consumers; `LEGACY-012` owns
the remaining test cleanup.

Backlog
[`LEGACY-023`](LEGACY-023-retire-core-commands-test.md) — Retire legacy
Core.Commands test — retired on 2026-06-18 at maturity `CPUContracted`.
`tests/unit/core/Test_CoreCommands.cpp` was removed because `CORE-002` retired
the legacy global core command service and `RUNTIME-102` owns the promoted
runtime/editor command-history endpoint. Promoted coverage remains in
`tests/contract/runtime/Test.EditorCommandHistory.cpp` and related runtime UI
tests. `LEGACY-005` remains blocked by 34 remaining test consumers and 133
legacy-internal consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-022`](LEGACY-022-core-framegraph-test-promoted.md) — Migrate
CoreFrameGraph test to promoted Core — retired on 2026-06-18 at maturity
`CPUContracted`. `tests/unit/core/Test.CoreFrameGraph.cpp` and
`tests/unit/core/Test.CoreFrameGraphTypeTokenHelper.cpp` now import promoted
`Extrinsic.Core.FrameGraph`, `Extrinsic.Core.Hash`, and
`Extrinsic.Core.Tasks` instead of the bare legacy aggregate `Core` module. The
test uses promoted `FrameGraph` construction/accessors and checks
`FrameGraph::Execute()` results. The focused core targets and `CoreFrameGraph`
CTest filter passed. `LEGACY-005` remains blocked by 35 remaining test
consumers and 133 legacy-internal consumers; `LEGACY-012` owns the remaining
test cleanup.

Backlog
[`LEGACY-021`](LEGACY-021-core-profiling-test-promoted.md) — Migrate profiling
test to promoted Core — retired on 2026-06-18 at maturity `CPUContracted`.
`tests/unit/core/Test.CoreProfiling.cpp` now imports promoted
`Extrinsic.Core.Telemetry` and `Extrinsic.Core.Hash` instead of bare legacy
`Core.Telemetry` / `Core.Hash`, preserving `ScopedTimer`, `TelemetrySystem`,
`TimingCategory`, and present-timing coverage with the `Test.<Name>.cpp`
convention. The focused core targets and profiling/telemetry CTest filter
passed. `LEGACY-005` remains blocked by 37 remaining test consumers and 133
legacy-internal consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-020`](LEGACY-020-core-tasks-test-promoted.md) — Migrate CoreTasks test
to promoted Core — retired on 2026-06-18 at maturity `CPUContracted`. The full
`tests/unit/core/Test_CoreTasks.cpp` scheduler, coroutine, `CounterEvent`,
wait-token, telemetry-export, and job lifetime coverage now imports promoted
`Extrinsic.Core.Tasks`, `Extrinsic.Core.Tasks.CounterEvent`, and
`Extrinsic.Core.Telemetry` as `tests/unit/core/Test.CoreTasks.cpp`. The smaller
legacy-suffixed promoted wrapper test was removed as duplicate coverage. The
focused core targets and `CoreTasks` CTest filter passed. `LEGACY-005` remains
blocked by 38 remaining test consumers and 133 legacy-internal consumers;
`LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-019`](LEGACY-019-strong-handle-test-promoted.md) — Migrate
StrongHandle test to promoted Core — retired on 2026-06-18 at maturity
`CPUContracted`. The full `tests/unit/core/Test_CoreHandle.cpp` coverage now
imports promoted `Extrinsic.Core.StrongHandle` as
`tests/unit/core/Test.CoreStrongHandle.cpp` and uses the exported
`StrongHandleHash` for unordered containers. The smaller legacy-suffixed
promoted wrapper test was removed as duplicate coverage. The focused core
targets and `StrongHandle` CTest filter passed. `LEGACY-005` remains blocked by
39 remaining test consumers and 133 legacy-internal consumers; `LEGACY-012`
owns the remaining test cleanup.

Backlog
[`LEGACY-018`](LEGACY-018-retire-interface-panel-registration-test.md) —
Retire legacy Interface panel-registration test — retired on 2026-06-18 at
maturity `CPUContracted`. The legacy-only
`tests/contract/ui/Test_PanelRegistration.cpp` consumer was removed from the
runtime test source list instead of migrated because `Interface::GUI` panel
registration is not a promoted endpoint; current promoted UI/app coverage lives
in `SandboxEditorUi` contract tests and the app-to-runtime-only dependency
test. `LEGACY-001` now has zero external test consumers and remains blocked by
six legacy-internal Graphics/Runtime files.

Backlog
[`LEGACY-017`](LEGACY-017-core-hash-test-promoted.md) — Retire duplicate legacy
CoreHash test — retired on 2026-06-18 at maturity `CPUContracted`. Duplicate
legacy `tests/unit/core/Test_CoreHash.cpp` coverage was deleted in favor of the
existing promoted `Extrinsic.Core.Hash` coverage, now named
`tests/unit/core/Test.CoreHash.cpp` with the `Test.<Name>.cpp` convention. The
affected core targets and `CoreHash` CTest filter passed. `LEGACY-005` remains
blocked by 40 remaining test consumers and 133 legacy-internal consumers;
`LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-016`](LEGACY-016-log-ring-buffer-test-promoted.md) — Migrate
LogRingBuffer test to promoted Core — retired on 2026-06-18 at maturity
`CPUContracted`. `tests/unit/core/Test.LogRingBuffer.cpp` now imports promoted
`Extrinsic.Core.Logging` instead of bare legacy `Core.Logging`, and the touched
independent test uses the `Test.<Name>.cpp` naming convention. The focused core
target and `LogRingBuffer` CTest filter passed. `LEGACY-005` remains blocked by
41 remaining test consumers and 133 legacy-internal consumers; `LEGACY-012`
owns the remaining test cleanup.

Backlog
[`LEGACY-015`](LEGACY-015-core-error-test-promoted.md) — Migrate CoreError test
to promoted Core — retired on 2026-06-18 at maturity `CPUContracted`.
`tests/unit/core/Test.CoreError.cpp` now imports promoted
`Extrinsic.Core.Error` instead of bare legacy `Core.Error`, and the touched
independent test uses the `Test.<Name>.cpp` naming convention. The focused core
target and `CoreError` CTest filter passed. `LEGACY-005` remains blocked by 42
remaining test consumers and 133 legacy-internal consumers; `LEGACY-012` owns
the remaining test cleanup.

Backlog
[`LEGACY-014`](LEGACY-014-runtimegraph-core-test-import.md) — Remove unused
RuntimeGraph legacy Core test import — retired on 2026-06-18 at maturity
`CPUContracted`. `tests/unit/geometry/Test_RuntimeGraph.cpp` no longer imports
bare legacy `Core`; the focused geometry target and `RuntimeGraph` CTest filter
passed. `LEGACY-005` remains blocked by 43 remaining test consumers and 133
legacy-internal consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-013`](LEGACY-013-promoted-core-import-migration.md) — Migrate
promoted Core imports off legacy modules — retired on 2026-06-18 at maturity
`CPUContracted`. Promoted geometry/runtime now import `Extrinsic.Core.*`
instead of bare legacy `Core.*`, promoted geometry no longer links
`IntrinsicCore`, and the promoted-src bare legacy import grep is clean. Four
directly affected geometry tests now consume promoted Core memory/error types;
`LEGACY-005` remains blocked by 44 remaining test consumers and 133
legacy-internal consumers. `LEGACY-012` owns the remaining test cleanup, while
legacy-internal consumers retire through the Runtime-first to Core-last subtree
deletion order.

Backlog
[`LEGACY-011`](LEGACY-011-src-legacy-feature-reimplementation-map.md) —
Value-gated legacy feature reimplementation map — retired on 2026-06-18 at
maturity `Scaffolded`. Every retained/deferred legacy feature candidate now has
a named done-task decision or an explicit future trigger, keeping semantic
reimplementation separate from the mechanical per-subtree deletion tasks.
Remaining legacy retirement work is consumer-grep cleanup in `LEGACY-012`,
the promoted-src Core import migration called out by the migration audit, and
mechanical Runtime-first to Core-last subtree deletion ordering.

Active
[`UI-018`](UI-018-sandbox-menu-first-ui.md) — Sandbox menu-first UI defaults —
retired on 2026-06-17 at maturity `CPUContracted`. The sandbox editor now
starts with only the main menu bar visible. Top-level panels open from `View`,
and PointCloud/Graph/Mesh domain windows remain closed until selected from
their menus, without changing panel models, command routing, or runtime
ownership.

Active
[`UI-014`](UI-014-uv-backend-and-texture-bake-controls.md) — UV backend and
texture bake controls — retired on 2026-06-17 at maturity `CPUContracted`.
The sandbox editor now exposes selected-mesh UV diagnostics, xatlas-backed UV
regeneration commands, property-catalog-driven bake source controls, generated
texture bake command routing, and live ImGui controls for preserve/regenerate
policy, atlas settings, target semantic, encoder, and output size. UI remains
headless-safe and routes geometry mutation, bake work, generated texture
payloads, and binding updates through runtime-owned command/history surfaces.

Active
[`UI-017`](UI-017-bound-render-state-inspector.md) — Bound render state
inspector — retired on 2026-06-17 at maturity `CPUContracted`. Selected mesh,
graph, point-cloud, and composition models now expose bound-state rows for
render lanes, presentation slots, defaults, property buffers, authored and
generated textures, readiness diagnostics, disabled command surfaces, and
derived-job/bake progress without UI storing renderer handles, raw property
pointers, worker state, or live asset-service references.

Active
[`RUNTIME-115`](RUNTIME-115-selected-mesh-bake-command-surface.md) — Selected
mesh bake command surface — retired on 2026-06-17 at maturity
`CPUContracted`. Runtime now owns a selected-mesh texture-bake command surface
that validates source entity/domain/property, encoder, UV availability,
resolution, generated texture key, and binding compatibility before work is
scheduled. Successful synchronous or derived-job bakes reload generated
`AssetTexture2DPayload` data through `AssetService` and optionally update
progressive presentation bindings through `EditorCommandHistory`; stale
derived-job applies are discarded deterministically.

Active
[`UI-016`](UI-016-geometry-property-catalog-and-binding-usability.md) —
Geometry property catalog and binding usability — retired on 2026-06-17 at
maturity `CPUContracted`. `Extrinsic.Runtime.SandboxEditorUi` now builds
selected-entity property catalogs for mesh vertex/edge/halfedge/face, graph
vertex/edge, and point-cloud point domains, including internal/connectivity,
canonical, user, generated, supported, and unsupported rows. Supported
scalar/label/vector properties show selected-value previews, and compatible
binding targets keep dimension/domain mismatches visible with deterministic
disabled reasons.

Active
[`BUG-045`](BUG-045-progressive-raw-mesh-uv-fallback.md) — Progressive raw mesh
surface UV fallback — retired on 2026-06-17 at maturity `CPUContracted`.
Raw mesh surface packing now falls back to zero GPU U/V values when imported
geometry has missing, mismatched, or non-finite `v:texcoord`, while extraction
still records UV fallback diagnostics. Dropped no-UV OBJ files now create a
mesh entity and upload a raw surface in the frame where the import event becomes
visible; deferred UV atlas generation and UV-dependent texture bakes still wait
for real resolved UVs. Existing direct import, dropped-file, progressive
model-scene, and close-path runtime regressions passed.

Backlog
[`GRAPHICS-090`](GRAPHICS-090-progressive-render-data-operational-smoke.md) —
Progressive render-data operational smoke — retired on 2026-06-16 at maturity
`Operational`. The promoted runtime sandbox GPU smoke now exercises a
progressive scene with mesh defaults/pending slots, a generated mesh texture
slot becoming ready, graph edge property-buffer presentation, unsupported and
previous-output-retained states, extraction diagnostics, and material texture
binding resolution counters. The `ci-vulkan` target built on this host and the
`gpu;vulkan` `ProgressiveRenderDataReachesOperationalFrame` test passed without
introducing live runtime/ECS/AssetService imports into graphics.

Backlog
[`UI-015`](UI-015-progressive-render-data-inspector.md) — Progressive render-data
inspector — retired on 2026-06-16 at maturity `CPUContracted`. The sandbox
editor inspector now exposes data-only progressive entity shape, lane/slot
state, compatible and incompatible property choices, slot default/property
commands routed through `EditorCommandHistory`, per-entity derived-job rows, and
composition child summaries. The UI remains a command/model consumer and does
not own geometry algorithms, asset IO, worker state, texture baking, or graphics
resources.

Backlog
[`RUNTIME-114`](RUNTIME-114-progressive-import-enrichment-pipeline.md) —
Progressive import enrichment pipeline — retired on 2026-06-16 at maturity
`CPUContracted`. Model-scene mesh leaves can now publish raw decoded geometry
immediately, attach progressive surface bindings, and queue observable
`StreamingExecutor`-backed UV atlas, vertex-normal, normal-bake, and albedo-bake
jobs through `DerivedJobRegistry`. Main-thread apply updates current ECS
properties and generated presentation descriptors only; generated texture upload
and material binding residency remain on the existing runtime texture handoff
path.

Backlog
[`RUNTIME-113`](RUNTIME-113-progressive-domain-presentation-extraction.md) —
Progressive domain presentation extraction — retired on 2026-06-16 at maturity
`CPUContracted`. Runtime extraction now consumes progressive descriptor
snapshots for mesh surface defaults/texture slots, mesh face-domain diagnostics,
graph vertex/edge property-buffer domains, point-cloud color/scalar/size/normal
descriptors, pending/failed/unsupported states, and previous-output retention
without blocking on derived jobs.

Backlog
[`RUNTIME-112`](RUNTIME-112-entity-derived-job-graph.md) — Entity derived-job
graph and snapshots — retired on 2026-06-16 at maturity `CPUContracted`.
Runtime now owns a `StreamingExecutor`-backed derived-job registry with stable
entity/domain/source/binding keys, explicit dependencies, deterministic
snapshots, follow-up scheduling, stale-result discard, cancellation/delete
handling, previous-output retention, main-thread apply, and fail-closed GPU
domain diagnostics.

Backlog
[`RUNTIME-111`](RUNTIME-111-progressive-render-data-descriptors.md) —
Progressive render-data descriptor contracts — retired on 2026-06-16 at
maturity `CPUContracted`. Runtime now has shared mesh/graph/point-cloud
presentation descriptors, slot/source/readiness/generated-output policy,
property compatibility diagnostics, and scene serialization for progressive
bindings while excluding raw property pointers, transient jobs, and GPU handles.

Backlog
[`RUNTIME-110`](RUNTIME-110-progressive-entity-render-data-pipeline.md) —
Progressive entity render-data pipeline clarification — retired on 2026-06-16
at maturity `Scaffolded`. The accepted planning contract makes mesh, graph,
and point-cloud leaves equal first-class render-data domains; separates
render-lane intent components from per-entity presentation bindings; records
stable property/slot/generated-output descriptors instead of raw property
pointers or GPU handles; and defines asynchronous derived jobs with dependency
visibility, stale-result discard, previous-output retention, and main-thread
apply. ADR-0021 now captures the hard-to-reverse architecture decision.
Implementation is split into `RUNTIME-111` descriptor contracts,
`RUNTIME-112` entity derived-job graph, `RUNTIME-113` progressive extraction,
`RUNTIME-114` import enrichment, `UI-015` inspector/debug visibility, and
`GRAPHICS-090` opt-in backend smoke.

Active
[`BUG-044`](BUG-044-runtime-import-postprocess-queue.md) — Runtime mesh import
post-process queue — retired on 2026-06-16 at maturity `CPUContracted`.
Direct mesh imports now publish decoded raw geometry before derived
materialization work. Missing normals, missing/invalid UV resolution, atlas
generation, and generated normal texture baking run on `Runtime.StreamingExecutor`
and apply back to the same ECS entity on the main thread with geometry dirty
tags. The import result reports the mesh entity immediately with zero generated
texture counts, while a later frame resolves finite texcoords/normals and
registers the generated normal material binding. Focused direct import,
model-scene, dropped-file, and mesh-normal runtime contract tests passed.

Active
[`ASSETIO-008`](ASSETIO-008-default-uv-atlas-materialization.md) — Default UV
atlas materialization for imported meshes — retired on 2026-06-16 at maturity
`CPUContracted`. Runtime mesh materialization now validates authored UVs through
the `Geometry.UvAtlas` contract, preserves valid authored coordinates by
default, and invokes the xatlas-backed default atlas backend when UVs are
missing or invalid before ECS population and generated texture bakes. The
materialization options expose preserve/regenerate policy, atlas resolution,
padding, texels-per-unit, required-vs-optional UV failure behavior, and a
replaceable backend for tests/future algorithms. Seam-split output preserves
normals, colors, scalar/vector fields, and `v:source_vertex` / `f:source_face`
provenance; diagnostics distinguish authored-preserved vs generated UVs,
invalid authored UVs, backend failures, seam splits, chart count, and atlas
dimensions. Direct mesh imports and model-scene handoff generated normal/albedo
bakes now use resolved UVs instead of skipping solely because the source omitted
UVs. Focused runtime/UV atlas tests, module inventory regeneration, strict
layering/test-layout/task checks, and doc-link checks passed.

Active
[`GEOM-025`](GEOM-025-uv-atlas-backend-xatlas.md) — UV atlas backend contract
and xatlas default — retired on 2026-06-16 at maturity `CPUContracted`.
Geometry now exposes `Geometry.UvAtlas`, a backend-neutral UV atlas API with
authored-UV validation/preservation, explicit failure/provenance diagnostics,
source-vertex/source-face xrefs, seam-aware `MeshSoup::IndexedMesh` output,
GEOM-018 quality metrics, and a caller-supplied backend replacement seam. The
default CPU backend is pinned to `jpcy/xatlas` through the repository vcpkg
overlay port and linked privately by geometry; no runtime/assets/graphics/ECS
layer imports or public xatlas headers were introduced. Focused geometry tests,
`IntrinsicTests` target build, module inventory regeneration, strict layering
and test-layout checks, task checks, and doc-link checks passed.

Active
[`GEOM-018`](GEOM-018-parameterization-distortion-map-quality-diagnostics.md)
— Parameterization distortion and map-quality diagnostics — retired on
2026-06-16 at maturity `CPUContracted`. Geometry now exposes
`Geometry.Parameterization.Diagnostics`, a deterministic CPU diagnostics
surface for mesh positions plus per-vertex UVs that reports explicit invalid
input counts, flipped elements, conformal/area/symmetric-Dirichlet/stretch
metrics, boundary length distortion, and seam-discontinuity placeholders.
Existing LSCM quality summaries are populated through the shared evaluator, and
the smoke benchmark runner now emits
`geometry.parameterization.diagnostics.smoke`. Focused geometry tests,
benchmark validation, module inventory regeneration, strict layering/test
layout/task checks, and doc-link checks passed.

Active
[`ASSETIO-005`](ASSETIO-005-asset-import-queue-progress.md) — Asset import
queue and progress UI — retired on 2026-06-16 at maturity `Operational`.
Runtime now exposes stable AssetIO queue snapshots over the promoted ingest
state machine, including operation identity, source/path metadata, coarse
queued/running/apply/upload/terminal stages, timestamps, determinate or
indeterminate progress, diagnostics, cancellation, and clear-completed
behavior. `Engine` owns snapshot polling plus command routing, while the
sandbox editor's File / Import window consumes data-only rows and does not own
asset, ECS, graphics, or worker-thread state. Focused runtime/UI queue coverage,
the default CPU-supported CTest gate, and strict layering/task/docs checks
passed.

Active
[`GRAPHICS-088`](GRAPHICS-088-resolved-uv-rendering-and-bake-residency.md) —
Resolved UV rendering and bake texture residency — retired on 2026-06-15 at
maturity `CPUContracted`. Graphics now treats packed mesh UVs as resolved
texture coordinates for surface material sampling, generated normal/albedo
bindings, the `Material.DefaultDebugUVs` checker material, and UV-backed
fragment-bake descriptors. The generic `RUNTIME-109` bake contract is consumed
through data-only generated texture semantics and source dirty stamps for
scalar, label, vector, standard material, and displacement-intent bake
descriptors; graphics still does not generate UVs or import runtime, ECS,
`AssetService`, geometry backends, or `xatlas`. Operational generated-UV Vulkan
sampling proof is deferred to `GRAPHICS-089` after `ASSETIO-008`.

Backlog
[`RUNTIME-109`](RUNTIME-109-extensible-mesh-attribute-texture-bakes.md) —
Extensible mesh attribute texture bake pipeline — retired on 2026-06-15 at
maturity `CPUContracted`. Runtime now exposes a generic CPU mesh attribute
texture bake request over resolved UVs for vertex and face source domains,
finite scalar float/double, label `uint32`, and `glm::vec2`/`glm::vec3`/
`glm::vec4` properties. Encoders cover scalar colormap, linear scalar,
label palette, vector2, vector3, normal, and RGBA outputs, while the existing
generated normal/albedo helpers remain wrappers over the generic seam. Stable
generated texture keys omit dirty stamp so rebakes target reload of the
intended CPU payload instead of minting unbounded generated assets. The baker
does not generate UVs; missing-UV import materialization remains under
`ASSETIO-008`, and operational renderer/Vulkan proof remains under
`GRAPHICS-088`. Focused runtime bake coverage passed.

Active
[`RUNTIME-101`](RUNTIME-101-asset-ingest-state-machine.md) — Asset ingest
state-machine migration — retired on 2026-06-15 at maturity `CPUContracted`.
Runtime now owns a backend-neutral ingest request/result state machine for
manual imports, dropped files, and reimport over promoted `AssetService`,
`Asset.ImportRouter`, `Runtime.StreamingExecutor`, and existing materialization
handoffs. `Engine::ImportAssetFromPath(...)`, synchronous dropped non-geometry
imports, deferred dropped-geometry main-thread apply, and
`Engine::ReimportAsset(...)` share deterministic diagnostics, duplicate active
request suppression, and stale completion guards. Reimport reloads the same
`AssetId` transactionally through `AssetService` and does not recreate
standalone geometry entities or revive scene-file `AssetSourceRef` coupling.
Focused runtime/import coverage and strict docs/task/layering checks passed.

Backlog
[`RUNTIME-107`](RUNTIME-107-headless-engine-loop-coverage.md) —
Headless-capable `Engine::Run()` loop coverage — retired on 2026-06-15 at
maturity `Operational`. `Core::Config::WindowConfig` now exposes an explicit
`WindowBackend` selector: `Configured` preserves the CMake-selected platform
backend, while `Null` routes `Platform::CreateWindow` to the deterministic
headless backend that is always compiled. The BUG-030 `Engine::Run()`
regressions now set `WindowBackend::Null`, so viewport click selection,
inspector transform flush, platform/drop import, and close-event assertions
execute on displayless hosts instead of skipping. Configured GLFW windows that
initialize already closed log a runtime zero-frame warning and still do not
fall back to Null silently. Focused runtime contract/integration coverage
passed 7/7 with no skips.

Backlog
[`RUNTIME-103`](RUNTIME-103-geometry-algorithm-execution-queue.md) —
Geometry algorithm execution queue — retired on 2026-06-15 at maturity
`CPUContracted`. The value gate found that current promoted editor workflows
do not justify a runtime async geometry algorithm queue: `UI-004` already
routes CPU K-Means over mesh vertices, graph nodes, and point-cloud points
through one deterministic synchronous `SandboxEditorUi` command that publishes
label/color properties, stamps `DirtyVertexAttributes`, and fails closed for
invalid targets or inputs. No request/result/cancellation/progress queue API
or CUDA follow-up is owed for current workflows. Future asynchronous
scheduling, centroid entities, topology mutation, broader algorithms, or
compute backends require new value-gated tasks with concrete consumers.
Focused K-Means/SandboxEditorUi runtime coverage and strict task/docs/layering
checks passed.

Backlog
[`RUNTIME-105`](RUNTIME-105-remove-streaming-graph-bridge.md) — Remove the
deprecated `GetStreamingGraph()` TaskGraph bridge — retired on 2026-06-15 at
maturity `Retired`. The promoted runtime no longer exports
`Engine::GetStreamingGraph()`, no longer owns a private streaming
`TaskGraph`, and no longer converts per-frame graph passes into
`StreamingExecutor` tasks during maintenance. `Runtime.Engine` now owns only
the persistent `StreamingExecutor` path for async asset IO / geometry
processing work, and `src/runtime/README.md` documents that current state.
The `RuntimeEngineLayering` source-inspection harness was corrected to inspect
`Core.FrameLoop.cpp`, where the promoted frame-loop implementation lives, so
the existing runtime layering prefix covers the bridge deletion. Focused
runtime frame-loop, streaming-executor, and layering prefixes passed, and the
default CPU-supported CTest gate passed.

Active
[`INFRA-001`](INFRA-001-vcpkg-manifest-mode.md) — Move third-party
dependencies to a vcpkg manifest — retired on 2026-06-15 at maturity
`Operational`. The build now resolves third-party C/C++ packages through the
root `vcpkg.json` manifest, the repository-local vcpkg toolchain, and
repository overlay ports. The retired FetchContent fallback, dependency-cache
knobs, and `external/cache` developer flow are gone from current build/tooling
docs. CI workflows bootstrap vcpkg, restore `external/vcpkg-bincache/`, export
`VCPKG_BINARY_SOURCES`, and time cache-backed configure with
`tools/ci/time_command.py`; exact primary-key cache hits over 10 s fail the
configure step. Local CPU, Vulkan, headless, raw-IDE, and fresh-clone checks
passed. Final GitHub Actions evidence came from `ci-linux-clang` run
`27533474526`, job `81376962604`: exact primary-key cache hit and
`Configure (ci preset) elapsed: 8.271 s`, with the job concluding success.

Active
[`BUG-043`](BUG-043-dropped-obj-missing-uvs-invisible.md) — Dropped OBJ without
UVs loads but is invisible — retired on 2026-06-14 at maturity
`CPUContracted`. Runtime mesh materialization now preserves valid authored
`v:texcoord` and writes deterministic finite projection fallback UVs when
imported OBJ/model-scene mesh payloads omit or invalidate texture coordinates.
The fallback runs before direct ECS materialization, model-scene handoff, and
generated attribute texture bakes, so render extraction can upload the mesh
surface instead of failing closed with `MeshGeometryMissingTexcoords`. The
renderer packer remains strict: surface `MeshVertex::U/V` still comes only from
`v:texcoord`, never oct-encoded normals or shader-side fabrication. Focused
runtime contract tests, adjacent mesh/import coverage, and the full
CPU-supported CTest gate passed; xatlas-quality default atlas work remains
owned by `ASSETIO-008` and `GEOM-025`.

Backlog
[`RUNTIME-108`](RUNTIME-108-resolved-uv-render-residency.md) — Remove mesh UV
normal fallback — retired on 2026-06-13 at maturity `CPUContracted`. Runtime
mesh surface packing now treats `MeshVertex::U/V` as texture coordinates only:
`PackMesh` and `BuildSurfaceTriangleFaceMap` require count-matched finite
`v:texcoord`, report `MissingTexcoords` or `NonFiniteTexcoord` for invalid
inputs, and extraction records matching counters while skipping unrenderable
surface uploads. Reference/procedural meshes and runtime test fixtures now
author UVs. Generated atlas/materialization remains with `ASSETIO-008` and
`GEOM-025`, renderer operational proof remains with `GRAPHICS-088`, and generic
texture-bake expansion remains with `RUNTIME-109`.

Active
[`ASSETIO-007`](ASSETIO-007-direct-mesh-generated-normal-texture.md) —
Direct mesh generated normal texture binding — retired on 2026-06-13 at
maturity `CPUContracted`. Direct mesh imports now use the same default normal
policy as model-scene imports: authored `v:normal` vectors are preserved when
present and area-weighted unit normals are synthesized when absent. When the
CPU mesh also has matching `v:texcoord`, runtime bakes that `v:normal`
property into a generated normal texture asset, requests texture upload, and
registers a data-only `MaterialTextureAssetBindings` record keyed by stable
render id. `RenderExtractionCache` resolves the binding onto the
extraction-owned material sidecar during extraction, keeping ECS free of
graphics handles. Meshes without bakeable texture coordinates still import and
render through the existing material fallback with CPU normals intact. Focused
runtime contract tests passed, `IntrinsicTests` built, and the default
CPU-supported CTest gate passed.

Active
[`GRAPHICS-087`](GRAPHICS-087-vertex-color-property-texture-bake.md) —
Bake vec3/vec4 vertex color properties to surface albedo textures — retired
on 2026-06-12 at maturity `CPUContracted`. Runtime now exposes the shared
`Extrinsic.Runtime.MeshAttributeTextureBake` helper for finite mesh vertex
`glm::vec3`/`glm::vec4` properties with `v:texcoord`, preserves typed vertex
properties through asset mesh materialization, and lets model-scene handoff
create generated albedo child texture assets when authored base-color textures
are absent. Generated albedo textures route through the existing texture
upload/material binding path and surface shaders consume them via
`MaterialParams::AlbedoID`. Focused runtime/graphics/asset tests passed,
`IntrinsicTests` built, and the default CPU-supported CTest gate passed.

Active
[`BUG-042`](BUG-042-point-sphere-impostor-depth.md) — Promoted impostor
spheres do not intersect surfaces correctly — retired on 2026-06-12 at
maturity `CPUContracted`. The promoted retained point path now matches the
legacy shape: the `Points` cull bucket emits six vertices per source point,
the forward point pipeline is triangle-list with depth writes enabled, and
sphere mode reconstructs the front sphere surface in view space before writing
corrected `gl_FragDepth`. Point selection remains on the unexpanded
`SelectionPoints` bucket. Focused renderer lifecycle/selection regressions
passed, including `ForwardPointSphereImpostorsWriteCorrectedDepth`, and the
default CPU-supported CTest gate passed.

Active
[`ASSETIO-006`](ASSETIO-006-generated-normal-map-bake.md) — Generated
normal-map bake from mesh vertex normals — retired on 2026-06-12 at maturity
`CPUContracted`. Runtime now bakes generated linear RGBA8 normal textures from
named mesh vertex `glm::vec3` properties plus `v:texcoord`, preserves decoded
texture coordinates through asset mesh materialization, and creates generated
normal child assets for model-scene materials that lack authored normal maps.
The generated texture uses the existing texture handoff/material binding lane,
so shaders consume it through `MaterialParams::NormalID` without asset or
graphics layer ownership inversions. Focused bake/handoff regressions and the
default CPU-supported CTest gate passed.

Backlog
[`BUG-041`](BUG-041-asset-mesh-vertex-normals.md) — Asset mesh vertex normals
are lost during runtime materialization — retired on 2026-06-12 at maturity
`CPUContracted`. Geometry/model decoders already produced `v:normal` payloads
for formats that supplied normals, but both runtime halfedge materialization
paths rebuilt meshes from positions and face indices only, dropping the normal
property before ECS `GeometrySources` population. Runtime now shares
`BuildRuntimeHalfedgeMeshWithNormals(...)` across direct mesh imports and
model-scene primitive handoff: explicit per-vertex normals are copied, missing
source normals are filled with deterministic area-weighted unit normals, and
the direct-import renderable fallback preserves the same normal data when
strict shared topology fails only for renderable non-manifold/winding
diagnostics. `MeshGeometryPacker` also encodes available mesh normals into the
existing 20-byte surface vertex layout's U/V channel. Focused runtime
regressions cover explicit OBJ normals, computed fallback normals, model-scene
handoff, and packer output; the default CPU gate passed.

Backlog
[`BUG-040`](BUG-040-orbit-camera-vertical-drag-sign.md) — Orbit camera
vertical drag sign — retired on 2026-06-12 at maturity `CPUContracted`. The
`BUG-039` quaternion orbit fix preserved the legacy algebraic `-yDelta` pitch
sign, but in the promoted app's screen-space input convention Y grows downward.
That made a mouse-up drag place the camera below the target and point upward;
mouse-down did the inverse. A new runtime controller regression red-gated the
small-drag sign with `Position.y == -0.62373507` and `Forward.y == 0.2079117`
for mouse-up. Orbit pitch now applies `+yDelta` around the camera-local right
axis; fly/free-look signs are unchanged. The focused sign regression,
`RuntimeCameraControllers` suite, and default CPU gate passed.

Backlog
[`BUG-039`](BUG-039-orbit-camera-rotation-lock.md) — Orbit camera rotation
lock — retired on 2026-06-12 at maturity `CPUContracted`. The promoted orbit
controller had reused scalar yaw/pitch state and a fixed world-up view, so a
large vertical drag clamped at the pitch pole instead of continuing like the
legacy trackball camera. Orbit now stores accumulated orientation derived from
the seed forward/up vectors, applies drag deltas as quaternion rotations around
the current camera-local up/right axes, derives view forward/up from that
orientation, and keeps existing radius, zoom, focus, yaw diagnostic, and WASD
panning behavior. The new runtime contract regression red-gated the lock with
`Forward.z == -0.0174523834` and `Up.y == 1`, then passed after the quaternion
orbit fix; the full camera-controller suite and default CPU gate passed.

Backlog
[`BUG-038`](BUG-038-sandbox-dropped-file-diagnostics.md) — Dropped file
imports fail silently in the sandbox — retired on 2026-06-12 at maturity
`CPUContracted`. The event path itself was already wired: focused contracts
showed runtime platform-drop dispatch and valid dropped OBJ/OFF imports reach
the import/materialization path, so asset IO, generic IO, GPU upload, and
renderer visibility were downstream of the reported silence. The actual bug was
observability at the runtime boundary: dropped imports recorded last-import
state for the editor panel but did not log receipt, route/queue decisions, or
completion. `Engine` now logs file-drop receipt, empty-path rejection,
geometry-vs-synchronous import routing, successful streaming queue submission,
queue-submission rejection, and shared import success/failure from
`RecordAssetImportEvent`. A new runtime contract regression red-gated a missing
OBJ drop with no logs, then proved receipt/queue/failure breadcrumbs plus a
failed `RuntimeAssetImportEvent` with payload `Mesh` and `FileNotFound`.

Active
[`BUG-037`](BUG-037-window-close-stale-run-state.md) — Window close can leave
runtime running — retired on 2026-06-12 at maturity `CPUContracted`. The
runtime close path now normalizes native `IWindow::ShouldClose()` exits through
`Engine::RequestExit()` when `Engine::Run()` leaves its outer loop, closing the
state gap where a platform/native close flag could end the loop while
`Engine::IsRunning()` still reported true. `RunFrame()` continues to handle
close before renderer work when the flag is observed at the platform-frame
boundary. New runtime/ImGui wiring regressions cover native close before the
first frame and native close after representative camera/UI/selection input,
red-gating the stale run-state bug before the fix and passing after the runtime
state normalization. The default CPU-supported correctness gate passed.

Active
[`BUG-036`](BUG-036-ui-input-capture-leak.md) — UI-captured input leaks into
engine controls — retired on 2026-06-12 at maturity `CPUContracted`. Dear
ImGui capture state is now surfaced through `ImGuiAdapter` for both mouse and
keyboard input, and `Engine::RunFrame()` samples that state once after the UI
frame before routing runtime input consumers. Mouse capture continues to block
viewport-selection picks, while mouse or keyboard capture suppresses camera
controller updates and transform-gizmo input for the frame without mutating the
platform raw input context. Focused contract coverage red-gated the leak by
forcing Dear ImGui capture while raw `W`, Shift, and mouse input were present;
the fixed path leaves runtime camera, gizmo, and selection consumers idle under
UI capture and keeps existing behavior when the UI does not capture input.

Backlog
[`RUNTIME-106`](RUNTIME-106-render-component-domain-composition.md) — Render
component domain composition — retired on 2026-06-12 at maturity
`CPUContracted`. Mesh, graph, and point-cloud rendering now share the promoted
user-facing composition contract: `GeometrySources::BuildConstView(...)`
selects the geometric domain, while `RenderSurface`, `RenderEdges`, and
`RenderPoints` component presence selects render lanes supported by that domain.
Mesh `RenderEdges` and `RenderPoints` reuse the existing runtime primitive-view
sidecars directly from ECS components, so mesh wireframe and vertex rendering no
longer require `RenderSurface` or `MeshPrimitiveViewSettings`. The legacy
primitive-view editor/engine command surfaces translate to `RenderEdges` /
`RenderPoints` for compatibility, and extraction no longer treats the settings
map as authority. Graph lane toggles remain on the shared graph residency handle
but repack deterministically on component changes, and point-cloud
`RenderSurface` / `RenderEdges` requests fail closed with diagnostics and no
stale point residency. Focused CPU/null coverage proves mesh edge-only,
point-only, and combined lanes, graph lane toggles, point-cloud unsupported
lanes, UI render-hint command routing, scene serialization of `edges`, and
engine compatibility translation; the default CPU gate remains the retirement
gate and no `Operational` follow-up is owed by default.

Backlog
[`BUG-028`](BUG-028-mesh-primitive-view-ui-rendering.md) — Mesh primitive view
UI toggles do not render — retired on 2026-06-11 at maturity `CPUContracted`.
The promoted mesh edge/vertex view path is runtime extraction-cache sidecar
state, not legacy ECS `MeshEdgeView` / `MeshVertexView` components. The fix
extends `MeshPrimitiveViewSettings` with vertex style/radius, exposes those
controls through sandbox UI commands and command history, writes retained point
`GpuEntityConfig::PointMode` / `PointSize` for the derived vertex sidecar every
frame, derives edge-view wireframes from halfedge/face topology when explicit
edge rows are absent, and updates the forward point shader to draw flat
circles, screen-space sphere impostors, and normal-aligned surfel ellipses from
the shared UV normal payload. Mesh vertex views compute face-area weighted
normals from promoted halfedge/face topology, point clouds forward `v:normal`
when present, and graph nodes carry the no-normal sentinel. Focused CPU/null
tests prove the UI command path, edge/vertex sidecar extraction, derived
wireframe fallback, OBJ mesh primitive views, config reuse updates, and shader
compilation; broader GPU screenshot proof remains in the working-sandbox
acceptance lane.

Backlog
[`UI-013`](UI-013-domain-render-hint-controls.md) — Sandbox EditorUI domain
render hint controls — retired on 2026-06-11 at maturity `CPUContracted`.
Promoted mesh, graph, and point-cloud rendering paths were already present in
runtime extraction and renderer passes; this slice closed the editor workflow
gap by adding `ApplySandboxEditorRenderHintCommand(...)`, typed render-hint
domain-window model fields, and ImGui controls for selected-domain
`RenderSurface`, `RenderEdges`, and `RenderPoints` components. Commands are
undoable through `EditorCommandHistory` when available, graph edge-lane edits
force runtime graph residency to repack, and uniform retained-point radius/type
settings now flow through `VisualizationSyncRecord` into `GpuEntityConfig`.
Retained-line per-entity width rasterization remains renderer-owned future
work; this slice stores the promoted component value and keeps graphics free of
live ECS reads.

Backlog
[`BUG-026B`](BUG-026B-vulkan-click-pick-readback-smoke.md) — Vulkan
click-pick readback smoke — retired on 2026-06-11 at maturity `Operational`.
The opt-in `gpu;vulkan` runtime sandbox smoke now waits for the promoted Vulkan
device to become operational, submits a real `SelectionController::RequestClickPick`
at the projected center of `ReferenceTriangle`, and verifies the GPU readback
selects the triangle through the runtime controller rather than the hierarchy
selection shortcut. The smoke asserts `Engine::GetLastRefinedPrimitiveSelection()`
reports a mesh face hit with resolved face/edge/vertex IDs plus a depth-derived
world/local cursor on the triangle plane, then submits a far-background click
and verifies the no-hit readback clears selection and the refined primitive
cache. The run passed on NVIDIA RTX 3050 / NVIDIA driver 590.48.01, upgrading
the BUG-026 fix to `Operational`.

Backlog
[`GRAPHICS-086`](GRAPHICS-086-rhi-retirement-parity-and-cuda-decision.md) —
RHI retirement parity and CUDA decision — retired on 2026-06-11 at maturity
`CPUContracted`. The audit maps legacy `RHI.CommandUtils`,
`RHI.PersistentDescriptors`, `RHI.Swapchain`/`RHI.Image`, and
`RHI.SceneInstances` to promoted `ICommandContext`/`ITransferQueue`/submit-plan
seams, backend-local Vulkan descriptor/swapchain/image ownership,
backend-neutral RHI handles/descriptors/present modes, and renderer-owned
`GpuWorld`/`RHI::GpuInstanceData` state. CUDA is removed from the promoted
default path because no current runtime, graphics, method, or benchmark
consumer needs it; future CUDA must open a new opt-in method/backend task with a
concrete workload and verification plan. `LEGACY-009` is now blocked by
consumer-grep/subtree ordering rather than an unnamed RHI/CUDA parity gap.

Backlog
[`GRAPHICS-084C`](GRAPHICS-084C-visualization-property-buffer-vulkan-smoke.md) —
visualization property-buffer Vulkan smoke — retired on 2026-06-11 at maturity
`Operational`. The existing visualization-overlay GPU smoke now submits
graphics-owned property-buffer upload descriptors for vector-field position and
vector arrays, verifies `RenderGraphFrameStats::VisualizationPropertyBuffers`
accepted/uploaded both descriptors without deferral or resource errors, and
records `VisualizationOverlayPass` on the promoted Vulkan path only after
packet BDA publication succeeds. The task keeps runtime/ECS out of graphics;
`GRAPHICS-086` later retired the broader RHI/CUDA audit.

Backlog
[`GRAPHICS-084`](GRAPHICS-084-visualization-property-buffer-residency.md) —
visualization property-buffer residency — retired on 2026-06-11 at maturity
`CPUContracted`. Runtime visualization adapters now emit copied CPU property
arrays as `VisualizationPropertyBufferUploadDescriptor` records when external
BDAs are absent. The renderer copies descriptor payloads into retained snapshot
storage, validates supported scalar/color/vector descriptors centrally, uploads
or reuses renderer-owned `RHI::BufferManager` storage buffers, publishes BDAs
into scalar/color/vector/isoline packets before `ValidateVisualizationPackets`
runs, and reports diagnostics for unsupported types, invalid shape,
non-finite values, stale dirty stamps, upload deferral, and invalid resources.
Runtime/UI stay data-only and do not own GPU resources. The opt-in Vulkan
operational proof was retired by `GRAPHICS-084C`; this retirement does not
claim a fresh `gpu;vulkan` host run.

Backlog
[`GRAPHICS-085`](GRAPHICS-085-overlay-packet-backend-parity.md) — overlay
packet backend parity — retired on 2026-06-11 at maturity `CPUContracted`.
The task composes the retained overlay-like backend lanes classified by
`RUNTIME-104` without adding a runtime/editor overlay creation API or a new
graphics packet class. A new graphics contract test submits transient debug
triangle, line, and point packets together with visualization vector-field and
isoline packets in one frame, then proves both `TransientDebugSurfacePass` and
`VisualizationOverlayPass` record with per-lane submitted/recorded diagnostics
and no missing-pipeline skips. Selectable overlay-like workflows remain covered
by ordinary renderable and primitive-view selection/outline snapshots; packet-
only visualization overlays remain visual-only because no immutable selection
metadata is added. Existing opt-in transient-debug and visualization-overlay
`gpu;vulkan` smokes remain the operational evidence path, but this retirement
does not claim a fresh Vulkan host run.

Backlog
[`RUNTIME-104`](RUNTIME-104-derived-overlay-producer-lifecycle.md) — derived
overlay producer lifecycle — retired on 2026-06-11 at maturity
`CPUContracted`. The value gate found no current promoted workflow requiring a
new persistent runtime overlay producer API. Legacy mesh/graph/point child
overlays are represented by ordinary `GeometrySources` entities when runtime/UI
imports or authors data; mesh edge/vertex overlays use component-driven
runtime-owned primitive-view sidecars; transient line/point/triangle overlays remain on
transient debug packets; vector-field and isoline overlays remain data-only
visualization packets emitted by `Runtime.VisualizationAdapters`. The
vector-field packet path is covered by runtime extraction regression coverage
and creates no child ECS entity, so the legacy parent/child cleanup invariant is
satisfied for current workflows without graphics importing ECS or storing RHI
handles in components. Backend command-shape proof remains open under
`GRAPHICS-085`; selected property-buffer residency remains `GRAPHICS-084`.

Backlog
[`BUG-027`](BUG-027-sandbox-dragdrop-close-mesh-views.md) — sandbox
drag/drop, close, and mesh primitive-view regression — opened and retired on
2026-06-11 at maturity `CPUContracted`. The reported sandbox path had three
runtime-wiring failures: direct platform close events reached the engine
listener but were ignored, the live frame loop polled the X-button close event
and continued into ImGui/render work before re-checking `ShouldClose()`,
dropped/direct standalone geometry imports materialized entities without
selecting them, and the promoted mesh primitive-view UI therefore had no
selected mesh to control after drag/drop. The fix wires `WindowCloseEvent` to
`Engine::RequestExit()`, delegates the `RunFrame()` platform phase to
`Core::ExecutePlatformBeginFrameContract(...)` so a close observed during
`PollEvents()` returns before renderer work, carries the materialized entity
handle out of standalone mesh/graph/point-cloud import, and selects that entity
after geometry import and camera focus. Regression coverage replays
`WindowDropEvent`/`WindowCloseEvent` through the runtime platform-event handler,
imports OBJ and OFF meshes through `Engine::Run()`, proves the imported mesh is
the active selection, drives the promoted primitive-view command surface,
asserts edge/vertex view uploads through `RenderExtractionCache`, and pins the
close-button timing with frame-loop/layering contracts. A narrow
`Engine::DispatchPlatformEventForTest(...)` seam exists only to replay platform
events through the same handler installed as the live window listener.

Active
[`BUG-026`](BUG-026-click-pick-readback-entity-zero-and-depth.md) — viewport
click selection dead: render-id zero collision, UINT clear punning, and
missing depth readback — opened and retired on 2026-06-10 at maturity
`CPUContracted`. Clicking in the sandbox selected nothing because two
defects stacked: (1) the render id written to the GPU instance table was the
raw `entt::entity` cast, so the default `ReferenceTriangle` (first entity of
a fresh registry, handle 0) collided with the picking drain's
`EntityId == 0` background sentinel and every click on it published NoHit;
(2) `PickingPass` cleared its `R32_UINT` ID targets with the scene-color
light-blue float clear, which the Vulkan backend bit-punned into
`0x3DCCCCCD`, so background clicks published phantom hits silently rejected
as stale. Fixed by centralizing the render-id convention as
`entt handle + 1` (`StableEntityLookup::ToRenderId`, 0 reserved for
background, `entt::null` wraps to 0) across extraction / selection /
refinement / gizmo packets, dedicating a zero-clear attachment pair to the
ID targets, and making the Vulkan backend value-convert clear colors for
integer formats (`ToVkClearColorValue`). The same task added the missing
depth readback the original selection design called for: 16-byte
`Picking.Readback` slots now carry a `SceneDepth` pixel sample, the drain
publishes `HasDepth`/`Depth` + the request pixel, `Engine` captures a
per-`Sequence` pick context (inverse view-projection, viewport, pick ray,
pixel-radius scale) and replays it on readback consume, and
`RefinePickReadbackResult` unprojects the cursor (`UnprojectPickDepth`),
reports it in world + entity-local space (`CursorFromDepth`, `WorldCursor`,
`LocalCursor`), anchors the closest-vertex/edge/face refinement with it, and
feeds the ray fallback for hint-less hits (pixel radius scaled by hit
distance under perspective; kept at the depth-invariant pixel footprint
under orthographic cameras such as the top-down controller — review
follow-up, 2026-06-11). Why the gates
missed it: CPU contracts seeded readback bytes directly (never entity 0,
never the real clear), and the `gpu;vulkan` smokes exercised hierarchy
selection, which bypasses the readback path. 12 new regression tests lock
the conventions; `Operational` (real Vulkan click round trip) owned by
`BUG-026B`.

Backlog
[`LEGACY-002`](LEGACY-002-seed-src-legacy-retirement-backlog.md) — seed
retirement tasks for remaining `src/legacy/` subtrees — retired to
`tasks/done/` on 2026-06-10. The deliverables had been complete since
2026-06-06 (the `LEGACY-003..010` per-subtree deletion tasks, the
architecture README "Legacy retirement" section with dependency-ordered
hints, and the `docs/migration/legacy-retirement.md` sequencing links);
the file stayed in backlog only because ~54 layering-allowlist rows still
named `LEGACY-002` as their open umbrella owner. `HARDEN-082` (the
metadata-only rebinding follow-up the task's context required) moved
those rows to their per-subtree owners, so no allowlist row references
`LEGACY-002` and the seed retires. Remaining legacy retirement is owned
by `LEGACY-001` and `LEGACY-004..010`, each gated on its consumer-grep
prerequisite.

Backlog
[`HARDEN-082`](HARDEN-082-rebind-legacy-allowlist-umbrella-rows.md) —
rebind legacy allowlist umbrella rows to per-subtree owners — opened and
retired to `tasks/done/` on 2026-06-10 at maturity `Retired`
(metadata-only governance rebind). All 54
`tools/repo/layering_allowlist.yaml` rows still naming the `LEGACY-002`
seeding umbrella moved to their concrete per-subtree retirement owners by
`file_glob` prefix (9 rows each to `LEGACY-004` Asset, `LEGACY-005` Core,
`LEGACY-006` ECS, `LEGACY-008` Graphics, `LEGACY-009` RHI, `LEGACY-010`
Runtime), with each row's `expires` text rewritten from the satisfied
"until LEGACY-002 seeds ..." condition to "until LEGACY-00N deletes ...".
No rows were added or removed and no glob changed; the strict layering
check stays green with the allowlisted-violation count unchanged at 1187.
This is the rebinding follow-up that `LEGACY-002`'s context required
before the seed itself could retire.

Backlog
[`HARDEN-078`](HARDEN-078-track-untracked-todo-temporary-markers.md) —
track or resolve untracked TODO / temporary markers in promoted src —
retired to `tasks/done/` on 2026-06-10 at maturity `Retired` (pure marker
hygiene). The `Core.Filesystem` dead commented `CallbackRegistry` import
and bare TODO were resolved by decision (a): watchers keep explicit
per-watch `ChangeCallback` injection (the already-implemented behavior),
recorded as a short policy note; no behavior change. The
`Engine::GetStreamingGraph()` temporary TaskGraph bridge now has a
tracked removal owner per `AGENTS.md` §13: the new `RUNTIME-105` backlog
task (the promoted tree already has zero bridge consumers), named in both
the `[[deprecated(...)]]` message and the runtime README streaming note.
The drift-audit Row 7 greps over `src/core/**` and `src/runtime/**`
return only task-ID-tracked markers. Default CPU gate green at
retirement.

Backlog
[`RORG-031A`](RORG-031A-architecture-foundation.md) — architecture
foundation backlog seed — retired to `tasks/done/` on 2026-06-10. The
seed's job was converting the legacy living backlog's architecture items
into structured, independently executable tasks, and that exists: the
`tasks/backlog/architecture/` queue carries the LEGACY-001..012 retirement
series, `HARDEN-078`, `INFRA-001`, and a category README with explicit
consumer-grep gates; architecture governance tooling
(`check_layering.py`, `check_docs_sync.py`,
`generate_module_inventory.py`) exists and runs strict in CI; and
migration/CI dependencies are recorded as gates and front-matter
`depends_on` edges. Open architecture work remains independently tracked
by its own task files.

Backlog
[`PROC-008`](PROC-008-category-readme-state-history-split.md) — category
README state/history split — retired to `tasks/done/` on 2026-06-10,
completing Theme H. Slice A mechanically split every
`tasks/backlog/<category>/README.md` into open lists and verbatim
`## Retired` history sections (open entries cite retired tasks as plain
code spans; emptied lists carry explicit none-open lines; the workshop
pack's completed execution record was re-headed as history;
`bugs/index.md` already conformed via `Verified / Closed`). Slice B
extended `check_task_state_links.py` with `validate_category_indexes`:
heading-stack scanning of category indexes where done-links are findings
unless under a history-marked heading
(retired/history/closed/completed/resolved/verified/done), ATX headings
require a trailing space so inline PR references like `#921` cannot pop
the stack, and sections that interleave done prerequisites with open work
by design — the rendering dependency DAG — opt out explicitly with
`<!-- state-link-guard: allow-done-links -->` while rendering's non-DAG
sections were de-linked. `docs/agent/task-format.md` step 4 and the skill
mirrors document the convention. The throwaway done-link probe produced
exactly one finding and was removed. With PROC-001..008 retired, Theme H
has no open members.

Backlog
[`METHOD-011`](METHOD-011-sph-fluid-reference-backend.md) — SPH fluid
reference backend — retired to `tasks/done/` on 2026-06-10 at maturity
`CPUContracted`. `methods/physics/sph_fluid_reference/` ships the
deterministic weakly compressible SPH `cpu_reference` backend (Mueller
2003): Poly6 density with self-contribution, clamped ideal-gas pressure,
symmetric Spiky-gradient pressure force, viscosity-Laplacian force,
semi-implicit Euler, half-space boundary planes with restitution-scaled
normal reflection, and deterministic O(N^2) index-ordered neighbor
enumeration with an advisory `MaxNeighborLimit` whose overflow is
reported, never truncated. Diagnostics cover validation codes, total
mass, density statistics (average/min/max, `MaxCompression`
incompressibility proxy, mean relative density error), neighbor counts,
max velocity, kinetic energy drift, and the non-finite fail-closed
fallback. Thirteen `unit;physics` tests pin kernel closed forms and
numeric normalization, uniform-grid density recovery (~1% interior
error), exact symmetric-pair momentum conservation, viscosity smoothing,
the free-fall closed form, the toy column drop over a floor plane,
overflow reporting, invalid-input validation, determinism, and the
fallback. The `physics.sph_fluid_reference.smoke` benchmark emits
validated result JSON (static-grid interior density error ~0.0098 vs
0.05 threshold). With METHOD-009/010/011 all retired, Theme C has no
open members; optimized/GPU backends and runtime integration open as new
tasks per the roadmap gates.

Backlog
[`METHOD-010`](METHOD-010-xpbd-cloth-shell-reference-backend.md) — XPBD
cloth and shell reference backend — retired to `tasks/done/` on 2026-06-10
at maturity `CPUContracted`. `methods/physics/xpbd_cloth_reference/` ships
the deterministic XPBD `cpu_reference` backend over triangle-mesh cloth
state: position prediction, iterative compliant constraint projection with
per-constraint Lagrange multipliers (structural unique-edge constraints
plus opposite-vertex bending pairs across interior edges, both built
deterministically by `BuildClothFromTriangles`), half-space collision
projection with collision inputs as pure method parameters (sphere
colliders declared but unsupported and counted), and position-derived
velocities. Diagnostics cover validation codes (including
`InvalidTopology` for repeated/out-of-range triangle indices),
degenerate-triangle and coincident-constraint counts, stretch/bend
residuals (max, L2), convergence against the residual tolerance,
kinetic/mechanical energy drift, and the non-finite fail-closed fallback.
Fourteen `unit;physics` tests pin the builder topology, rigid projection,
hanging-patch convergence, bend response, pinned vertices, collision
floor, degenerate/invalid handling, non-convergence reporting,
determinism, and the fallback. The `physics.xpbd_cloth_reference.smoke`
benchmark emits validated result JSON (final max stretch residual
~6.2e-4 vs 5e-3 threshold). Optimized CPU/GPU backends remain forbidden
until a future task names this package as its oracle. Theme C's remaining
open member is `METHOD-011` (SPH fluid).

Backlog
[`METHOD-009`](METHOD-009-particle-spring-reference-backend.md) — particle
and mass-spring reference backend — retired to `tasks/done/` on 2026-06-10
at maturity `CPUContracted`. `methods/physics/particle_spring_reference/`
ships the deterministic `cpu_reference` backend for particle dynamics and
mass-spring systems: semi-implicit Euler integration, Hooke springs with
Provot-style axial damping, pinning via zero inverse mass, global drag, and
machine-checkable diagnostics (validation codes for invalid
timestep/particle/spring, pinned and degenerate-spring counts, post-step
spring residuals max/L2, kinetic/total energy drift, the `omega*dt`
stability ratio with limit flag, and a non-finite fail-closed fallback that
returns the input state unchanged). Twelve `unit;physics` correctness
tests pin free-fall closed form, rest-length equilibrium, exact
momentum/center-of-mass conservation, bounded harmonic energy drift,
damped hanging-spring analytic equilibrium, pinned/degenerate/invalid
handling, instability fallback, and repeated-step determinism. The
`physics.particle_spring_reference.smoke` benchmark manifest + workload
emit validated result JSON with exact conservation error as the quality
metric. Optimized CPU/GPU backends remain forbidden until a future task
names this package as its oracle. Theme C's remaining open members are
`METHOD-010` (XPBD cloth/shell) and `METHOD-011` (SPH fluid).

Backlog
[`RORG-031C`](RORG-031C-runtime-composition.md) — runtime composition
backlog seed — retired to `tasks/done/` on 2026-06-10. The seed's job was
to replace the unnamed runtime composition narrative gap with concrete
child tasks, and that is done: `RUNTIME-099` (explicit lifecycle pipeline
with shutdown determinism, `CPUContracted`), `RUNTIME-100` (scene
lifecycle), `RUNTIME-102` (editor command history), `RUNTIME-103`
(geometry algorithm execution queue decision), and `RUNTIME-104` (derived
overlay producer lifecycle) are retired. `RUNTIME-101` (asset ingest state
machine) was independently tracked after this seed and retired on
2026-06-15, synchronized with the `LEGACY-011` feature map. Theme A now has
no open members.

Backlog
[`BUG-025`](BUG-025-contact-manifold-normal-convention.md) — geometry
contact manifold normals violate the documented A→B convention — retired to
`tasks/done/` on 2026-06-10 at maturity `CPUContracted`. Root cause was two
kernel inversions: `EPA_Solver` negated the closest-face outward normal of
the A−B Minkowski polytope (which is already the A→B direction), and
`Contact_Analytic(Sphere, AABB)` computed the normal from the box-closest
point toward the sphere center (B→A) in both its shallow and
deep-penetration branches. Fix: EPA returns `searchDir` directly,
`Contact_Fallback` derives `ContactPointB = ContactPointA - Normal * Depth`
(the same world point under the corrected normal), and the sphere-AABB
analytic path is A→B in both branches with consistent contact points. New
`unit;geometry` convention tests pin every analytic overload, the
reversed-argument dispatcher, and the GJK/EPA fallback for
sphere/capsule/OBB pairings in both argument orders
(`ContactManifold.Convention_*`). The physics-layer orientation guard and
its regression test stay as defense in depth. Geometry label 1263/1263 and
physics label 21/21 at retirement. Theme G has no open members.

Backlog
[`BUG-024B`](BUG-024B-sandbox-transform-edit-vulkan-pixel-shift-smoke.md) —
Vulkan pixel-shift smoke for sandbox transform edits — retired to
`tasks/done/` on 2026-06-10 at maturity `Operational`. The opt-in
`gpu;vulkan` smoke `RuntimeSandboxAcceptanceGpuSmoke.InspectorTransformEditShiftsReferenceTrianglePixels`
applies the promoted Inspector transform-edit command through the live
`EditorCommandHistory` path on a mid-run frame (after that frame's
fixed-step bundle) and asserts the rendered `ReferenceTriangle` moved: the
frame center returns to the background and the analytically projected
shifted sample contains the triangle. Passed on NVIDIA GeForce RTX 3050 /
driver 590.48.01 (focused 1/1; full smoke suite 6/6), upgrading the BUG-024
fix from `CPUContracted` to `Operational`. Theme G has no open members.

Backlog
[`BUG-024`](BUG-024-sandbox-transform-edit-rendering.md) — sandbox transform
UI edits do not move rendered triangle — retired to `tasks/done/` on
2026-06-10 at maturity `CPUContracted`. Root cause: Inspector/gizmo
transform edits run after the fixed-step ECS bundle, so render extraction
observed a stale `Transform::WorldMatrix` and the rendered model matrix
never moved within the frame. Fix: `Extrinsic.Runtime.EcsSystemBundle` now
exports `FlushPreRenderTransformState` (direct `TransformHierarchy` →
`BoundsPropagation` → `RenderSync` pass), invoked by `Engine::RunFrame()`
after the variable tick, ImGui editor hook, and gizmo drive — before
transform-gizmo packet build and extraction. Regression coverage:
engine-level `RuntimeSandboxAcceptance.InspectorTransformEditFlushedToRenderStateSameFrame`
(verified failing with the flush disabled), extraction-level
`RuntimeRenderExtraction.UiTransformEditModelReachesRenderWorldAfterPreRenderFlush`
(asserts the render-world model translation), and flush-helper contract
tests in `RuntimeEcsSystemBundle`. Default CPU gate passed 2882/2882 at
retirement. `Operational` (Vulkan pixel-shift smoke) owned by `BUG-024B`.

Previously-active
[`PROC-006`](PROC-006-audit-cadence-lapse-visibility.md) — audit cadence
lapse visibility retired to `tasks/done/` on 2026-06-09. The slice added
`tools/agents/check_audit_cadence.py` (agent-output limit 14d, drift limit
42d, report-only by default, `--strict` for local use only), a non-blocking
nightly-deep report step, last-report dates in the `tasks/SESSION-BRIEF.md`
audits section (dates rather than ok/overdue so the brief stays
deterministic under the CI freshness check), and lapse-visibility notes in
both audit checklists. No PR gate depends on audit recency. Theme H's
remaining open leaf is `PROC-008` (category README state/history split).

Previously-active
[`PROC-004`](PROC-004-task-front-matter-and-generated-session-brief.md) —
structured task front-matter + generated session brief retired to
`tasks/done/` on 2026-06-09 at maturity `Operational`. Slice A gave all 44
open tasks YAML front-matter (`id`/`theme`/`depends_on`) with strict
validation in `validate_tasks.py` (id↔title match, resolvable dependency
edges). Slice B added `tools/agents/generate_session_brief.py` and the
committed, `ci-docs.yml`-freshness-checked `tasks/SESSION-BRIEF.md`
(active tasks; per-theme unblocked/blocked with first unmet dependency),
and adopted it as mandatory session reading in `docs/agent/prompt/prompt.md`
and the `intrinsicengine-core` skill, demoting the two task READMEs to
on-demand depth. Slice C (anchor-prose retirement) was skipped — PROC-003
had already reduced anchors to open-endpoint entries. The audits surface in
the brief is owned by `PROC-006`.

Previously-active
[`PROC-003`](PROC-003-split-task-index-state-from-retirement-history.md),
[`PROC-007`](PROC-007-onboarding-prompt-tightening.md),
[`PROC-005`](PROC-005-align-structural-check-mode-contract-text.md),
[`PROC-002`](PROC-002-task-id-uniqueness-and-allocation-rule.md), and
[`PROC-001`](PROC-001-skill-mirror-sync-generator-and-ci-gate.md) —
the first five Theme H agentic-workflow hardening slices retired to
`tasks/done/` on 2026-06-09 on branch
`claude/agentic-workflow-analysis-kohifk`. PROC-001 added
`tools/agents/sync_skills.py` (generate-and-verify skill mirror sync with
link rewriting and a `ci-docs.yml` gate) and repaired 11 drifted mirror
files. PROC-002 added the task-ID uniqueness pass to `validate_tasks.py`
with five grandfathered historical collisions and the max+1 allocation
rule in `task-format.md`. PROC-005 replaced the stale "warning mode"
structural-check claims in `AGENTS.md` §10 and `docs/agent/contract.md`
with the strict-CI reality plus a tracked-exception rule. PROC-007
deduplicated the onboarding prompt against `AGENTS.md` §2/§5/§7/§9/§12 and
gave loop mode an `N = 3` default plus a per-iteration push checkpoint.
PROC-003 created this retirement log, trimmed `tasks/active/README.md`
532→21 lines and `tasks/backlog/README.md` 334→212 lines to state-only,
and added the `validate_state_only_indexes` regrowth guard; category-README
cleanup is owned by the follow-up `PROC-008`. Remaining Theme H leaves:
`PROC-004` (front-matter + session brief), `PROC-006` (audit cadence
visibility), `PROC-008`.

Previously-active
[`HARDEN-079`](../done/HARDEN-079-core-module-implementation-splits.md),
[`GEOM-021`](../done/GEOM-021-meshsoup-module-implementation-split.md),
[`GEOM-022`](../done/GEOM-022-remaining-geometry-module-implementation-splits.md),
[`HARDEN-080`](../done/HARDEN-080-ecs-module-implementation-splits.md),
[`PLATFORM-005`](../done/PLATFORM-005-platform-module-implementation-splits.md),
[`GRAPHICS-083`](../done/GRAPHICS-083-graphics-rhi-module-implementation-splits.md), and
[`RUNTIME-096`](../done/RUNTIME-096-runtime-module-implementation-splits.md) —
promoted module implementation split batch retired to `tasks/done/` on
2026-06-07. The implementation split landed in `bfcd2751`; retirement was
held until the default CPU gate passed after rebuilding `IntrinsicTests` with
`CCACHE_DISABLE=1`, explicitly building `IntrinsicBenchmarkSmoke`, and rerunning
`ctest -LE 'gpu|vulkan|slow|flaky-quarantine'` for 2816/2816 passing tests.
The earlier rendergraph ASan failure was stale incremental C++23 module layout
state from ccache/module artifacts, not a source defect in the split.

Previously-active
[`WORKSHOP-007`](../done/WORKSHOP-007-dependency-driven-default-recipe.md) —
dependency-driven default frame recipe retired to `tasks/done/` on
2026-06-06. The slice removed blanket previous-pass chaining from
`BuildDefaultFrameRecipe`, kept explicit pass dependencies as an intentional
graph API rather than a default recipe behavior, exposed explicit dependency
edges in compiled pass declarations/debug dumps, and added contract coverage for
resource-derived ordering, side-effect order, barrier packet ordering, and
picking/selection/debug/postprocess feature combinations.

Previously-active
[`WORKSHOP-006`](../done/WORKSHOP-006-extract-render-prep-pipeline.md) —
render-prep pipeline extraction retired to `tasks/done/` on 2026-06-06. The
slice added the `Extrinsic.Graphics.RenderPrepPipeline` module, moved
CPU-side `PrepareFrame` prep ordering out of the renderer, retained task-graph
and sequential ordering coverage, added fail-closed missing-input/task-graph
diagnostics, and made renderer lifecycle diagnostics reject `ExecuteFrame`
after failed prep.

Previously-active
[`ARCH-001`](../done/ARCH-001-physics-layer-ownership-and-ecs-integration.md)
— physics layer ownership and ECS/runtime integration contract retired to
`tasks/done/` on 2026-06-05 at maturity `Retired`. The slice accepted
`src/physics` as the simulation-world layer through ADR-0019 with
`physics -> core, geometry` dependencies, updated `AGENTS.md`, architecture
docs, agent skill mirrors, label policy, and layering tooling, and opened
`PHYSICS-001..003` follow-up tasks for world/runtime sync,
broadphase/narrowphase, and solver diagnostics. `HARDEN-064` has since retired
the ECS collider/rigid-body authoring contract under the
no-solver-handles-in-ECS boundary, and `PHYSICS-001` has retired the first
CPU-only physics world/runtime bridge. `PHYSICS-002` is the next open physics
runtime-readiness leaf retired on 2026-06-06; `PHYSICS-003` is now the next
open physics runtime-readiness leaf.

Previously-active
[`GRAPHICS-040C`](../done/GRAPHICS-040C-aa-recipe-selection-and-integration.md)
— AA recipe selection + post-chain integration retired to `tasks/done/` on
2026-06-05 at maturity `Operational`. The slice added the explicit
`FrameRecipeAAOptions` selector, mode-specific FXAA/SMAA pass compilation,
temporal `ReconstructionPass` routing with retained history imports,
input/output extent splitting, renderer-side reference-TAA execution, and
`ReconstructorAppliedFrames` / `HistoryDisocclusionPercent` / jitter
diagnostics. Vendor reconstructor backend children remain unopened.

Previously-active
[`GRAPHICS-040B`](../done/GRAPHICS-040B-reconstructor-interface-and-reference-taa.md)
— `IReconstructor` interface + reference TAA retired to `tasks/done/` on
2026-06-05 at maturity `CPUContracted`. The slice added the vendor-free
`Extrinsic.Graphics.Reconstruction` module with `IReconstructor`,
`ReconstructionHints`, and `ReconstructionResult`, a CPU-contracted
`ReferenceTAAReconstructor` using 5x5 YCoCg variance clipping,
exposure-aware history weighting, reset invalidation, and disocclusion
fallback reporting, plus a retained `RGBA16_FLOAT` ping-pong
`ReconstructionHistorySystem` with retire-window coverage. Recipe selection and
post-chain integration are retired in `GRAPHICS-040C`; vendor children remain
unopened.

Previously-active
[`GRAPHICS-040A`](../done/GRAPHICS-040A-jitter-and-motion-vectors.md) —
camera jitter + motion-vector buffer retired to `tasks/done/` on 2026-06-05 at
maturity `CPUContracted`. The slice added a deterministic Halton(2,3)×16
temporal jitter helper, projection-matrix jitter override, authoritative
`TemporalCameraViewSnapshot::JitterOffset`, opt-in `MotionVectors`
frame-recipe resource/attachment shape, `NoJitterNoHistory` suppression, and
graphics contract coverage for jitter replay, projection math, and motion-vector
target gating. Reference TAA reconstruction retired in `GRAPHICS-040B`; recipe
selection and post-chain integration retired in `GRAPHICS-040C`.

Previously-active
[`GRAPHICS-039D`](../done/GRAPHICS-039D-cluster-async-compute-affinity.md) —
cluster build/assignment async-compute affinity retired to `tasks/done/` on
2026-06-05 at maturity `CPUContracted`. The slice tagged
`ClusterGridBuildPass` and `LightClusterAssignmentPass` with
`RenderQueue::AsyncCompute`, proved capability-absent demotion to graphics
through the framegraph/RHI queue-affinity helpers and submit-plan builder,
preserved single-queue correctness, updated the renderer docs, and added
frame-recipe contract coverage for async-capable and async-absent profiles.

Previously-active
[`GRAPHICS-039C`](../done/GRAPHICS-039C-cluster-surface-shader-integration.md) —
clustered surface-shader integration + recipe wiring retired to `tasks/done/`
on 2026-06-05 at maturity `CPUContracted`. The slice added scene-table BDA
publication for `ClusterLights.Headers` / `ClusterLights.Indices`, cluster-grid
metadata in `GpuSceneTable`, renderer-owned retained cluster buffers and
pipeline leases, default-recipe reads for forward `SurfacePass` and deferred
`CompositionPass`, shared GLSL clustered-light iteration with a full-loop
fallback, CPU parity coverage for known-cell clustered accumulation, renderer
lifecycle assertions for scene-table publication/rebuild survival, and touched
shader compilation. Async-compute affinity is retired in `GRAPHICS-039D`.

Previously-active
[`GRAPHICS-039B`](../done/GRAPHICS-039B-light-cluster-assignment.md) —
light-to-cluster assignment + overflow diagnostics retired to `tasks/done/` on
2026-06-05 at maturity `CPUContracted`. The slice added
`ClusterLightCellHeader`, retained `ClusterLights.Headers` /
`ClusterLights.Indices` / `ClusterLights.Counter` imports, the
`light_cluster_assign.comp` shader asset, a deterministic CPU assignment helper
over existing `LightSnapshot` values, conservative point/spot inclusion,
directional-light skip, 256-contributor clamp diagnostics, frame-recipe
`LightClusterAssignmentPass` ordering after `ClusterGridBuildPass`, and
contract coverage for shape inclusion, empty cells, overflow, counter clearing,
command shape, and diagnostic publication. Surface-shader consumption and
async-compute affinity remain `GRAPHICS-039C/D`.

Previously-active
[`GRAPHICS-039A`](../done/GRAPHICS-039A-cluster-grid-build.md) — cluster grid
resource + build pass retired to `tasks/done/` on 2026-06-04 at maturity
`CPUContracted`. The slice added the `Extrinsic.Graphics.LightClusters` module,
the default 80 px tile / 24 logarithmic Z-slice froxel-grid contract, per-cell
view-space AABB construction with clamped partial-edge tile bounds,
`ClusterGrid.AABBs` resource/import semantics, default-recipe
`ClusterGridBuildPass` ordering after depth/HZB, the
`cluster_grid_build.comp` shader asset, and contract coverage for dimensions,
log-Z slicing, empty beyond-far mapping, partial edge tiles, AABB bounds,
resource usage, dispatch shape, and frame-recipe gating. Light assignment,
shader consumption, and async-compute affinity remain `GRAPHICS-039B/C/D`.

Previously-active
[`GRAPHICS-038E`](../done/GRAPHICS-038E-hzb-conservatism-gpu-smoke.md) —
opt-in `gpu;vulkan` HZB conservatism smoke retired to `tasks/done/` on
2026-06-04 at maturity `Operational` on Vulkan-capable hosts. The slice added a
test-only HZB conservatism compute shader, a Vulkan smoke that dispatches the
two-phase predicate on real GPU storage buffers, CPU parity checks against
`ComputeTwoPhaseCullPartition(...)`, known-visible no-over-rejection,
disocclusion rescue, persistent rejection, invalid-previous-sample
conservatism, frustum-first rejection, and selection-bucket exemption coverage.
The default CPU/null contracts remain unchanged; production HZB storage-image
descriptor publication remains future backend descriptor integration.

Previously-active
[`GRAPHICS-038D`](../done/GRAPHICS-038D-camera-transition-and-selection-exemption.md) —
camera-transition skip heuristic and selection-bucket occlusion exemption
retired to `tasks/done/` on 2026-06-04 at maturity `CPUContracted`. The slice
added snapshot-carried explicit camera-transition flags, delta-threshold
stale-HZB detection, `HzbStaleSkipCount` diagnostics, shader push-constant
flags, hard frustum-only phase-1 routing for selection buckets, runtime
camera-controller one-shot transition signaling, contract/integration coverage,
and renderer/runtime docs sync. `Operational` opt-in GPU/Vulkan conservatism
proof remains owned by `GRAPHICS-038E`.

Previously-active
[`GRAPHICS-038C`](../done/GRAPHICS-038C-two-phase-cull-shader.md) —
phase-1/phase-2 cull shader extension and per-bucket buffer doubling retired
to `tasks/done/` on 2026-06-04 at maturity `CPUContracted`. The slice added the
`GpuCullBucketPhases` ABI, phase-1/phase-2 indirect output surfaces per bucket,
diagnostics counters, shader phase-output selection, renderer reset/table/barrier
wiring for both phases, `GetBucketPhase(kind, phase)`, deterministic CPU
visible/rejected/rescued partition coverage, and rendering docs sync. The
camera-transition/selection exemption remains `GRAPHICS-038D`; concrete Vulkan
HZB reject-list publication, phase-2 recull, and opt-in `gpu;vulkan`
conservatism proof remain `GRAPHICS-038E`.

Previously-active
[`GRAPHICS-038B`](../done/GRAPHICS-038B-hzb-build-compute.md) — HZB build
compute shader + dispatch wiring retired to `tasks/done/` on 2026-06-04 at
maturity `CPUContracted`. The slice added `assets/shaders/hzb_build.comp`, the
pure HZB build-plan selector, backend-neutral per-mip fallback recording,
default-recipe `HZBBuildPass` wiring after `DepthPrepass`, renderer-owned
`HZB.Current` import/pipeline lease plumbing, null-RHI dispatch/barrier
contracts, shader-output verification, and rendering/debug-view docs sync.
Single-pass/SPD-style storage-image publication and opt-in `gpu;vulkan`
conservatism proof remain owned by `GRAPHICS-038E`.

Previously-active
[`GRAPHICS-037D`](../done/GRAPHICS-037D-multi-queue-vulkan-recording.md) —
Vulkan multi-queue recording retired to `tasks/done/` on 2026-06-04 at
maturity `Operational` on Vulkan-capable hosts. Slices A-D landed
async-compute/transfer queue-family discovery, Sync2 queue-family token
translation, the backend-neutral RHI submit-plan/context seam, per-affinity
Vulkan command-buffer submission with timeline waits/signals and ownership
transfer barriers, default-recipe async histogram routing, and opt-in
`gpu;vulkan` readback smoke coverage. Capability-absent hosts keep the
single-queue path through queue-affinity demotion and the default CPU gate.

Previously-active
[`UI-001`](../done/UI-001-sandbox-editor-shell-panels.md) — sandbox editor shell
and core panels on top of the runtime ImGui adapter/pass stack retired to
`tasks/done/` on 2026-06-03 at maturity `CPUContracted`. Slices A-D landed the
promoted editor shell, scene hierarchy, inspector/render-hint fields,
selected/hovered entity rows, refined primitive id/hit display, runtime-owned
local-transform edits, camera-controller replacement, mesh edge/vertex
primitive-view toggles, selected-entity spatial-debug and visualization-config
commands, visualization adapter-binding routing through engine-owned
render-extraction state, and file/import command execution through
`Engine::ImportAssetFromPath(...)` on top of the retired `ASSETIO-001`
asset/runtime ingest seams. Final file-backed visual/interactive proof remains
owned by `RUNTIME-095`.

Previously-active
[`RUNTIME-083`](../done/RUNTIME-083-visualization-adapters.md) —
`Extrinsic.Runtime.VisualizationAdapters` runtime producer umbrella retired to
`tasks/done/` on 2026-06-02 at maturity `CPUContracted`. Slices A-E landed the
umbrella module, property-scalar, KMeans/color, vector-field, isoline, Htex
preview, and fragment-bake adapters, runtime-owned adapter registration/binding
state, scalar and non-scalar extraction selection into
`RuntimeRenderSnapshotBatch::Visualization*`, and extraction-side packet/error
stats with CPU `integration;runtime;graphics` coverage. `Operational`
visualization proof remains owned by `RUNTIME-095` or a later visualization
backend smoke.

Previously-active
[`GRAPHICS-079`](../done/GRAPHICS-079-default-recipe-imgui-pass-wiring.md) —
default-recipe `Pass.ImGui` wiring (Theme A working-sandbox path, the consumer
half of the ImGui/UI leaves that gate `UI-001`) retired to `tasks/done/` on
2026-06-02. Slices A/B wired the renderer-side `ImGuiPass` executor route,
overlay handoff seam, pipeline lease, and runtime-owned overlay attachment.
Slice C added the retained font atlas, renderer-owned transient vertex/index
upload helper, runtime adapter payload copy, direct draw recording contracts,
and byte-identical atlas retention across rebuild. Slice D.1 promoted
`Pass.ImGui` to write `FrameRecipe.PresentSource` and proved the CPU/null
recorded path plus the default-recipe closing-cleanup assertion. Slice D.2 added
per-command user-texture bindless metadata/shader sampling and registered the
opt-in `ImGuiSurfaceGpuSmoke` `gpu;vulkan` fixture, which skips on hosts without
an operational GLFW/Vulkan lane. Maturity: `Operational` on Vulkan-capable
hosts, `CPUContracted` on this host. Final implementation commit `69f9b16c`;
full slice chain `8f1374c6`, `61192d50`, `84d16985`, `97d34aba`, `9e283c72`,
`69f9b16c`. Downstream editor panels remain owned by `UI-001`; final sandbox
acceptance remains owned by `RUNTIME-095`.

Previously-active
[`RUNTIME-090`](../done/RUNTIME-090-imgui-platform-renderer-adapter.md) —
runtime-side Dear ImGui platform/renderer adapter
(`Extrinsic.Runtime.ImGuiAdapter`, Theme A working-sandbox path stage 4, the
producer half of the ImGui/UI leaves that gate `UI-001`) retired to
`tasks/done/` on 2026-06-02 at maturity `CPUContracted`. Slice A
(`claude/intrinsicengine-agent-onboarding-qu8wV`, PR #962, commits `3bd20f2` +
`4676a1d`) landed the standalone adapter module (ImGui 1.92 context lifecycle
with `ImGuiBackendFlags_RendererHasTextures`, `Platform::Event`→ImGui-IO pump,
`ImDrawData`→`ImGuiOverlayFrame` walk, editor hook, diagnostics) with
`FakeWindow`-driven `contract;runtime` coverage at `Scaffolded→CPUContracted`;
`imgui_lib` is linked **PRIVATE** to `ExtrinsicRuntime` and `imgui.h` stays out
of the `.cppm` interface. Slice B
(`claude/intrinsicengine-agent-onboarding-01gFi`, PR #963, commit `fdc3165`)
closed `CPUContracted` by wiring the adapter into `Engine`: `Engine` owns the
`Graphics::ImGuiOverlaySystem` instance (the allowed `runtime -> graphics` edge)
and constructs the adapter in `Initialize()` after the `Window`/`Renderer`;
`RunFrame` calls `BeginFrame(frameDt)` after `PollEvents` + the minimize/resize
early returns and before `OnVariableTick`, and `EndFrame()` after the variable
tick and before the render contract's `PrepareFrame()`, so exactly one
`ImGuiOverlayFrame` is produced per engine frame; the editor hook is exposed via
`Engine::SetImGuiEditorCallback` with a read-only `GetImGuiAdapter()` observer.
New `Test.ImGuiAdapterEngineWiring.cpp` `contract;runtime` coverage drives a
bounded `Engine::Run()` (static wiring cases run displayless; the live per-frame
loop + editor-hook cases are window-gated and verified under `xvfb-run`).
GRAPHICS-079 now consumes the adapter-produced payload through the renderer-side
retained font atlas, transient upload helper, `FrameRecipe.PresentSource`
topology, per-command bindless user-texture sampling, and the registered
`ImGuiSurfaceGpuSmoke` opt-in fixture. Final working-sandbox acceptance is
`RUNTIME-095`.

Previously-active
[`RUNTIME-093`](../done/RUNTIME-093-primitive-selection-refinement.md) — runtime
primitive selection refinement for meshes, graphs, and point clouds (Theme A
working-sandbox path) retired to `tasks/done/` on 2026-06-01 at maturity
`CPUContracted`. Slice A (PR #959) delivered the standalone
`Extrinsic.Runtime.PrimitiveSelectionRefinement` module (result type +
fail-closed taxonomy + hint-based mesh/graph/point-cloud refinement against
authoritative `GeometrySources`, entity-transform local/world hit reporting,
`contract;runtime` fixtures) at `Scaffolded`. Slice B1 (commit `0cacfdf`, PR
#960) added the optional CPU ray fallback for missing (`None`-domain) hints
(`CpuFallbackResolved`/`CpuFallbackMiss`). Slice B2
(`claude/intrinsicengine-agent-onboarding-X3GCq`, commit `752b47f`) closed
`Scaffolded → CPUContracted` by wiring refinement into the runtime frame loop:
the new pure `RefinePickReadbackResult(scene, readback)` bridge resolves a pick
readback's render id to a live entity (decode + recycling-safe `registry.valid()`
check → deterministic `StaleEntity`), reads `Transform::WorldMatrix` as
`LocalToWorld`, builds the authoritative `GeometrySources::ConstSourceView`, and
delegates to `RefinePrimitiveSelection`; `Engine::RunFrame` caches the result in
`m_LastRefinedPrimitive` (`GetLastRefinedPrimitiveSelection()`) as the existing
readback-drain loop runs (newest pick wins, background clears, empty-drain
retains), alongside the unchanged `SelectionController` whole-entity mutation.
The editor-facing-cache arm was chosen over controller ownership to keep the
controller graphics-free (controller-owned variant recorded as a nonblocking
follow-up). No graphics mutation; ECS tag model unchanged. Ten new
`Test.PrimitiveSelectionRefinementWiring.cpp` `contract;runtime` cases pass;
`contract;runtime` gate 277/277, `unit;geometry` 1254/1254;
layering/test-layout/doc-links/task-policy checks clean; module inventory
regenerated (no diff). `Operational` interactive selection proof stays owned by
`RUNTIME-089`, `GRAPHICS-074`, and final sandbox acceptance (`RUNTIME-095`).

Previously-active
[`RUNTIME-092`](../done/RUNTIME-092-stable-entity-lookup.md) — runtime stable
entity lookup sidecar (Theme A working-sandbox path) retired to `tasks/done/` on
2026-05-31 at maturity `CPUContracted`. Slice A landed the standalone
`Extrinsic.Runtime.StableEntityLookup` module (runtime-owned
`StableId -> entt::entity` winner-map realising the `HARDEN-068` Decision-3
deferred lookup, reversible render-id resolution, deterministic
smallest-render-id duplicate policy, lazy + bulk stale invalidation,
diagnostics; `Scaffolded`). Slice B
(`claude/intrinsicengine-agent-onboarding-8y1qR`) closed
`Scaffolded → CPUContracted` by wiring the sidecar into the runtime frame path:
`Engine` now owns a `StableEntityLookup`, attaches it to the
`SelectionController` in `Initialize()` (`SetStableEntityLookup`), and
`Rebuild`s it once per frame in `RunFrame` immediately before the pick-readback
drain. The controller's render-id resolution seam (`ConsumeHit`,
`SetSelectedByStableEntityId`) routes through the attached lookup's
`ResolveByRenderId` (decode + live-registry validation), so a pick naming a
recycled/destroyed slot is rejected by the single runtime authority instead of
mis-resolving to the recycled occupant; with no lookup attached the controller
falls back to the bare decode so standalone callers are unaffected. Slice B
decision: reference-scene entities remain transient (no generated `StableId`).
Five new `Test.SelectionStableLookupComposition.cpp` `contract;runtime` cases
plus the 13 Slice A cases pass; runtime gate 243/243, ECS gate 146/146;
layering/test-layout/doc-links/task-policy checks clean; module inventory
regenerated (no diff). `Operational` user-visible selection durability stays
owned by `RUNTIME-089`, UI tasks, and final sandbox acceptance (`RUNTIME-095`).

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
`RenderEdges`/`RenderPoints` through `BindGraphGeometry` (upload/reuse/
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
readback parity is retired by
[`GRAPHICS-077E`](../done/GRAPHICS-077E-transient-debug-pixel-readback.md).

[`GRAPHICS-078`](../done/GRAPHICS-078-visualization-overlay-upload-helper.md) —
visualization-overlay upload helper retired to `tasks/done/` on 2026-05-28 after
Slice D added `VisualizationOverlaySurfaceGpuSmoke`; maturity is
`CPUContracted` on CPU-only hosts and command-stream `Operational` on
Vulkan-capable hosts. Pixel readback parity is retired by
[`GRAPHICS-078E`](../done/GRAPHICS-078E-visualization-overlay-pixel-readback.md).

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

[`BUG-035`](../done/BUG-035-vulkan-slot-recycling-smoke.md) — Vulkan
slot-recycling smoke retired to `tasks/done/` on 2026-06-12 at `Operational`.
The opt-in `gpu;vulkan` smoke advances the real promoted Vulkan frame loop past
the retirement window and observes destroyed buffer/texture slots being reused
with bumped generations through public handles.

[`BUG-034`](../done/BUG-034-vulkan-resource-pool-reclamation.md) — Vulkan
ResourcePool reclamation retired to `tasks/done/` on 2026-06-12 at
`CPUContracted`, with `BUG-035` providing the Vulkan operational proof.
`VulkanDevice` now processes buffer/image/sampler/pipeline pool deletions from
the frame loop, including fail-closed `EndFrame()` exits, while keeping
deferred Vulkan-object destruction in the existing deletion queue. The Null
device slot-recycling contract pins the backend-neutral behavior in the default
CPU gate.

[`BUG-033`](../done/BUG-033-mesh-io-untrusted-header-counts.md) — mesh IO
untrusted header-count hardening retired to `tasks/done/` on 2026-06-12 at
`CPUContracted`. OFF/PLY import now validates declared counts against payload
before allocation, uses overflow-safe byte checks, rejects invalid PLY list
count types/counts, and fails closed on degenerate OFF face rows. Malformed
input regressions pass without aborting.

[`BUG-032`](../done/BUG-032-triangle-edge-point-vulkan-rendering.md) —
triangle edge/point Vulkan rendering retired to `tasks/done/` on 2026-06-12 at
`Operational`. The fix aligned `GpuGeometryRecord` ABI stride between C++ and
GLSL, removed double-applied vertex offsets from GpuScene shaders, propagated
runtime mesh sidecar point/edge config, and proved visible reference-triangle
edge/point lanes through Vulkan smoke/readback coverage.

[`BUG-031`](../done/BUG-031-benchmark-smoke-not-in-intrinsictests-aggregate.md)
— benchmark smoke aggregate wiring retired to `tasks/done/` on 2026-06-12 at
`CPUContracted`. The current tree registers `IntrinsicBenchmarkSmoke` through
the shared aggregate target property; building only `IntrinsicTests` produces
the smoke runner and the benchmark CTest pair passes.

[`BUG-030`](../done/BUG-030-headless-engine-run-tests-red-gate.md) — headless
`Engine::Run()` red-gate retired to `tasks/done/` on 2026-06-12 at
`CPUContracted`. Live-window engine-loop tests now guard born-closed windows
with the house `ShouldClose() -> GTEST_SKIP()` pattern, and `tests/README.md`
records the rule. The broader headless execution restoration is retired by
`RUNTIME-107`.

[`BUG-029`](../done/BUG-029-ray-aabb-slab-nan-poisoning.md) — ray/AABB slab
NaN poisoning retired to `tasks/done/` on 2026-06-12 at `CPUContracted`.
Analytic ray/AABB overlap and raycast now use NaN-free slab intervals for
axis-parallel/on-boundary rays, sphere raycasts use a finite center-origin
fallback normal, and BVH boundary-coincident ray traversal is pinned by tests.

## Satisfied cross-domain dependency anchors (history)

These anchors from `tasks/backlog/README.md` are fully satisfied (every
endpoint retired); they are preserved here verbatim for traceability.

- **GRAPHICS-034 ⇐ ASSETIO-001 ⇐ GEOIO-002.** Asset-backed mesh residency
  planning depends on promoted asset routing, which depends on geometry decoder
  parity. `GEOIO-002`, `ASSETIO-001`, and `GRAPHICS-034` are retired; the
  implementation children remain unopened.
- **RUNTIME-085..088 ⇐ HARDEN-065, GRAPHICS-030B, GRAPHICS-070/071.** Runtime
  mesh/graph/point-cloud residency depends on promoted `GeometrySources`, the
  proven runtime-to-`GpuWorld` upload/bind pattern, and retained surface/line/
  point pass contracts.
- **RUNTIME-089 ⇐ GRAPHICS-074; RUNTIME-093 ⇐ RUNTIME-089, RUNTIME-085..088.**
  Runtime selection policy consumes graphics readback, while primitive
  refinement requires both selected entities and authoritative geometry
  residency/source data.
- **UI-001 ⇐ RUNTIME-090, GRAPHICS-079, RUNTIME-089.** UI panels require ImGui
  frame production/presentation and runtime-owned selection state; panels must
  remain command/event producers, not owners of engine state. `UI-001` is
  retired at `CPUContracted`; final operational proof remains under
  `RUNTIME-095`.
- **RUNTIME-095 ⇐ GRAPHICS-072..079, GRAPHICS-081, ASSETIO-001 (texture/model
  ingest; `RUNTIME-080` retired into it), RUNTIME-085..089,
  RUNTIME-092..093, UI-001.**
  Satisfied 2026-06-04: the final working-sandbox acceptance composes the
  renderer, runtime residency, selection/refinement, asset/UI command surfaces,
  and UI paths for the scoped mesh/graph/point-cloud scene.
- **GRAPHICS-029..034 ⇐ HARDEN-060..062.** Sandbox renderable extraction needs
  promoted ECS scene/hierarchy/transform parity. `HARDEN-060`, `HARDEN-061`,
  and `HARDEN-062` are all retired to `tasks/done/`, so this gate is
  satisfied; the Theme A renderer leaves are unblocked on the ECS side.
- **RUNTIME-091 ⇐ HARDEN-061.** Runtime fixed-step ECS system activation depends
  on the promoted `TransformHierarchy` system and must keep composition in
  `runtime` rather than adding upward imports to `src/ecs`.
- **HARDEN-067 ⇐ RUNTIME-091 or equivalent scheduling decision.** Bounds
  propagation can be implemented independently, but default-runtime usefulness
  depends on a known ECS system activation path.
- **METHOD-001 ⇐ ARCH-001.** Satisfied 2026-06-05: the physics layer
  ownership decision is accepted and the deterministic `cpu_reference`
  rigid-body method package is retired at `CPUContracted`. Runtime/ECS
  integration remains out of scope for the method package and is owned by
  physics/runtime follow-ups.
- **HARDEN-064 ⇐ ARCH-001.** Satisfied 2026-06-05: ECS collider/rigid-body
  authoring shipped under ADR-0019 without storing solver handles in ECS.
- **PHYSICS-001 ⇐ HARDEN-064, METHOD-001.** Satisfied 2026-06-05:
  `PHYSICS-001` is retired at `CPUContracted` with the first
  `src/physics` world/body descriptor surface and runtime fixed-step bridge.
- **PHYSICS-002 ⇐ PHYSICS-001.** Satisfied 2026-06-06:
  collision broadphase/narrowphase contracts are retired at `CPUContracted`
  on top of the physics world/body descriptor surface.
- **GRAPHICS-093 ⇐ GRAPHICS-092 Slice B blocker.** Satisfied 2026-06-18:
  retained forward lines now have a backend-portable non-indexed `LineQuads`
  topology (`DrawIndirectCount()` / `TriangleList`) while edge-id selection keeps
  the indexed `Lines` bucket. Dynamic line-width residency and Vulkan operational
  proof are retired by `GRAPHICS-092`.
- **GRAPHICS-033B ⇐ GRAPHICS-033A (done).** Diagnostics counters and the
  `VulkanRequestedButNotOperational` startup breadcrumb depend on the
  status / reason enums and the reconciliation matrix wiring.
- **GRAPHICS-033C ⇐ GRAPHICS-033A (done), GRAPHICS-032 (done), GRAPHICS-031
  (done), GRAPHICS-018R (done).** Vulkan recording for the bootstrap
  visible recipe needed the gate seam plus the recipe, default material, and
  operational-transition reset seam already in `tasks/done/`; those artifacts
  are now retired by GRAPHICS-081.
- **GRAPHICS-033D ⇐ GRAPHICS-033A (done), GRAPHICS-033B, GRAPHICS-033C.**
  The opt-in `gpu;vulkan` visible-triangle smoke composed all three
  prior children and runs only on hosts with Vulkan + GLFW; its bootstrap
  fixture is retired and the default-recipe fixture is canonical.

[`HARDEN-083`](HARDEN-083-geometry-source-availability-contract.md) —
geometry source availability and provenance contract retired to `tasks/done/`
on 2026-06-19 at `CPUContracted`. `GeometrySources` now reports exact active
domain, source provenance, and available vertex/node, edge, halfedge, and face
CPU sources separately; topology markers can explain provenance without
pretending missing property sets exist.

[`RUNTIME-117`](RUNTIME-117-geometry-availability-render-lane-resolver.md) —
geometry availability and render-lane resolver retired to `tasks/done/` on
2026-06-19 at `CPUContracted`. Runtime now owns the standard resolver over ECS
source availability plus `RenderSurface`, `RenderEdges`, and `RenderPoints`,
including property-domain support, element counts, and lane diagnostics.

[`RUNTIME-118`](RUNTIME-118-geometry-availability-consumer-migration.md) —
geometry availability consumer migration retired to `tasks/done/` on
2026-06-19 at `CPUContracted`. Runtime packers, extraction, progressive
property resolution, selected bake validation, and primitive-selection
refinement now consume the availability/provenance model instead of using exact
`ActiveDomain` as the common capability gate.

[`RUNTIME-119`](RUNTIME-119-gpu-renderable-availability-snapshot.md) — GPU
renderable availability snapshot retired to `tasks/done/` on 2026-06-19 at
`CPUContracted`. `RenderExtractionCache` exposes a read-only
`GpuRenderableAvailabilityView` keyed by stable entity id, with independent
surface, edge, and point lane residency plus canonical named-buffer facts while
ECS remains free of GPU handles and renderer sidecars.

[`UI-021`](UI-021-sandbox-editor-geometry-availability-migration.md) —
sandbox editor geometry availability migration retired to `tasks/done/` on
2026-06-19 at `CPUContracted`. `Runtime.SandboxEditorUi` now consumes
`Extrinsic.Runtime.GeometryAvailability` for domain windows, visualization
targets, property catalogs, primitive-view commands, render hints, K-Means
affordances, and mesh UV/bake diagnostics while preserving source provenance
labels.

[`GEOM-026`](GEOM-026-cross-domain-vertex-normal-recompute.md) — cross-domain
vertex normal recomputation contracts retired to `tasks/done/` on 2026-06-21
at `CPUContracted`. Geometry now exposes domain-owned CPU normal recompute
modules for halfedge meshes, graphs, and point clouds:
`Geometry.HalfedgeMesh.Vertices.Normals`, `Geometry.Graph.Vertex.Normals`, and
`Geometry.PointCloud.Normals`. The old `Geometry.NormalEstimation` module is
removed in favor of KDTree/default and supplied-index point-cloud recompute
overloads that write canonical `v:normal` property data with diagnostics.

[`RUNTIME-120`](RUNTIME-120-vertex-attribute-binding-resolver.md) — reusable
vertex attribute binding resolver retired to `tasks/done/` on 2026-06-24 at
`CPUContracted`. Runtime now has the GPU-agnostic
`Extrinsic.Runtime.VertexAttributeBinding` resolver for count-matched property
to vertex-channel binding, and the mesh packer routes normal and texcoord reads
through it without changing existing packed output. Follow-up vertex-channel
work remains tracked by `RUNTIME-121` through `RUNTIME-125`.

[`GRAPHICS-099`](GRAPHICS-099-rendering-contract-foundation.md) — rendering
contract foundation retired to `tasks/done/` on 2026-06-24 at
`CPUContracted`. Graphics now exposes
`Extrinsic.Graphics.RenderingContract`, a CPU-only public contract vocabulary
for renderer descriptors, scoped snapshot envelopes, binding intents, shared
recipe slots, view/output recipes, render artifact metadata, deterministic
diagnostics, and fail-closed validation helpers. Current renderer execution,
Vulkan, shaders, runtime integration, UI, and loadable-file behavior remain
unchanged; follow-up implementation stays split across `GRAPHICS-100`,
`GRAPHICS-101`, `GRAPHICS-102`, `RUNTIME-127`, `UI-023`, and `GRAPHICS-103`.

[`GRAPHICS-100`](GRAPHICS-100-current-renderer-contract-adapter.md) — minimal
current-renderer contract adapter retired to `tasks/done/` on 2026-06-24 at
`CPUContracted`. Graphics now exposes
`Extrinsic.Graphics.CurrentRendererContractAdapter`, a data-only adapter that
populates the current promoted renderer descriptor, immutable frame/snapshot
envelopes from `RenderFrameInput` or `RenderWorld`, binding intents for current
material/normal/color/texture/visualization lanes, a default frame-recipe
descriptor, view/output metadata, and deterministic compatibility diagnostics.
Renderer execution, Vulkan, shaders, runtime extraction, UI, and loadable
recipe behavior remain unchanged; operational proof stays owned by
`GRAPHICS-103`.

[`GRAPHICS-101`](GRAPHICS-101-loadable-render-recipe-configs.md) — loadable
rendering recipe config schema and validation retired to `tasks/done/` on
2026-06-24 at `CPUContracted`. Graphics now exposes
`Extrinsic.Graphics.RenderRecipeConfig`, a CPU-only versioned JSON loader and
dry-run preview API that overlays optional recipe config onto caller-provided
renderer contract values. It produces `RenderRecipeDescriptor`,
`ViewOutputRecipeDescriptor`, and `BindingSet` copies without mutating active
renderer state, rejects undeclared slots, unsupported capabilities, unsafe
binding domains, invalid defaults, required-binding overrides, and fixed-core
replacement attempts, and reports distinct invalid/unsupported/stale/degraded/
fallback-applied states. Runtime activation, UI editing, shared recipe
execution, Vulkan, shaders, and backend behavior remain deferred to
`RUNTIME-127`, `UI-023`, `GRAPHICS-102`, and `GRAPHICS-103`.

[`RUNTIME-127`](RUNTIME-127-render-artifact-publication.md) — render artifact
publication and apply semantics retired to `tasks/done/` on 2026-06-24 at
`CPUContracted`. Runtime now exposes
`Extrinsic.Runtime.RenderArtifactPublication`, a CPU-only registry and command
surface for renderer-produced artifacts keyed by renderer, snapshot,
view/output recipe, source revisions, and output purpose. It records lifecycle
kinds, UI-facing unpublished/stale/canceled/failed/superseded/published/applied
states, diagnostics, provenance, undo metadata, and audit entries. Publish and
apply are explicit and provenance-gated; apply is limited to candidate project
results and authorizes caller-owned mutation without letting the registry,
graphics, or renderers mutate project data implicitly. UI editing remains owned
by `UI-023`, and image-producing Vulkan/render-graph proof remains owned by
`GRAPHICS-103`.

[`GRAPHICS-102`](GRAPHICS-102-shared-visibility-lighting-recipe-execution.md) —
shared visibility and lighting recipe execution retired to `tasks/done/` on
2026-06-24 at `CPUContracted`. Graphics now exposes
`Extrinsic.Graphics.SharedRenderRecipeExecution`, a CPU-only shared recipe
executor over immutable `RenderWorld` data and scoped `SnapshotEnvelope`
metadata. It produces renderer-neutral visibility/grouping products
(visible items, rejected diagnostics, grouping keys, batch and instance groups,
LOD selections, spatial partitions, and optional acceleration-structure build
requests) plus lighting/environment products (resolved lights, emissive
geometry identities, environment/probe/volume/tag/quality outputs, intents,
debug modes, and fallbacks). Renderer compatibility checks report missing
capabilities or missing produced products deterministically. Backend command
buffers, Vulkan resources, project mutation, UI activation, and operational
render-graph proof remain owned by `GRAPHICS-103` and `UI-023`.

[`UI-023`](UI-023-render-recipe-ui-editing.md) — sandbox render recipe editing
UI retired to `tasks/done/` on 2026-06-24 at `CPUContracted`.
`Extrinsic.Runtime.SandboxEditorUi` now exposes data-only render recipe editor
models, draft/validation/preview/activation command DTOs, and artifact
publish/apply command routing through runtime-owned state. The attached ImGui
panel lists current renderer descriptors, declared recipe slots, binding
overrides, view/output recipe data, draft diagnostics, preview/activation
state, and render artifact lifecycle rows without UI owning renderer state or
mutating graphics/backend resources directly.

[`GRAPHICS-103`](GRAPHICS-103-vulkan-rendergraph-contract-integration.md) —
Vulkan render-graph contract integration retired to `tasks/done/` on
2026-06-24 at `Operational`. The current renderer now evaluates the
contract-first descriptor, scoped snapshot, binding intent, shared recipe,
view/output, and declared artifact metadata path during frame execution,
fail-closes before render-graph execution on incompatibility, records
unsupported-product, missing-output, degraded-fallback, and artifact diagnostics,
and finalizes declared artifact availability from render-graph execution and
readback outcomes. Opt-in Vulkan smoke coverage proves a declared output
artifact can produce non-empty readback-backed evidence through the contract
path while runtime publication remains runtime-owned.

[`RUNTIME-121`](RUNTIME-121-vertex-color-channel-upload.md) — per-vertex mesh
color channel upload retired to `tasks/done/` on 2026-06-24 at `Operational`.
Runtime mesh packing now resolves count-matched `v:color` through the reusable
vertex-attribute resolver into packed unorm8 color data, graphics uploads that
optional stream beside the current mesh vertex bytes, `GpuGeometryRecord`
publishes `ColorBufferBDA`, and the active default-recipe GpuScene shader path
fetches/interpolates the stream for surface/GBuffer shading. CPU coverage proves
present/absent packer behavior, GpuWorld BDA publication, and
`DirtyVertexAttributes` structural color-stream reupload; the opt-in
`gpu;vulkan` runtime sandbox smoke proves the active deferred path shades a
mesh from `v:color`. Dormant `surface.vert`/`PtrVertexAttr` was deliberately not
used; RUNTIME-122 owns the later GPU SoA migration.

[`RUNTIME-122`](RUNTIME-122-gpu-soa-vertex-channel-storage-and-shader-fetch.md)
— GPU SoA vertex channel storage and shader fetch retired to `tasks/done/` on
2026-06-24 at `Operational`. Runtime mesh, graph, point-cloud, and mesh
primitive-view packers now emit explicit per-channel vertex streams; graphics
stores position, texcoord, normal, and color data as contiguous managed SoA
channel ranges, publishes per-channel BDAs through `GpuGeometryRecord`, and
keeps stable element offsets for draw/culling metadata. The active default
GpuScene surface, depth, selection, line, and point vertex shaders now fetch
from channel BDAs instead of interleaved vertex structs. Focused CPU
packer/GpuWorld/shader-contract coverage, the full CPU-supported CTest gate,
structural validators, and opt-in `gpu;vulkan` runtime sandbox surface plus
line/point smokes passed.

[`RUNTIME-123`](RUNTIME-123-editor-bind-property-as-channel.md) — editor
"bind any property as normals / colors" retired to `tasks/done/` on
2026-06-24 at `CPUContracted`. Runtime now has a
`VertexChannelBindingSet` ECS descriptor consumed by mesh, graph, and
point-cloud packers. The Sandbox Editor property catalog exposes normal/color
binding targets, validates candidate properties through the
`VertexAttributeBinding` resolver, persists per-entity bindings, and stamps
`DirtyVertexAttributes` without direct renderer/RHI upload calls. Focused
SandboxEditorUi, mesh/graph/point-cloud packer, and mesh extraction coverage,
the full CPU-supported CTest gate, structural validators, and regenerated
module inventory passed.

[`RUNTIME-124`](RUNTIME-124-per-channel-partial-uploads.md) — per-channel dirty
tracking and partial GPU uploads retired to `tasks/done/` on 2026-06-24 at
`Operational`. ECS now has fine-grained vertex-channel dirty tags for
positions, texcoords, normals, and colors, while the legacy broad
`DirtyVertexAttributes` path still maps to all non-position attribute streams.
Runtime extraction plans resident mesh, graph, and point-cloud updates as
channel masks and calls `GpuWorld::UpdateGeometryChannels` instead of releasing
and re-uploading geometry when topology, vertex count, and storage layout are
unchanged. Graphics writes only the changed contiguous SoA channel sub-ranges,
coalesces upload barriers for the managed vertex buffer, and reports
full-upload fallbacks for count/storage mismatches. Focused CPU extraction,
GpuWorld, dirty-tag, editor-command, and render-extraction tests passed, and an
opt-in `gpu;vulkan` runtime sandbox smoke proves a vertex-color mutation shades
through the active deferred GpuScene path without a full geometry rebind.

[`RUNTIME-126`](RUNTIME-126-gpu-readback-jobs-and-property-writeback.md) — GPU
readback jobs and result→property write-back retired to `tasks/done/` on
2026-06-25 at `Operational`. `StreamingExecutor` now has a
`WaitingForReadback` park/resume state, `DerivedJobRegistry` exposes readback
job diagnostics and `DrainReadbacks()` resume semantics, and
`Extrinsic.Runtime.GpuReadbackJob` schedules transfer-facade readbacks that
write dimension-checked byte payloads into typed geometry properties on the
main-thread apply phase. Dependent follow-up jobs remain blocked until the
readback job has resumed and applied, preserving existing `SubmitFollowUp` /
`DependsOn` ordering. Focused CPU readback/derived-job/binding tests, explicit
readback streaming integration tests, the full CPU-supported CTest gate, and an
opt-in `gpu;vulkan` readback round-trip smoke passed.

[`GEOM-027`](GEOM-027-property-name-lifetime-contract.md),
[`GEOM-028`](GEOM-028-property-registry-handle-safety.md),
[`GEOM-029`](GEOM-029-const-property-set-validity-contract.md),
[`GEOM-030`](GEOM-030-property-set-const-lookup-migration.md),
[`GEOM-031`](GEOM-031-property-set-naming-normalization.md),
[`GEOM-032`](GEOM-032-bool-property-access-contract.md),
[`GEOM-033`](GEOM-033-erased-property-metadata-catalog.md), and
[`GEOM-051`](GEOM-051-property-system-enhancements.md) — geometry property
system migration blockers retired to `tasks/done/` on 2026-06-28 at
`CPUContracted`. `Geometry.Properties` now uses a single borrowed
`std::string_view` name contract, fail-closed handle validity, const-correct
property lookup, canonical `ShrinkToFit`, non-bool contiguous span/data access,
bool proxy-safe indexed access, erased property descriptors, and reusable live
element ranges across mesh, graph, point-cloud, and const domain views. Focused
property tests cover names, copied handles, invalid handles, const lookup,
descriptor metadata, bool access, and live ranges.

[`GEOM-043`](GEOM-043-remeshing-reprojection-error-bounded-sizing.md) —
remeshing reprojection and error-bounded sizing retired to `tasks/done/` on
2026-06-28 at `CPUContracted`. Adaptive remeshing now exposes a reference
projector backed by closest-face queries, error-bounded Taubin-style sizing,
and uniform-remeshing projection options for split/move operations. Focused CPU
tests prove projection to a frozen reference surface and the error-bounded
sizing path on representative meshes.

[`GEOM-044`](GEOM-044-subdivision-sqrt3-loop-feature-masks.md) — subdivision
utility migration retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
Loop subdivision now preserves optional feature-edge masks through crease
stencils and tag propagation, and the new
`Geometry.HalfedgeMesh.SubdivisionSqrt3` module adds triangle-centered Sqrt(3)
subdivision. Focused CPU tests cover the single-triangle Sqrt(3) split and Loop
feature-mask propagation.

[`GEOM-046`](GEOM-046-mesh-topology-utilities.md) — mesh topology utilities
retired to `tasks/done/` on 2026-06-28 at `CPUContracted`. `HalfedgeMesh` now
has polygon-face triangulation, removal safety, Delaunay predicates/flips,
explicit edge-length cache publication, connected-component labeling and split
helpers, largest-component retention, dual/triangle-adjacency construction, and
deterministic nearest-face queries. Focused CPU tests cover triangulation,
component labels/splits, adjacency, nearest-face ordering, and the canonical
`e:length` cache.

[`GEOM-047`](GEOM-047-graph-pointcloud-query-noise-utilities.md) — graph and
point-cloud query/noise utilities retired to `tasks/done/` on 2026-06-28 at
`CPUContracted`. Graph utilities now publish and validate canonical `e:length`
edge caches, run closest/K/radius edge queries through deterministic BVH-backed
candidate search, support one-ring constrained closest-edge lookup, and apply
seeded Gaussian graph noise scaled by bounding-box diagonal. Point-cloud
utilities now apply seeded Gaussian noise scaled by average spacing and
fail-close on degenerate nonzero-noise requests. Focused CPU tests cover cache
publication, query ordering against brute force, one-ring search, deterministic
noise, identity cases, and degenerate diagnostics.

[`CORE-003`](CORE-003-engine-config-file-lane.md) — engine config file lane
retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Extrinsic.Core.Config.EngineLoad` now provides a versioned
`intrinsic.core.engine-config` JSON schema, side-effect-free preview,
file-load, serialization, typed diagnostics, and fallback-applied usability
state while keeping the value-type `EngineConfig` module free of IO imports.
Runtime exposes `ResolveEngineConfigForBoot(...)`, which starts from
`CreateReferenceEngineConfig()` and then applies CLI, environment, or existing
default-path config files before sandbox `Engine` construction. Focused core
tests cover every boot field, file round-trip, invalid-key/value fallback, and
diagnostics; runtime contract tests cover CLI selection and missing explicit
path fallback; the sandbox target builds with the new entry-point wiring.

[`GEOM-035`](GEOM-035-mesh-surface-point-sampling.md) — triangle-mesh surface
point sampling retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Geometry.PointCloud.SurfaceSampling` now samples triangle meshes into
point-clouds with area-weighted face selection, sqrt-corrected barycentric
samples, deterministic seeds, explicit diagnostics, and `p:normal` publication
from interpolated source `v:normal` or geometric fallback normals. Focused
geometry tests cover area proportions, determinism, normal handling, and
invalid-input diagnostics; the benchmark smoke runner includes a CPU-only
surface-sampling workload and manifest with no performance claim.

[`GEOM-036`](GEOM-036-sampling-quality-metrics.md) — blue-noise and sampling
quality metrics retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Geometry.PointCloud.QualityMetrics` now exposes deterministic CPU functions
for nearest-neighbor distances, NN histograms, coefficient of variation,
minimum pair distance, Poisson-disk ratio, coverage, RDF with rectangular-domain
edge correction, 2D periodograms, and radially averaged power spectra. Focused
geometry tests cover grid, jittered-grid, white-noise, regular-lattice, cloud
adapter, and fail-closed edge cases; the benchmark smoke runner includes a
CPU-only quality-metrics workload and manifest with no performance claim.

[`RUNTIME-133`](RUNTIME-133-method-figure-data-export.md) — method figure
data-export seam retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Extrinsic.Runtime.MethodFigureExport` now serializes copied metric series,
scalar summaries, run manifests, and point sets to deterministic CSV, JSON,
and ASCII PLY with stable column/property ordering and 17-digit scientific
float formatting. Writers validate inputs before opening output, commit via a
same-directory temporary file, and fail closed with explicit diagnostics.
Focused runtime unit tests cover metric round-trip parsing, byte-identical
manifest ordering, point-set CSV/PLY output, invalid arrays, duplicate keys,
and unwritable target paths.
