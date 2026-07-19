# Runtime Backlog Index

This README is the agent-facing entry point into the `tasks/backlog/runtime/`
queue. It lists runtime-owned backlog work and cross-links rendering tasks
whose ownership lives in `src/runtime` even when the task file is filed under
another backlog directory.

## Runtime backlog tasks

### Main-loop non-blocking and composition-root cleanup (seeded 2026-07-03)

Opened from the main-loop/task-graph/render-graph review
([`docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`](../../../docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md)).
Blocking fixes first, then abstractness seams, then steady-state efficiency:

- `RUNTIME-140` is retired; runtime import apply now uses per-asset completion
  and event flushes instead of a global `Scheduler::WaitForAll()` barrier.
- `RUNTIME-141` is retired; heavy Sandbox editor CPU method commands now queue
  runtime derived jobs, keep rendering advanceable while jobs run, and suppress
  duplicate active same-output submits instead of blocking the ImGui callback.
- `RUNTIME-142` is retired; dropped/editor model-scene and texture imports plus
  editor scene save/load now use the runtime streaming lane instead of blocking
  the frame path.
- `RUNTIME-143` is retired; the renderer runtime frame-command hook is now a
  multi-subscriber registry. Its temporary Engine-owned generic GPU participant
  seam was retired by `RUNTIME-137`, which moved the K-Means GPU participant
  path onto the `JobService` `GpuQueue` registry.
- `RUNTIME-144` is retired; post-import processors, import authoring/default
  UX, and the `F` focus action now register through runtime seams while sandbox/default
  composition owns the default bundle (normal-bake registration coordinates
  with `RUNTIME-129`/`RUNTIME-137`/`GRAPHICS-104`).
- `RUNTIME-145` is retired; it delivered steady-state frame-path efficiency
  polish (incremental `StableEntityLookup`, `StreamingExecutor` slot recycling,
  dirty-gated pre-render flush, extraction/import allocation cleanup).

Related core-layer work from the same review lives in
[`tasks/backlog/architecture/`](../architecture/README.md) (`BUG-055`,
`CORE-005..009`, `ARCH-006`).

### Runtime.Engine decomposition (seeded 2026-07-06)

`Runtime.Engine.{cppm,cpp}` (~1,070 / ~6,260 lines) accreted one public
facade per landed task and is now the runtime layer's dominant god-module
and compile hotspot. This series keeps `Engine` as the concrete composition
root (lifecycle, ownership, explicit frame skeleton) and relocates domain
facades into engine-owned subsystem objects, following the existing
`SelectionController`/`EditorCommandHistory`/`GizmoInteraction` accessor
pattern. `RUNTIME-146` through `RUNTIME-165` are retired. Runtime device
selection, backend factory dispatch, Vulkan fallback breadcrumb policy, GPU
asset fallback-texture descriptor construction, legacy mesh primitive-view
compatibility translation, reference-scene lifecycle control, and runtime
input-action descriptor/state/dispatch policy, plus runtime-module contribution
scheduling and dispatch, plus selection pick readback correlation state, drain
policy, refined primitive cache ownership, frame-pacing diagnostics,
ImGui/render-graph counter mirroring, and Dear ImGui overlay/adapter/callback
bridge ownership plus renderer overlay attachment, plus JobService GPU-queue
renderer-hook ownership and participant shutdown sequencing, plus object-space
normal bake GPU-queue service ownership, dependency setup, JobService
participant registration, diagnostics access, transform-gizmo interaction state,
undo storage, selected-entity scratch, packet building, and gizmo-vs-selection
input interlock, plus render-extraction cache ownership, render-world pool
state, last extraction stats, frame-index ownership, GPU asset cache ownership,
asset-event listener wiring, model texture/model scene handoff ownership,
pending material-binding re-resolution, and asset-residency teardown ordering,
plus persistent `StreamingExecutor` / `DerivedJobRegistry` ownership,
maintenance drains, shutdown reset, and derived-job facade delegation now live
outside `Runtime.Engine.cpp`.
`RUNTIME-129` remains the owner of production Vulkan bake plan-provider wiring.
Retired `GRAPHICS-128` supplies the nonzero shared-index-slice command
contract; `RUNTIME-183` remains the accepted composition prerequisite.
`RUNTIME-154` keeps the existing reference-scene public facade while moving
provider resolution, population state, camera-seed caching, and teardown policy
into `Extrinsic.Runtime.ReferenceSceneControl`. `RUNTIME-155` keeps the
existing input-action registration facade while moving descriptor/state/dispatch
policy into `Extrinsic.Runtime.InputActions`.

Sequencing against the ADR-0024 kernel seams
([`docs/adr/0024-kernel-module-architecture.md`](../../../docs/adr/0024-kernel-module-architecture.md),
priority set `ARCH-007`..`ARCH-012`): the seams take precedence for
architecture/runtime picks. `RUNTIME-150`/`RUNTIME-151` touch the same
`RunFrame()` and Engine-interface surfaces the seams wire into and are
front-matter gated (`RUNTIME-150` on `ARCH-007`/`ARCH-008`, `RUNTIME-151`
additionally on `ARCH-011`). `ARCH-013` completed the post-seam re-review:
`RUNTIME-147` kept `Engine::GetAssetImportPipeline()` as a transitional
composition accessor that should be shapeable into a narrow service/module seam
later, while `RUNTIME-148` and `RUNTIME-149` remained unchanged mechanical
extractions. `RUNTIME-146` is retired; boot-time config resolution now lives in
the free-standing `Extrinsic.Runtime.EngineConfigBoot` module. `RUNTIME-147` is
retired; asset import now lives in `Extrinsic.Runtime.AssetImportPipeline`.
`RUNTIME-148` is retired; its first scene-persistence subsystem is superseded
by retired `RUNTIME-172`'s app-composed
`Extrinsic.Runtime.SceneDocumentModule`. `RUNTIME-149` is retired; render-recipe
activation and hot-config control now live in
`Extrinsic.Runtime.EngineConfigControl`. `RUNTIME-150` is retired, and
`RUNTIME-167` replaced its one-consumer partition; the frame-loop hook adapters
and per-frame helpers now live in the include-only
`Runtime.Engine.FrameLoop.Internal.hpp` helper. `RUNTIME-151` is retired;
StableId signal tracking is owned by `StableEntityLookupSceneBinding`, and
`Runtime.Engine.cppm` no longer exposes EnTT.
`RUNTIME-152` is retired; device bootstrap policy now lives in
`Extrinsic.Runtime.DeviceBootstrap`, with `Engine` remaining the platform
window/device-initialize composition caller. `RUNTIME-153` is retired; legacy
mesh primitive-view compatibility translation now lives in
`Extrinsic.Runtime.MeshPrimitiveViewControls`, with `Engine` retaining the
public compatibility methods as delegating facades. `RUNTIME-154` is retired;
reference-scene lifecycle control now lives in
`Extrinsic.Runtime.ReferenceSceneControl`, with `Engine` retaining the public
reference-scene registry/state/camera-seed accessors as delegating facades.
`RUNTIME-155` is retired; runtime input-action registration and dispatch policy
now lives behind `Extrinsic.Runtime.InputActions`. `RUNTIME-156` is retired;
registered runtime-module sim-system/frame-hook records, deterministic ordering,
fixed-step pass insertion, and frame-hook dispatch now live behind
`Extrinsic.Runtime.ModuleSchedule`. `RUNTIME-157` is retired; selection pick
readback correlation state, refined primitive cache ownership, and the readback
drain bridge now live behind `Extrinsic.Runtime.SelectionReadback`.
`RUNTIME-158` is retired; the exported frame-pacing diagnostics record and
ImGui/render-graph counter-copy policy now live behind
`Extrinsic.Runtime.FramePacingDiagnostics`.
`RUNTIME-159` is retired; runtime-side Dear ImGui overlay/adapter/callback
ownership, per-frame Begin/End bracketing, capture reads, diagnostics access,
and renderer overlay attachment first moved into Engine-private
`ImGuiEditorBridge` implementation glue. Retired `RUNTIME-182` absorbed and
deleted that bridge in the app-composed `EditorUiModule`, preserving the
paired frame bracket through runtime-module hooks.
`RUNTIME-160` is retired; the renderer frame-command hook token,
`JobService::RecordGpuQueueFrameCommands(...)` delegation, and GPU-participant
shutdown sequencing now live behind `Extrinsic.Runtime.JobServiceGpuQueueBridge`.
`RUNTIME-161` is retired; object-space normal bake GPU-queue ownership,
ready-frame dependency setup, JobService participant registration, queue
diagnostics access, and shutdown dependency clearing now live behind
`Extrinsic.Runtime.ObjectSpaceNormalBakeService` without closing the remaining
production Vulkan work in `RUNTIME-129`.
`RUNTIME-162` is retired; transform-gizmo frame state, selected-entity scratch,
gizmo/selection pointer interlock, and transform-gizmo packet building now live
behind `Extrinsic.Runtime.GizmoFrameService` while preserving the public Engine
gizmo accessors.
`RUNTIME-163` is retired; `RenderExtractionCache`, render-world pool, last
extraction stats, frame-index ownership, and render-extraction facade delegation
now live in an Engine-private `RenderExtractionService`.
`RUNTIME-164` is retired; `Graphics::GpuAssetCache`, the cache's
`AssetEventBus` listener token, `AssetModelTextureHandoff`,
`AssetModelSceneHandoff`, fallback bootstrap delegation, pending material
binding re-resolution, and asset-residency teardown ordering now live behind
an Engine-private `AssetResidencyService`.
`RUNTIME-165` is retired; persistent `StreamingExecutor` /
`DerivedJobRegistry` ownership, maintenance drains, shutdown reset, and
derived-job facade delegation first moved behind
`Extrinsic.Runtime.AsyncWorkService`. Retired `RUNTIME-179` folded that interim
service into the app-composed `AsyncWorkModule`, added world-qualified
retirement, and removed the Engine facades.

