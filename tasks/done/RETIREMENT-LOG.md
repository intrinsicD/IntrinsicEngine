# Retirement Log

Append-only narrative record of retired tasks, newest first. When retiring a
task, append its summary block here (see `docs/agent/task-format.md`,
"Retiring a task") instead of editing `tasks/active/README.md`. Links are
relative to `tasks/done/`, which sits at the same depth as `tasks/active/`,
so blocks moved from the old active-README history work verbatim.

## Retired task narratives

Backlog
[`RORG-031C`](RORG-031C-runtime-composition.md) — runtime composition
backlog seed — retired to `tasks/done/` on 2026-06-10. The seed's job was
to replace the unnamed runtime composition narrative gap with concrete
child tasks, and that is done: `RUNTIME-099` (explicit lifecycle pipeline
with shutdown determinism, `CPUContracted`), `RUNTIME-100` (scene
lifecycle), and `RUNTIME-102` (editor command history) are retired, while
`RUNTIME-101` (asset ingest state machine), `RUNTIME-103` (geometry
algorithm execution queue), and `RUNTIME-104` (derived overlay producer
lifecycle) remain independently tracked Theme F backlog tasks synchronized
with the `LEGACY-011` feature map. Theme A now has no open members.

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