### ADR-0027 app-composition convergence (seeded 2026-07-18)

[ADR-0027](../../../docs/adr/0027-right-sized-runtime-composition.md)
right-sizes the ADR-0024 destination around observable ownership instead of
wrapper counts. It keeps the lean app-to-runtime lifecycle boundary while
requiring every registrar/service/schedule feature to gain a production
consumer or disappear. Extension-pass registration, a priority input chain,
`InlineModule`, and `WorldSwitchModule` are deferred behind named real-consumer
triggers. The implementation graph is:

- Retired `RUNTIME-179` composes the global streaming/derived-job owner and
  removes its Engine facades.
- Retired `RUNTIME-180` — compose global
  viewport/controller state with world-qualified targets and move initial
  reference content to app bootstrap.
- Retired `RUNTIME-181` composes the one global validated config
  preview/apply and app-section owner while kernel startup remains
  omission-safe.
- Retired `RUNTIME-172` — the document/history owner is composed with one
  active-world binding, exact module/history publication, optional async file
  operations, and the narrow synchronous scene-replacement participant
  contract.
- [`RUNTIME-188`](RUNTIME-188-extract-scene-interaction-module.md) — compose
  selection/lookup/readback/gizmo ownership after the document, camera, and
  editor-capture seams exist; copied world-tagged snapshots replace
  Engine-owned interaction pointers, while obsolete mesh-view compatibility
  facades/cache are deleted and ECS render-hint components remain authoritative.
- [`RUNTIME-183`](RUNTIME-183-extract-asset-workflow-module.md) — compose the
  global asset/residency/import/bake owner after the hard document/interaction
  split; require exact document/history and built-in device/renderer/extraction
  services, optionally consume streaming/selection, and borrow kernel config/
  initialized state without camera or config-control-module ownership. Borrowed
  world handoffs never become hidden ECS ownership.
- Retired `RUNTIME-182` composes the optional global ImGui/host owner, with
  one frame-local kernel capture value, a preserved paired Begin/End bracket,
  and app-owned Sandbox panels.
- Corrected [`RUNTIME-168`](RUNTIME-168-privatize-sandbox-default-policies-surface.md)
  — after `RUNTIME-188` and `RUNTIME-183`, delete the one-consumer exported
  policy module, retain its `.cpp` as a private implementation unit of the
  existing Sandbox editor-facade module, and let Sandbox own transactional
  typed handles over only the published import pipeline/input registry plus
  optional exact camera/selection for focus.
- Existing [`RUNTIME-129`](RUNTIME-129-schedule-gpu-normal-bake-after-import.md)
  completes the operational Vulkan bake inside `AssetWorkflowModule` after
  retired `GRAPHICS-128` made the shared managed-index subrange selectable.
- [`RUNTIME-184`](RUNTIME-184-replace-application-lifecycle.md) — remove
  `IApplication` and unrestricted app ticks through explicit Sandbox/module
  composition, isolated from residual API migration and the final
  representation/checker ratchet.
- [`RUNTIME-185`](RUNTIME-185-prune-runtime-composition-mechanisms.md) —
  separately remove/narrow every lifecycle/setup/service/schedule feature
  still lacking a production consumer after both explicit app lifecycle and
  the operational normal-bake consumer graph have landed.
- [`RUNTIME-186`](RUNTIME-186-retire-engine-auxiliary-surface.md) — settle
  residual frame-pacing/render-extraction observation and input-action setup
  APIs, remove Engine re-exports, and migrate callers without absorbing a
  missed domain-owner correction.
- [`RUNTIME-187`](RUNTIME-187-finalize-domain-free-engine-surface.md) — final
  representation-only PImpl plus exact Engine import/getter/type/re-export
  checker ratchet.

Active
[`GRAPHICS-127`](../../active/GRAPHICS-127-native-gpu-timestamp-profiler.md)
follows retired `RUNTIME-181`/`RUNTIME-182` so its profiling config and Frame
Graph UI use the settled owners.
Retired `RUNTIME-177` added no generic debug-draw producer seam because its
consumer inventory was empty; existing spatial-debug and transform-gizmo
paths remain typed. `RUNTIME-129` and `RUNTIME-184` may proceed independently
after their respective prerequisites (`RUNTIME-183` remains for the bake;
`GRAPHICS-128` is retired); both gate `RUNTIME-185`.
`ARCH-014` reaches this graph through `RUNTIME-187`; `REVIEW-003` reaches it
transitively through `ARCH-014`.

#### Retired decomposition entries

- [`RUNTIME-182` — Extract the editor-UI composition module](../../done/RUNTIME-182-extract-editor-ui-module.md)
  (done, 2026-07-19, `Operational`): one app-composed `EditorUiModule` owns
  the ImGui overlay/adapter, exact Engine-free editor host, global visibility
  action, paired frame hooks, capture finalization, and adapter diagnostics.
  Sandbox registers its app-owned frame contribution and windows explicitly;
  Engine owns only the frame-local capture value and exact kernel built-ins.
  The interim bridge and all Engine editor/ImGui facades are gone.
- [`RUNTIME-181` — Extract the config-control composition module](../../done/RUNTIME-181-extract-config-control-module.md)
  (done, 2026-07-19, `Operational`): one app-composed
  `EngineConfigControl` owns validated live preview/apply and app-section
  callbacks; shared plain recipe activation preserves configured frame-zero
  behavior when the optional module is omitted. Engine has no control facade,
  registry, or accessor.
- [`RUNTIME-179` — Extract the async-work composition module](../../done/RUNTIME-179-extract-async-work-module.md)
  (done, 2026-07-19, `Operational`): one app-composed `AsyncWorkModule` owns
  streaming/derived work, maintenance, shutdown drain, world-retirement
  cancellation, and stale-commit rejection. Engine retains only the optional
  domain-free maintenance-hook lookup.
- [RUNTIME-165 — Extract async work service out of Engine](../../archive/RUNTIME-165-extract-async-work-service.md)
  (done, 2026-07-09, `Operational`): persistent `StreamingExecutor` /
  `DerivedJobRegistry` ownership, maintenance-lane completion/readback drains,
  count-limited main-thread apply, background pumping, shutdown reset ordering,
  and derived-job facade delegation now live in
  `Extrinsic.Runtime.AsyncWorkService`. `Engine` keeps lifecycle/frame ordering,
  dependent subsystem wiring, and public derived-job compatibility facades.
- [RUNTIME-164 — Extract asset residency service out of Engine](../../archive/RUNTIME-164-extract-asset-residency-service.md)
  (done, 2026-07-09, `Operational`): GPU asset cache ownership, cache
  asset-event listener wiring, model texture/model scene handoff ownership,
  fallback bootstrap delegation, pending material-binding re-resolution, and
  asset-residency teardown ordering now live in an Engine-private
  `AssetResidencyService`. `Engine` keeps lifecycle/frame
  ordering plus public `GetAssetService()` / `GetGpuAssetCache()`
  compatibility facades.
- [RUNTIME-163 — Extract render extraction service out of Engine](../../archive/RUNTIME-163-extract-render-extraction-service.md)
  (done, 2026-07-09, `Operational`): live render-extraction cache, render-world
  pool, last extraction stats, and frame-index ownership now live in an
  Engine-private `RenderExtractionService`. `Engine` keeps render-frame phase
  ordering plus public render-extraction compatibility facades.

### Retired compile-hotspot decomposition (seeded 2026-07-09)

The CI-latency audit retained in retired `CI-003` measured the runtime layer's
three largest exported-interface hotspots. Retired `ARCH-006` removed the
`Runtime.SandboxEditorUi.cppm` presentation surface (159.174s baseline) and
replaced it with app-owned `EditorShell` plus presentation-free runtime
facades. The other measured interfaces were `Runtime.Engine.cppm` (140.072s,
owned by `RUNTIME-151` after the mechanical `RUNTIME-146..150` splits) and
`Runtime.RenderExtraction.cppm` (106.935s).

- [`RUNTIME-166`](../../done/RUNTIME-166-slim-render-extraction-module.md) —
  retired after hiding `RenderExtractionCache` private state behind one
  implementation object, slimming the primary interface from 38 to 17 module
  dependency statements, and splitting three independently compiled
  implementation domains without changing extraction behavior. Five paired
  same-host Clang 20 direct-edge samples measured a bounded primary-interface
  median reduction from 40.87s to 20.72s; no whole-build claim is made.

### Module-surface diet candidates (seeded 2026-07-10)

Opened from the `.cppm` compile-time triage. These tasks target low-fanout
module surfaces whose public build-graph value is lower than their interface
compile cost. They preserve behavior and ownership; they are not feature tasks.
`ARCH-006` owns the top Sandbox editor/app hot path, and `RUNTIME-166` owns the
main `Runtime.RenderExtraction` module slimming.

- [`RUNTIME-168`](RUNTIME-168-privatize-sandbox-default-policies-surface.md) —
  after `RUNTIME-188` and `RUNTIME-183`, remove the one-consumer public module
  while retaining its implementation under the existing
  `SandboxEditorFacades` surface; Sandbox privately owns exact provider
  borrows and typed handles, not another runtime owner.
- [`RUNTIME-188`](RUNTIME-188-extract-scene-interaction-module.md) — extract
  the separately audited interaction/readback/gizmo owner and remove its
  Engine facade and borrowed render pointers.

### Retired module-surface diet work

- [`RUNTIME-172`](../../done/RUNTIME-172-extract-scene-document-module.md)
  replaced the broad scene-document surface with one app-composed exact
  document/history owner, one validated active-world binding, optional
  generation-guarded async operations, and a narrow synchronous replacement
  participant contract. Engine retains only two typed temporary participant
  registrations whose named owners are `RUNTIME-188` and `RUNTIME-183`.
- [`RUNTIME-178`](../../done/RUNTIME-178-restore-engine-convergence-budget.md)
  restored and improved the fixed Engine convergence budget to 42 plain
  imports / 21 domain imports / 31 getter names with no temporary debt while
  preserving the operational UV-view and private-service behavior.
- [`RUNTIME-173`](../../done/RUNTIME-173-privatize-kmeans-gpu-job-queue-surface.md)
  retired the Sandbox-session-only K-Means GPU queue into private facade-module
  glue at `CPUContracted`, retaining the request/submission/result DTO contract
  on `SandboxEditorFacades` while removing one standalone module/BMI surface.
- [`RUNTIME-171`](../../done/RUNTIME-171-privatize-asset-residency-service-surface.md)
  retired the Engine-only asset residency service into private Engine module
  glue at `Operational`, preserving cache/listener/model-handoff ownership,
  maintenance, and teardown while removing one standalone module/BMI surface.
- [`RUNTIME-169`](../../done/RUNTIME-169-privatize-render-extraction-service-surface.md)
  retired the Engine-only render extraction service into private Engine module
  glue at `Operational`, preserving cache/pool/stat/frame-index ownership and
  teardown while removing one standalone module/BMI surface.
- [`RUNTIME-174`](../../done/RUNTIME-174-privatize-imgui-editor-bridge-surface.md)
  retired the Engine-only ImGui editor bridge into private Engine module glue
  at `Operational`, preserving its value ownership and implementation bodies
  while removing one standalone module/BMI surface.
- [`RUNTIME-167`](../../done/RUNTIME-167-privatize-engine-frameloop-surface.md)
  retired the one-consumer `Runtime.Engine:FrameLoop` partition into
  include-only Engine implementation glue at `Operational`, preserving the
  frame-loop body byte-for-byte while removing one module/BMI surface.
- [`RUNTIME-170`](../../done/RUNTIME-170-privatize-object-space-normal-gpu-queue-surface.md)
  retired the one-consumer object-space normal bake GPU queue module into
  service-owned private state at `CPUContracted`; production Vulkan wiring
  remains owned by `RUNTIME-129`.

### bcg geometry-processing port integration (seeded 2026-06-26)

Core/runtime work paired with the `bcg_code_base` geometry port gaps tracked in
[`tasks/backlog/geometry/README.md`](../geometry/README.md). The core container
lives here because `geometry` may not own a `core` container; the SpatialDebug
consumer lives here because `runtime` owns composition over `geometry`.

`CORE-004` is retired; `Extrinsic.Core.IndexedHeap` now backs
`Geometry.Graph.ShortestPath` Dijkstra with a true decrease-key frontier.
`RUNTIME-135` is retired; the SpatialDebug closest-face consumer now lives in
`Extrinsic.Runtime.SpatialDebugClosestFace`. Editor method windows for the
ported algorithms are retired `UI-024`/`UI-025`/`UI-026` under the UI backlog.

- [`RUNTIME-175 — Point-cloud consolidation runtime facade, config lane, and backend adapter`](RUNTIME-175-pointcloud-consolidation-runtime-config-integration.md)
  is the engine-integration leaf for the LOP consolidation method family
  (`methods/METHOD-016..020`): an app-owned
  `sandbox.point_cloud_consolidation` section on the generic CORE-009 config
  lane, an `EngineConfigControl` hot-apply path, the
  `ApplySandboxEditorPointCloudConsolidationCommand` editor facade with
  `GeometrySources` writeback, and the `Runtime.ConsolidationBackend` RHI
  fallback adapter. Gated on `methods/METHOD-016`; the GPU job-queue leg is
  gated on `methods/METHOD-020`. Gated on `CORE-009` for the section substrate
  and on `methods/METHOD-016` for the algorithm. Mirrors retired `RUNTIME-134`
  progressive-Poisson playground; the Sandbox panel is `ui/UI-035`; coordinate
  with the app-owned editor structure retired by `ARCH-006`.

### Parameterization family integration (Theme I, seeded 2026-07-13)

- `RUNTIME-176 — Parameterization runtime facade, config lane, and UV view
  model` is retired; its authoritative task record is under
  `tasks/done/RUNTIME-176-parameterization-runtime-config-integration.md`.
  It delivered the engine-integration leaf for the parameterization method
  family (`methods/METHOD-021..026` on the retired `geometry/GEOM-063` typed
  CPU strategy surface): a typed parameterization section subsequently moved
  by CORE-009 onto the generic app-section lane, an
  `EngineConfigControl` hot-apply path, the
  `ApplySandboxEditorParameterizationCommand` editor facade writing UVs back as
  `v:texcoord` via `GeometrySources`, plus a pointer-free
  `SandboxEditorParameterizationViewModel` the UV split view draws. No
  placeholder backend selector landed in its CPU-only slice.
  `METHOD-025`/`METHOD-026` own later optimized/GPU extensions. Mirrors
  `RUNTIME-175`/`RUNTIME-134`; retired `UI-036` delivered the Sandbox panel and
  resizable CPU UV split view on 2026-07-15. Future strategies/backends extend
  that delivered panel and this delivered runtime model rather than assigning
  new ownership to either retired task. The optional GPU-shaded target was
  delivered by retired `GRAPHICS-122` at `Operational` on 2026-07-15; runtime
  wires the graphics-owned target into the delivered panel while preserving
  the CPU-layout fallback. The derived-view rendering decision is
  [ADR-0025](../../../docs/adr/0025-parameterization-uv-view-and-split-view.md).
  Build on the app-owned editor structure retired by `ARCH-006`.

### CPU→GPU vertex-attribute overhaul (Theme B)

A reusable, flexible, fast CPU→GPU vertex-attribute pipeline. Today each
geometry kind has its own packer with inlined property names and a fixed AoS
vertex struct, no vertex color channel, and no way to bind an arbitrary property
as normals/colors. This series fixes that incrementally.

`RUNTIME-125` is retired at `CPUContracted`: it added the PR-fast
SoA-vs-interleaved probe benchmark and planning-only storage/promotion
contracts without adopting an AoS lane. `RUNTIME-139` owns the optional
operational AoS storage path, shader variants, promote-on-edit behavior, and
`gpu;vulkan` parity evidence.

Storage model is fixed by
[`ADR-0022`](../../../docs/adr/0022-vertex-storage-soa-per-channel-streaming.md):
uniform SoA with per-channel streaming.

### CPU↔GPU transfer foundation — readback leg (Theme B)

The forward (CPU→GPU) transfer/binding/scheduling spine and runtime readback leg
are retired: RUNTIME-120's vertex-attribute resolver, RUNTIME-121's structural
mesh vertex-color upload path, RUNTIME-112's `DerivedJobRegistry`,
GRAPHICS-084's visualization property buffers, RUNTIME-124's per-channel
partial uploads, GRAPHICS-095/096/097/098's GPU transfer foundation, and
RUNTIME-126's readback job/write-back path now compose the current promoted
foundation. RUNTIME-126 adds a readback job kind (driving GRAPHICS-096
`DownloadBuffer`) and a readback-to-property write-back binding
(dimension-checked via GRAPHICS-095), so algorithms can chain follow-ups on
GPU-computed results ("compute -> read back -> derive color/vector-field ->
re-upload -> visible") using the existing `SubmitFollowUp`/`DependsOn` edges.
This foundation is recorded in
[`ADR-0023`](../../../docs/adr/0023-cpu-gpu-transfer-foundation.md).
`RUNTIME-137` is retired; the async readback helper is the sanctioned compute
backend drain path and `JobService` owns the `GpuQueue` participant registry.
Those historical graphics and queue prerequisites for `RUNTIME-129` are
satisfied; the task is currently gated only on `RUNTIME-183` so object-space
normal bake GPU submission lands inside the accepted AssetWorkflow owner.

`RUNTIME-111` through `RUNTIME-115` are retired; additional progressive
render-data follow-ups should open as value-gated tasks with a concrete
consumer.

### Render output artifact publication (Theme B)

Renderer outputs become runtime-owned artifacts before any project data changes.
This keeps output lifetime, diagnostics, provenance, and publish/apply behavior
observable to UI, agents, tests, and reproducibility tooling.

`RUNTIME-127` is retired; runtime now has a render artifact registry, lifecycle
states, UI-facing status vocabulary, and explicit provenance-carrying
publish/apply commands for candidate renderer outputs.

### Retired developer-experience decisions

The 2026-07 right-sizing audit observed that the snapshot batch's
`DebugLines/DebugPoints/DebugTriangles` fields have no runtime writer, so a
trivially scoped debug primitive costs 8–11 files across 4 layers.

- [RUNTIME-177 — Immediate-mode debug-draw seam for runtime and method code](../../done/RUNTIME-177-immediate-mode-debug-draw-seam.md)
  (done, 2026-07-19, no capability maturity): the follow-up inventory found no
  production direct writer, so the proposed accumulator/service/config/UI
  stack was rejected. A future producer-owned task must name and land its first
  real caller.

### Runtime adapter umbrellas (clarified by Q tasks; producer modules)

These open the runtime-side producer/owner umbrellas already clarified by the
done-task `Q` follow-ups. Each unblocks one or more rendering pass families.


### Working sandbox runtime path

These tasks fill the runtime-owned gaps between the renderer pass DAG and a
usable sandbox that can show authored mesh, graph, and point-cloud data with
selection and UI. They are ordered after the visible-triangle foundation and
compose with the rendering tasks listed in `tasks/backlog/rendering/README.md`.

`RUNTIME-134` is retired at `CPUContracted`; the Sandbox now exposes the
progressive-Poisson playground for selected point-cloud and mesh inputs.

`RUNTIME-138` is the runtime-owned selected-entity responsiveness task. It
makes the Sandbox editor path read cached selected-entity state, submit
commands/jobs, and move heavy property/channel/UV/scalar derivations out of the
ImGui callback into generation-keyed async runtime jobs. The first landed slice
is visibility-gated model construction plus per-frame domain-window model reuse;
the async cache/job and bounded-apply slices remain open.


## Cross-linked rendering tasks (runtime-owned)

Some rendering backlog tasks are runtime-owned for extraction/wiring even
though they may be filed under another task queue. Runtime reviewers must treat
these as runtime work when scheduling and review:

- `RUNTIME-127` is retired. `GRAPHICS-084` retired the runtime-adapter/property-
  selection side of visualization property-buffer residency; `GRAPHICS-084C`
  retired the opt-in Vulkan smoke evidence.

## Related docs

- [`AGENTS.md`](../../../AGENTS.md) — authoritative repository agent contract.
- [`tasks/backlog/rendering/README.md`](../rendering/README.md) — rendering
  backlog DAG and selection rules.

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [RUNTIME-146 — Extract engine config boot into a free-standing module](../../archive/RUNTIME-146-extract-engine-config-boot-module.md)
  (done, 2026-07-08, `Operational`): boot-time config resolution now lives in
  `Extrinsic.Runtime.EngineConfigBoot`, exporting `CreateReferenceEngineConfig()`,
  `EngineConfigBoot*`, and `ResolveEngineConfigForBoot(...)` without importing
  the full `Engine` interface. Sandbox startup and config-control tests import
  the module directly; `Runtime.Engine.cppm`/`.cpp` contain no boot-helper
  declarations or definitions.
- [RUNTIME-147 — Extract the runtime asset-import pipeline out of Engine](../../archive/RUNTIME-147-extract-asset-import-pipeline-subsystem.md)
  (done, 2026-07-08, `Operational`): runtime asset import now lives in
  `Extrinsic.Runtime.AssetImportPipeline`, including the import/reimport/queue/
  cancel facade, ingest records, import event log, default-policy registries,
  decode/materialize helpers, queue snapshot/cancel/clear state, and import
  dirty-state marking. `Engine` keeps only
  `GetAssetImportPipeline()` and platform drop delegation; Sandbox default
  policies, editor UI, and runtime tests call the pipeline directly.
- [RUNTIME-148 — Extract the scene-document facade out of Engine](../../archive/RUNTIME-148-extract-scene-document-subsystem.md)
  (done, 2026-07-08, `Operational`): runtime scene persistence now lives in
  `Extrinsic.Runtime.SceneDocument`, including direct and queued scene
  save/load, new/close document behavior, the scene-file event log, serializable
  scene snapshots, and the replacement cleanup/rebuild ordering. `Engine` keeps
  only `GetSceneDocument()` for this surface; Sandbox editor UI and runtime
  scene lifecycle tests call the document subsystem directly. This is
  historical intermediate evidence; retired `RUNTIME-172` replaces that module
  and getter with the composed exact `SceneDocumentModule` service.
- [RUNTIME-149 — Extract render-recipe and hot-config control out of Engine](../../archive/RUNTIME-149-extract-engine-config-control-subsystem.md)
  (done, 2026-07-08, `Operational`): runtime render-recipe activation and
  engine-config hot-subset control now live in
  `Extrinsic.Runtime.EngineConfigControl`, including recipe preview/apply/clear,
  startup recipe activation, hot-subset preview/apply diagnostics, boot-only
  rejection, and the active config-control state. `Engine` keeps only
  `GetConfigControl()` for this surface; Sandbox editor UI and runtime
  config-control tests call the subsystem directly.
- [RUNTIME-150 — Split the frame-loop hook adapters out of Runtime.Engine.cpp](../../archive/RUNTIME-150-split-engine-frame-loop-implementation-unit.md)
  (done, 2026-07-08, `Operational`): runtime frame-loop hook adapters and
  per-frame helpers were first moved into the private
  `Extrinsic.Runtime.Engine:FrameLoop` partition. `RUNTIME-167` subsequently
  replaced that one-consumer module surface with include-only private header
  glue; `Engine::RunFrame()` remains on `Engine` and preserves frame shape and
  ordering.
- [RUNTIME-151 — Slim the Engine module interface and remove the entt leak](../../archive/RUNTIME-151-slim-engine-interface-and-remove-entt-leak.md)
  (done, 2026-07-08, `Operational`): StableId signal tracking now lives behind
  `StableEntityLookupSceneBinding` in `Extrinsic.Runtime.StableEntityLookup`;
  `Runtime.Engine.cppm` holds the binding but no EnTT include, token, scoped
  connection, or callback declaration. The interface moved from 733 lines / 50
  imports / 2 EnTT includes directly before the task to 721 lines / 46 imports
  / 0 EnTT tokens, with lookup, selection, scene-lifecycle, and full CPU gate
  verification passing.
- [RUNTIME-152 — Extract runtime device bootstrap out of Engine](../../archive/RUNTIME-152-extract-runtime-device-bootstrap.md)
  (done, 2026-07-09, `Operational`): runtime device-selection policy, backend
  factory dispatch, Vulkan-requested breadcrumb policy, and GPU asset fallback-
  texture descriptor construction now live in
  `Extrinsic.Runtime.DeviceBootstrap`. `Engine` remains the platform window and
  device-initialize composition caller.
- [RUNTIME-153 — Extract mesh primitive-view controls out of Engine](../../archive/RUNTIME-153-extract-mesh-primitive-view-controls.md)
  (done, 2026-07-09, `Operational`): legacy `MeshPrimitiveViewSettings`
  compatibility translation now lives in
  `Extrinsic.Runtime.MeshPrimitiveViewControls`, which applies, clears, and
  reads the authoritative ECS `RenderEdges` / `RenderPoints` components. The
  `Engine` methods remain compatibility facades and no longer import render-
  geometry component policy directly.
- [RUNTIME-154 — Extract reference-scene lifecycle control out of Engine](../../archive/RUNTIME-154-extract-reference-scene-control.md)
  (done, 2026-07-09, `Operational`): reference-scene lifecycle control now
  lives in `Extrinsic.Runtime.ReferenceSceneControl`, including provider
  registration/resolution, installed population state, camera-seed caching, and
  provider teardown. `Engine` keeps `GetReferenceSceneRegistry()`,
  `IsReferenceSceneInstalled()`, and `GetReferenceCameraSeed()` as delegating
  facades.
- [RUNTIME-155 — Extract runtime input-action registry out of Engine](../../archive/RUNTIME-155-extract-runtime-input-action-registry.md)
  (done, 2026-07-09, `Operational`): runtime input-action descriptor/service/
  context/handle types, handle allocation, action storage, key-edge trigger
  checks, ImGui keyboard-capture suppression, callback failure logging, and
  per-frame dispatch now live in `Extrinsic.Runtime.InputActions`. `Engine`
  re-exports the API for compatibility and keeps `RegisterInputAction(...)` /
  `UnregisterInputAction(...)` as delegating facades.
- [RUNTIME-156 — Extract runtime-module schedule out of Engine](../../archive/RUNTIME-156-extract-runtime-module-schedule.md)
  (done, 2026-07-09, `Operational`): runtime-module sim-system/frame-hook
  records, deterministic dependency ordering, frame-hook ordering, fixed-step
  pass insertion/context construction, and frame-hook dispatch now live in
  `Extrinsic.Runtime.ModuleSchedule`. `Engine` still owns module objects,
  built-in service provisioning, `OnRegister` / `OnResolve` sequencing, and
  shutdown calls as delegating composition.
- [RUNTIME-157 — Extract selection readback state out of Engine](../../archive/RUNTIME-157-extract-selection-readback-state.md)
  (done, 2026-07-09, `Operational`): selection pick readback correlation,
  completed readback draining, primitive refinement, and the editor-facing
  refined primitive cache now live in `Extrinsic.Runtime.SelectionReadback`.
  `Engine` keeps public accessors as delegating compatibility facades.
- [RUNTIME-158 — Extract frame pacing diagnostics out of Engine](../../archive/RUNTIME-158-extract-frame-pacing-diagnostics.md)
  (done, 2026-07-09, `Operational`): `RuntimeFramePacingDiagnostics` and the
  ImGui/render-graph counter mirroring helpers now live behind
  `Extrinsic.Runtime.FramePacingDiagnostics`; `Engine` keeps the public
  diagnostics accessor and phase timing composition.
- [RUNTIME-159 — Extract ImGui editor bridge out of Engine](../../archive/RUNTIME-159-extract-imgui-editor-bridge.md)
  (done, 2026-07-09, `Operational`): runtime Dear ImGui adapter, overlay
  system ownership, editor callbacks, renderer overlay attachment, per-frame
  Begin/End bracketing, capture reads, and diagnostics access now live in an
  Engine-private `ImGuiEditorBridge` implementation service. `RUNTIME-182`
  subsequently absorbs and deletes that interim bridge in the app-composed
  `EditorUiModule`.
- [RUNTIME-160 — Extract JobService GPU queue bridge out of Engine](../../archive/RUNTIME-160-extract-jobservice-gpu-queue-bridge.md)
  (done, 2026-07-09, `Operational`): renderer runtime-frame hook ownership,
  JobService GPU-queue command recording, and participant shutdown sequencing
  now live behind `Extrinsic.Runtime.JobServiceGpuQueueBridge`.
- [RUNTIME-161 — Extract object-space normal bake service out of Engine](../../archive/RUNTIME-161-extract-object-space-normal-bake-service.md)
  (done, 2026-07-09, `Operational`): object-space normal bake GPU-queue
  ownership, dependency setup, ready-frame callback construction, JobService
  participant registration, diagnostics access, pending-count access, and
  dependency clearing now live in
  `Extrinsic.Runtime.ObjectSpaceNormalBakeService`. `RUNTIME-129` remains open
  for production Vulkan plan-provider and `gpu;vulkan` smoke closure.
- [RUNTIME-162 — Extract gizmo frame service out of Engine](../../archive/RUNTIME-162-extract-gizmo-frame-service.md)
  (done, 2026-07-09, `Operational`): transform-gizmo interaction state, undo
  storage, selected-entity scratch, gizmo/selection pointer interlock, and
  transform-gizmo packet building now live in
  `Extrinsic.Runtime.GizmoFrameService`. `Engine` keeps frame ordering plus the
  public gizmo interaction and undo-stack compatibility facades.
- [RUNTIME-140 — Remove the global scheduler barrier from the import apply path](../../archive/RUNTIME-140-remove-global-waitforall-from-import-apply.md)
  (done, 2026-07-05, `CPUContracted`): runtime import materialization now
  drains only the specific `AssetId` load/event through
  `AssetService::CompleteCpuLoadAndFlushEvent(...)`; focused regressions prove
  unrelated scheduler work stays in flight while import apply completes.
- [RUNTIME-142 — Async model-scene/texture import and scene-file IO](../../archive/RUNTIME-142-async-modelscene-texture-scenefile-io.md)
  (done, 2026-07-05, `Operational`): dropped model-scene/texture imports,
  Sandbox editor model-scene/texture imports, and Sandbox editor scene
  save/load now queue `StreamingExecutor` work, keep file IO and decode/parse/
  serialize work off the frame callback path, and apply results on the bounded
  main-thread drain. A slow fake IO backend regression proves the frame loop
  advances while queued texture reads remain blocked.
- [RUNTIME-143 — Multi-subscriber frame-command hook and K-Means decoupling from Engine](../../archive/RUNTIME-143-frame-hook-registry-and-kmeans-decoupling.md)
  (done, 2026-07-05, `Operational`): `IRenderer` now exposes deterministic
  add/remove runtime frame-command hook registration. Its temporary
  Engine-owned runtime GPU participant seam has been retired by `RUNTIME-137`;
  K-Means GPU queue command recording, maintenance drains, in-flight checks,
  and post-idle teardown now route through the `JobService` `GpuQueue`
  participant registry.
- [RUNTIME-144 — Post-import processor and import UX-policy seam](../../archive/RUNTIME-144-post-import-processor-and-ux-policy-seam.md)
  (done, 2026-07-06, `Operational`): `Engine` now owns generic post-import
  processor, import-authoring, import-completed, and input-action dispatch
  registries only; `Extrinsic.Runtime.SandboxDefaultPolicies` installs the
  sandbox default direct-mesh generated-normal processor, authoring defaults,
  focus/auto-select import UX, and `F` focus action from the app composition
  side.
- [RUNTIME-125 — Optional AoS fast lane for static geometry](../../archive/RUNTIME-125-aos-static-fast-lane.md)
  (done, 2026-07-02, `CPUContracted`): PR-fast SoA/probe benchmark evidence and
  planning-only storage-lane/promotion contracts landed without allocating an
  AoS GPU lane or selecting shader variants. Operational AoS storage/shaders,
  promote-on-edit behavior, and Vulkan parity remain owned by open follow-up
  `RUNTIME-139`.
- [RUNTIME-136 — Sandbox method backend selectors](../../archive/RUNTIME-136-sandbox-method-backend-selectors.md)
  (done, 2026-07-02, `CPUContracted`): the Sandbox exposes CPU/GPU backend
  selectors for K-Means and Progressive Poisson, with requested-vs-actual
  backend readouts and fallback diagnostics.
- [RUNTIME-135 — SpatialDebug closest-face picking via accelerated mesh query](../../archive/RUNTIME-135-spatialdebug-closest-face-picking.md)
  (done, 2026-06-28, `CPUContracted`): runtime now has a data-only closest-face
  SpatialDebug overlay consumer that caches the GEOM-039 mesh closest-face
  index by active mesh key/revision and emits deterministic fail-closed overlay
  diagnostics without renderer/RHI/Vulkan or editor/UI ownership.
- [CORE-004 — Indexed decrease-key min-heap container and Dijkstra adoption](../../archive/CORE-004-indexed-decrease-key-heap.md)
  (done, 2026-06-28, `CPUContracted`): core now exports
  `Extrinsic.Core.IndexedHeap`, a deterministic fail-closed indexed binary
  min-heap with `TryTop`, `TryPop`, O(log n) `DecreaseKey`, and O(log n)
  `Remove`; geometry Dijkstra now uses it as the live frontier while preserving
  shortest-path and diagnostic parity against the prior lazy priority-queue
  implementation.
- [RUNTIME-132 — Lift single-use RunFrame hook adapters out of the RunFrame body](../../archive/RUNTIME-132-lift-runframe-hook-adapters.md)
  (done, 2026-06-28): `Engine::RunFrame` now delegates single-use frame-hook
  adapters, fixed-step substeps, camera/gizmo/selection input, BUG-026
  pick-context capture, and completed pick-readback refinement to private
  implementation helpers while preserving the documented frame order.
- [RUNTIME-128 — Default lit material for material-less imported primitives](../../archive/RUNTIME-128-default-lit-material-for-materialless-imports.md)
  (done, 2026-06-28, `CPUContracted`): model-scene imports now give
  material-less primitives a neutral lit StandardPBR default while preserving
  slot 0 as the unlit missing/invalid material indicator.
- [RUNTIME-127 — Render artifact publication and apply semantics](../../archive/RUNTIME-127-render-artifact-publication.md)
  (done, 2026-06-24, `CPUContracted`): runtime now has a render artifact
  registry, lifecycle states, UI-facing status vocabulary, and explicit
  provenance-carrying publish/apply commands for candidate renderer outputs.
- [RUNTIME-109 — Extensible mesh attribute texture bake pipeline](../../archive/RUNTIME-109-extensible-mesh-attribute-texture-bakes.md)
  (done, 2026-06-15, `CPUContracted`): generic runtime CPU mesh-attribute
  texture bakes now cover resolved-UV vertex/face scalar, label, vector2,
  vector3/normal, and RGBA outputs with stable generated texture keys.
- [RUNTIME-120 — Reusable vertex attribute binding resolver](../../archive/RUNTIME-120-vertex-attribute-binding-resolver.md)
  (done, 2026-06-24, `CPUContracted`): runtime now has a CPU-only
  property-to-vertex-channel resolver with fail-closed diagnostics, and the mesh
  packer routes normal and texcoord reads through it without behavior change.
- [RUNTIME-121 — Per-vertex color channel through the geometry vertex stream](../../archive/RUNTIME-121-vertex-color-channel-upload.md)
  (done, 2026-06-24, `Operational`): mesh `GeometrySources` now resolve
  count-matched `v:color` into packed unorm8 upload data, publish it through
  `GpuGeometryRecord::ColorBufferBDA`, consume it in the active default-recipe
  GpuScene surface/GBuffer shader path, and prove the path with CPU contracts,
  dirty-reupload coverage, and an opt-in `gpu;vulkan` smoke.
- [RUNTIME-122 — GPU SoA vertex channel storage and shader fetch](../../archive/RUNTIME-122-gpu-soa-vertex-channel-storage-and-shader-fetch.md)
  (done, 2026-06-24, `Operational`): runtime mesh, graph, point-cloud, and mesh
  primitive-view packers now emit explicit channel streams; graphics stores
  position, texcoord, normal, and color data as managed SoA channel ranges,
  publishes per-channel BDAs through `GpuGeometryRecord`, and the active
  GpuScene surface, depth, selection, line, and point shaders fetch through the
  channel BDAs. Focused CPU coverage, the default CPU gate, structural
  validators, and opt-in `gpu;vulkan` surface plus line/point smokes passed.
- [RUNTIME-123 — Editor "bind any property as normals / colors"](../../archive/RUNTIME-123-editor-bind-property-as-channel.md)
  (done, 2026-06-24, `CPUContracted`): runtime now has a
  `VertexChannelBindingSet` ECS descriptor consumed by mesh, graph, and
  point-cloud packers; the Sandbox Editor property catalog exposes normal/color
  binding targets, validates candidate properties through the
  `VertexAttributeBinding` resolver, persists per-entity bindings, and stamps
  `DirtyVertexAttributes` without direct renderer/RHI upload calls.
- [RUNTIME-124 — Per-channel dirty tracking and partial GPU uploads](../../archive/RUNTIME-124-per-channel-partial-uploads.md)
  (done, 2026-06-24, `Operational`): ECS exposes fine-grained vertex-channel
  dirty tags for positions, texcoords, normals, and colors; runtime extraction
  maps resident mesh, graph, and point-cloud edits to `GpuWorld` channel update
  masks; graphics writes only changed SoA channel ranges and preserves full
  uploads for topology, vertex-count, and storage-layout changes.
- [RUNTIME-126 — GPU readback jobs and result→property write-back in the derived-job graph](../../archive/RUNTIME-126-gpu-readback-jobs-and-property-writeback.md)
  (done, 2026-06-25, `Operational`): runtime readback jobs now park in
  `WaitingForReadback`, resume through `DerivedJobRegistry::DrainReadbacks()`
  after transfer delivery, write dimension-checked bytes into typed geometry
  properties, expose readback diagnostics, and keep follow-up jobs pending until
  write-back apply completes.
- [RUNTIME-119 — GPU renderable availability snapshot](../../archive/RUNTIME-119-gpu-renderable-availability-snapshot.md)
  (done, 2026-06-19, `CPUContracted`): `RenderExtractionCache` now exposes a
  read-only GPU availability view keyed by stable entity id, with independent
  surface, edge, and point lane residency plus named-buffer facts.
- [RUNTIME-118 — Geometry availability consumer migration](../../archive/RUNTIME-118-geometry-availability-consumer-migration.md)
  (done, 2026-06-19, `CPUContracted`): runtime packers, extraction,
  progressive property resolution, selected bake validation, and primitive
  refinement now consume source/provenance availability instead of using exact
  `ActiveDomain` as the common capability gate.
- [RUNTIME-117 — Geometry availability and render-lane resolver](../../archive/RUNTIME-117-geometry-availability-render-lane-resolver.md)
  (done, 2026-06-19, `CPUContracted`): runtime now owns the standard resolver
  over ECS source availability plus `RenderSurface`, `RenderEdges`, and
  `RenderPoints`.
- [RUNTIME-115 — Selected mesh bake command surface](../../archive/RUNTIME-115-selected-mesh-bake-command-surface.md)
  (done, 2026-06-17, `CPUContracted`): selected mesh property texture bakes
  now route through a runtime-owned command surface with validation, generated
  texture payload reload, optional progressive binding updates, command-history
  dirtying, synchronous test hooks, and observable derived-job apply/stale
  diagnostics.
- [RUNTIME-114 — Progressive import enrichment pipeline](../../archive/RUNTIME-114-progressive-import-enrichment-pipeline.md)
  (done, 2026-06-16, `CPUContracted`): model-scene mesh leaves can publish raw
  geometry immediately, attach progressive surface bindings, and queue
  observable UV, normal, normal-bake, and albedo-bake jobs through
  `DerivedJobRegistry` while GPU residency remains texture-handoff owned.
- [RUNTIME-113 — Progressive domain presentation extraction](../../archive/RUNTIME-113-progressive-domain-presentation-extraction.md)
  (done, 2026-06-16, `CPUContracted`): runtime extraction consumes progressive
  presentation descriptors for mesh surface slots, graph vertex/edge property
  buffers, point-cloud property buffers, diagnostics, and previous-output
  retention without blocking on derived jobs.
- [RUNTIME-112 — Entity derived-job graph and snapshots](../../archive/RUNTIME-112-entity-derived-job-graph.md)
  (done, 2026-06-16, `CPUContracted`): `StreamingExecutor`-backed derived-job
  registry now exposes entity/global snapshots, explicit dependencies,
  follow-up scheduling, stale/cancel/failure diagnostics, and previous-output
  retention.
- [RUNTIME-111 — Progressive render-data descriptor contracts](../../archive/RUNTIME-111-progressive-render-data-descriptors.md)
  (done, 2026-06-16, `CPUContracted`): shared mesh/graph/point-cloud
  progressive descriptors, slot/source/readiness/generated-output policy,
  property compatibility diagnostics, and scene serialization now exist without
  raw property pointers or GPU handles.
- [RUNTIME-110 — Progressive entity render-data pipeline clarification](../../archive/RUNTIME-110-progressive-entity-render-data-pipeline.md)
  (done, 2026-06-16, `Scaffolded`): accepted ADR-0021's progressive
  mesh/graph/point-cloud render-data model and split implementation into
  `RUNTIME-111` through `RUNTIME-114`, `UI-015`, and `GRAPHICS-090`.
- [RUNTIME-101 — Asset ingest state-machine migration](../../archive/RUNTIME-101-asset-ingest-state-machine.md)
  (done, 2026-06-15, `CPUContracted`): promoted the runtime ingest
  request/result state machine for manual import, dropped files, and reimport;
  routed Engine import entry points through shared diagnostics and duplicate/
  stale guards; and kept reimport as same-`AssetId` `AssetService` reload
  without reviving ECS or scene-file asset-source coupling.
- [RUNTIME-108 — Remove mesh UV normal fallback](../../archive/RUNTIME-108-resolved-uv-render-residency.md)
  (done, 2026-06-13, `CPUContracted`): mesh surface packing now requires
  count-matched finite `v:texcoord` and never substitutes oct-encoded normals
  into `MeshVertex::U/V`; missing or invalid texture coordinates fail closed
  with explicit runtime extraction diagnostics.
- [RUNTIME-105 — Remove the deprecated GetStreamingGraph() TaskGraph bridge](../../archive/RUNTIME-105-remove-streaming-graph-bridge.md)
  (done, 2026-06-15, `Retired`): deleted the promoted runtime
  `Engine::GetStreamingGraph()` compatibility accessor, the private streaming
  task graph, and the per-frame TaskGraph-to-`StreamingExecutor` conversion.
  `StreamingExecutor` is now the only promoted runtime streaming path.
- [RUNTIME-103 — Geometry algorithm execution queue](../../archive/RUNTIME-103-geometry-algorithm-execution-queue.md)
  (done, 2026-06-15, `CPUContracted`): value-gated the promoted editor
  geometry-processing path and retained synchronous CPU K-Means as the intended
  endpoint for current workflows. No runtime async algorithm queue or CUDA
  follow-up is owed without a new concrete workload.
- [RUNTIME-107 — Headless-capable Engine::Run loop coverage](../../archive/RUNTIME-107-headless-engine-loop-coverage.md)
  (done, 2026-06-15, `Operational`): added an explicit
  `WindowBackend::Null` test-facing window backend selector and routed the
  BUG-030 `Engine::Run()` regressions through it so they execute on headless
  hosts instead of skipping.
- [GRAPHICS-084 — Visualization property-buffer residency](../../archive/GRAPHICS-084-visualization-property-buffer-residency.md)
  (done, 2026-06-11, `CPUContracted`): consumed runtime visualization adapter/
  property selections while keeping GPU upload ownership in graphics.
- [RORG-031C — Runtime composition backlog seed](../../archive/RORG-031C-runtime-composition.md)
  (done, 2026-06-10): composition-root and lifecycle backlog work for
  `begin_frame`, extraction, prepare, execute, end, shutdown determinism, and
  subsystem wiring; executed via `RUNTIME-099`/`RUNTIME-100`/`RUNTIME-102`
  with `RUNTIME-101` later retired as the independently tracked asset-ingest
  child.
- [RUNTIME-104 — Derived overlay producer lifecycle](../../archive/RUNTIME-104-derived-overlay-producer-lifecycle.md)
  (done, 2026-06-11, `CPUContracted`): classified legacy
  `Graphics.OverlayEntityFactory` behavior for current workflows and retained no
  persistent runtime overlay producer API. Mesh/graph/point child overlays map
  to ordinary `GeometrySources` entities, mesh edge/vertex overlays use
  component-driven primitive-view sidecars, and vector-field/isoline overlays use runtime
  visualization packets without child ECS entities. Backend command-shape proof
  is retired by
  [GRAPHICS-085](../../archive/GRAPHICS-085-overlay-packet-backend-parity.md).
- [RUNTIME-106 — Render component domain composition](../../archive/RUNTIME-106-render-component-domain-composition.md)
  (done, 2026-06-12, `CPUContracted`): aligned mesh, graph, and point-cloud
  rendering around `RenderSurface`, `RenderEdges`, and `RenderPoints`
  component presence; mesh edge/vertex sidecars are now driven by render
  components rather than `MeshPrimitiveViewSettings`, and unsupported
  point-cloud surface/edge requests fail closed with diagnostics.
- [RUNTIME-091 — Activate promoted ECS system bundle in fixed-step runtime](../../archive/RUNTIME-091-promoted-ecs-system-bundle-activation.md)
  (done): runtime-owned activation of promoted ECS systems via
  `Extrinsic.Runtime.EcsSystemBundle::RegisterPromotedEcsSystemBundle`, called
  every fixed-step substep before `Core::FrameGraph::Compile` so
  `TransformHierarchy` + `BoundsPropagation` run deterministically before
  render extraction.
- [RUNTIME-096 — Runtime module implementation splits](../../archive/RUNTIME-096-runtime-module-implementation-splits.md):
  module-interface hygiene follow-up for promoted runtime `.cppm` targets found
  by the 2026-06-06 implementation-body audit, including camera controllers,
  gizmo helpers, engine helper functions, and geometry packers.
- [RUNTIME-097 — Default sandbox ECS-authored white triangle](../../archive/RUNTIME-097-default-sandbox-ecs-triangle.md)
  (done, 2026-06-07, `CPUContracted`): replaced the default sandbox/reference
  triangle's `ProceduralGeometryRef` bootstrap with ordinary mesh-domain
  `GeometrySources`, selectable/editor-visible components, and a white
  appearance contract while keeping the sandbox app implementation runtime-only.
- [RUNTIME-098 — Promoted scene serialization and editor command seam](../../archive/RUNTIME-098-promoted-scene-serialization.md)
  (done, 2026-06-07, `CPUContracted`): adds backend-neutral JSON scene
  save/load over current sandbox-authored ECS data, runtime `Engine` scene-file
  facades, and Sandbox editor `File / Scene` commands without reviving legacy
  serializer/editor modules.
- [RUNTIME-099 — Runtime lifecycle composition pipeline](../../archive/RUNTIME-099-runtime-lifecycle-composition.md)
  (done, 2026-06-09, `CPUContracted`): `Engine::RunFrame()` carries an
  internal `RuntimeFrameContext` and delegates platform/render/maintenance/
  operational/shutdown phase ordering through promoted `Extrinsic.Core.FrameLoop`
  contracts, replacing legacy render orchestration with explicit runtime stage
  order and shutdown determinism.
- [RUNTIME-100 — Scene manager lifecycle and persistence boundary](../../archive/RUNTIME-100-scene-manager-lifecycle.md)
  (done, 2026-06-09, `CPUContracted`): single runtime scene replacement
  boundary, render-extraction/selection/physics reset contracts, and explicit
  supported/deferred/retired persistence decisions beyond `RUNTIME-098`.
- [RUNTIME-102 — Editor command history and undo/redo seam](../../archive/RUNTIME-102-editor-command-history.md)
  (done, 2026-06-09, `CPUContracted`): runtime/editor command history,
  dirty-state source, recursive delete/orphan policy, undo/redo contracts, and
  Sandbox editor document-state model.
- [RUNTIME-070 — Bootstrap GpuAssetCache fallback texture in Engine::Initialize](../../archive/RUNTIME-070-fallback-texture-bootstrap.md):
  runtime-side graphics-bootstrap step initializing the canonical 4×4 magenta
  fallback texture per GRAPHICS-015Q (done).
- [BUILD-001 — Wire shader compilation to the promoted Sandbox build](../../archive/BUILD-001-sandbox-shader-compile-wiring.md):
  CMake-only task adding `intrinsic_add_glsl_shaders(ExtrinsicSandbox)` so the
  promoted Sandbox build emits SPIR-V binaries; unblocks GRAPHICS-031A pipeline
  loads.
- [RUNTIME-081 — `Extrinsic.Runtime.CameraControllers`](../../archive/RUNTIME-081-camera-controllers.md):
  Orbit / Fly / FreeLook / TopDown camera controllers producing
  `RenderFrameInput::Camera` (clarified by GRAPHICS-017Q; done).
- [RUNTIME-080 — `Extrinsic.Runtime.AssetBridges.Texture`](../../archive/RUNTIME-080-asset-bridges-texture.md)
  _(superseded, retired 2026-06-03)_: texture-typed asset event subscriber
  producing `GpuAssetCache::RequestUpload` calls (clarified by GRAPHICS-015Q).
  The capability shipped under `ASSETIO-001` as
  `Extrinsic.Runtime.AssetModelTextureHandoff`; this umbrella was retired
  without re-implementation.
- [RUNTIME-082 — `Extrinsic.Runtime.SpatialDebugAdapters`](../../archive/RUNTIME-082-spatial-debug-adapters.md)
  (done 2026-05-27): BVH / KD-tree / Octree / ConvexHull adapters producing
  spatial-debug snapshot records (clarified by GRAPHICS-011Q). All four
  slices landed (umbrella + BvhAdapter → KdTree + Octree adapters →
  ConvexHull adapter + registry → `ExtractAndSubmit` wiring +
  `ECS::Components::SpatialDebugBinding` + cache-owned adapters); retired
  to `tasks/done/`.
- [RUNTIME-083 — `Extrinsic.Runtime.VisualizationAdapters`](../../archive/RUNTIME-083-visualization-adapters.md)
  (done 2026-06-02, `CPUContracted`):
  PropertySet / KMeans / Isoline / VectorField / HtexMetadata adapters producing
  visualization packet spans (clarified by GRAPHICS-014Q).
- [RUNTIME-084 — `Extrinsic.Runtime.GizmoInteraction`](../../archive/RUNTIME-084-gizmo-interaction.md)
  (done 2026-06-06, `CPUContracted`): transform-gizmo hit testing,
  translate/rotate/scale drag application, undo emission, Engine
  input/camera/selection wiring, and
  `RuntimeRenderSnapshotBatch::TransformGizmos` submission (clarified by
  GRAPHICS-017Q).
- [RUNTIME-090 — `Extrinsic.Runtime.ImGuiAdapter`](../../archive/RUNTIME-090-imgui-platform-renderer-adapter.md)
  (done, 2026-06-02, `CPUContracted`): Dear ImGui platform/renderer adapter
  producing `ImGuiOverlayFrame` records for `ImGuiOverlaySystem::SubmitFrame`
  (clarified by GRAPHICS-013CQ).
- [RUNTIME-085 — `GeometrySources` mesh residency bridge](../../archive/RUNTIME-085-geometrysources-mesh-residency.md)
  (retired to `tasks/done/` 2026-05-28 at `CPUContracted`):
  runtime-authored ECS mesh data (`Vertices`/`Edges`/`Halfedges`/`Faces`) to
  retained `GpuWorld` surface geometry. Promoted from backlog 2026-05-27;
  Slice A landed the `Extrinsic.Runtime.MeshGeometryPacker` module (mesh
  `GeometrySources` → `GpuWorld::GeometryUploadDesc` triangle-list shape
  with fail-closed `MeshPackStatus` taxonomy), Slice B the extraction wiring,
  and Slice C the dirty-domain reupload + deferred-retire ordering.
  `Operational` visual proof is closed by RUNTIME-095.
- [RUNTIME-086 — `GeometrySources` graph residency bridge](../../archive/RUNTIME-086-geometrysources-graph-residency.md):
  graph nodes/edges to retained point and line geometry. _(retired to
  `tasks/done/` on 2026-05-30 at maturity `CPUContracted`; Slice A — graph
  packer — plus Slices B + C — `RenderExtractionCache` residency wiring — all
  landed.)_
- [RUNTIME-087 — `GeometrySources` point-cloud residency bridge](../../archive/RUNTIME-087-geometrysources-pointcloud-residency.md):
  point-cloud vertices to retained point geometry. _(retired to `tasks/done/` on
  2026-05-30 at maturity `CPUContracted`; standalone point-cloud packer plus
  `RenderExtractionCache` residency wiring, deferred-retire, and shutdown drain
  landed together.)_
- [RUNTIME-088 — Mesh primitive view lifecycle](../../archive/RUNTIME-088-mesh-primitive-view-lifecycle.md)
  _(done 2026-05-31 at maturity `CPUContracted`: optional mesh edge/vertex render
  views as runtime sidecars over the authoritative mesh `GeometrySources`,
  wired into `RenderExtractionCache`; `Operational` visual proof closed by
  RUNTIME-095.)_
- [RUNTIME-089 — Runtime selection controller and snapshot handoff](../../archive/RUNTIME-089-selection-controller.md)
  _(done; retired 2026-05-31 at `CPUContracted`)_: input/pick-result policy,
  selected/hovered state, and `RenderWorld.Selection` submission. Slice A
  (standalone `Extrinsic.Runtime.SelectionController` module) landed at
  `Scaffolded`; Slice B wired `Engine::RunFrame` (pick drain + readback consume)
  and `RenderExtractionCache::ExtractAndSubmit` (`RenderWorld.Selection` mirror)
  to close `CPUContracted`.
- [RUNTIME-092 — Runtime stable entity lookup sidecar](../../archive/RUNTIME-092-stable-entity-lookup.md)
  _(done 2026-05-31, `CPUContracted`)_: runtime-owned `StableId`/live-entity
  lookup for selection and editor tooling. Slice A landed the standalone
  `Extrinsic.Runtime.StableEntityLookup` module (`HARDEN-068` Decision-3 deferred
  sidecar) with deterministic smallest-render-id duplicate policy and lazy stale
  invalidation; Slice B wired it into `Engine::RunFrame` (per-frame `Rebuild`
  before the pick-readback drain) and routed the `SelectionController` render-id
  resolution seam through the sidecar to close `CPUContracted`.
- [RUNTIME-093 — Primitive selection refinement](../../archive/RUNTIME-093-primitive-selection-refinement.md)
  (done, 2026-06-01, `CPUContracted`): mesh face/edge/vertex, graph edge/node, and
  point-cloud point refinement from graphics primitive hints plus authoritative
  `GeometrySources`, wired into `Engine::RunFrame` via `RefinePickReadbackResult`.
- [RUNTIME-095 — Working sandbox app acceptance path](../../archive/RUNTIME-095-working-sandbox-acceptance.md):
  done 2026-06-04 at `Operational` on Vulkan-capable hosts; final CPU/null +
  opt-in Vulkan acceptance for mesh, graph, point cloud, camera, selection,
  outline, and UI.
- [RUNTIME-097 — Default sandbox ECS-authored white triangle](../../archive/RUNTIME-097-default-sandbox-ecs-triangle.md)
  _(done 2026-06-07 at maturity `CPUContracted`)_: the default visible triangle
  is an ordinary ECS mesh entity using the same runtime extraction and editor
  inspection paths as loaded mesh objects.
- [RUNTIME-098 — Promoted scene serialization and editor command seam](../../archive/RUNTIME-098-promoted-scene-serialization.md)
  _(done 2026-06-07 at maturity `CPUContracted`)_: scene save/load persists
  current sandbox-authored mesh/graph/point-cloud ECS data and is exposed
  through the runtime-owned Sandbox editor scene-file command surface.
- [GRAPHICS-016 — Runtime extraction and graphics handoff](../../archive/GRAPHICS-016-runtime-extraction-handoff.md):
  - Runtime owns live ECS access, extraction, sidecar/cache mappings from ECS
    entities and asset/source handles to graphics handles, dirty-domain
    interpretation, deletion events, and compaction/relocation handoff.
  - Graphics must not import live ECS ownership; promoted graphics layers
    consume snapshots/views only.
  - GRAPHICS-016 completed the first implementation gate for the rendering
    backlog before most rendering pass implementation work begins. See
    the rendering DAG in
    [`tasks/backlog/rendering/README.md`](../rendering/README.md) for
    downstream ordering.
- [`GRAPHICS-001 — Rendering parity inventory and task index`](../../archive/GRAPHICS-001-rendering-parity-inventory.md) —
  retired rendering parity seed; current rendering selection lives in the
  rendering backlog DAG above.
