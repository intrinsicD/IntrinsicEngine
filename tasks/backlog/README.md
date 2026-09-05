# Backlog Tasks

Backlog tasks are approved or proposed work items that have not started yet.

This index describes **open** work only. Retired-task narratives and fully
satisfied dependency anchors live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); per-category
detail (including each category's retired members) lives in the category
READMEs.

## Categories

- [`architecture/`](architecture/) — architecture and layering decisions.
- [`assets/`](assets/) — promoted CPU asset authority and import/export ingest.
- [`benchmarks/`](benchmarks/) — benchmark manifests, runners, baselines, and
  matched product evidence.
- [`bugs/`](bugs/) — reproducible correctness bugs and harness defects.
- [`ecs/`](ecs/) — promoted ECS scene/components/systems hardening.
- [`geometry/`](geometry/) — geometry algorithms, IO, and method readiness.
- [`methods/`](methods/) — paper/method packages following the method workflow.
- [`physics/`](physics/) — physics layer ownership and phenomena roadmap.
- [`platform/`](platform/) — windowing/input port and explicit platform backends.
- [`process/`](process/) — agentic-workflow and process-infrastructure hardening.
- [`rendering/`](rendering/) — renderer, frame graph, and RHI work.
- [`runtime/`](runtime/) — runtime composition root and lifecycle.
- [`ui/`](ui/) — editor/UI integration seams.
- [`workshop/`](workshop/) — clean-workshop task pack: guardrails, boundary
  fixes, typed routing, renderer decomposition, maturity taxonomy, and
  architecture review gate.

## Convergence themes

Use this section when picking the next active task. Each theme groups backlog
work that converges on one engine outcome; "cross-domain dependency anchors"
record the edges agents must respect when selecting work, so per-category DAGs
stay globally aligned.

The agent contract in [`/AGENTS.md`](../../AGENTS.md) is the authoritative
source for the engine mission and layering invariants. Themes below describe
how the *current* backlog maps onto that contract.

### Theme J — Framework24 product convergence (P0, active gate)

Reach full user-facing Framework24 feature and workflow parity while making
IntrinsicEngine the demonstrably better replacement in modularity,
extensibility, reliability, usability, and performance. Framework24 is the
behavioral baseline, not an architecture or implementation blueprint;
IntrinsicEngine may redesign its architecture and algorithms through the
normal reviewed process. The current C++23/Vulkan, layer-ownership, and
correctness/reliability contracts remain quality floors until explicitly
revised, while “better” never excuses a missing feature. The authoritative
inventory, workflows, scorecard, measurement rules, and stop condition are in
[`docs/product/framework24-convergence.md`](../../docs/product/framework24-convergence.md).

Retired `ARCH-017` established the mission/picker/scorecard reset. `REVIEW-004` is the
one-shot final gate and depends on the current bounded product owners:
`ASSETIO-012`, `BENCH-001`, `BUG-159..160`,
`GRAPHICS-135`, `METHOD-015`, `RUNTIME-218`, and `UI-046..051`. `METHOD-015`
is an existing Framework24 registered-feature gap, not a reopening of the
unrelated research queue. The remaining atlas work is split: `BUG-159`
removes per-chart global remap cost, and `BUG-160` repairs or replaces the
fragmenting chart policy after the remap fix.
`BUG-154`, `BUG-156`, and `BUG-158` are satisfied dependencies of `REVIEW-004`,
with `BENCH-001` retaining the unfulfilled claim-grade parity gate.

While `REVIEW-004` is open, Theme J, reproducible Theme G regressions, and
correctness/reliability repairs required by a golden workflow are the only new
work-selection lanes. Theme I research expansion and speculative Theme B/H
work are paused. Preserve their existing task and evidence state; they resume
after `REVIEW-004` retires with every scorecard row accepted.

### Theme A — Shortest path to sandbox visible geometry (P0, complete)

Complete for the scoped Theme A acceptance scene: the default-recipe renderer,
runtime `GeometrySources` residency for mesh/graph/point-cloud content,
selection/refinement handoff, ImGui/editor UI panels, and the final
`ExtrinsicSandbox` acceptance task (`RUNTIME-095`) are all retired. The
sandbox app implementation remains policy-light and imports runtime only.

Origin: [sandbox geometry rendering gap analysis (2026-05-08)](../../docs/reviews/2026-05-08-sandbox-geometry-rendering-gap-analysis.md);
completed gate inventory:
[2026-05-30 working sandbox app — remaining gates](../../docs/reviews/2026-05-30-sandbox-app-remaining-gates.md).
Member-by-member history: retirement log and
[`rendering/README.md`](rendering/README.md) /
[`runtime/README.md`](runtime/README.md).

Theme A has **no open members**; the last straggler (`RORG-031C`, the
runtime composition backlog seed) retired once its lifecycle children landed
and the remaining runtime children became independently tracked tasks.

### Theme B — Rendering modernization (P1)

Promote the post-reorganization renderer toward 2026-era features without
breaking the foundation. The umbrella roadmap (`GRAPHICS-035`), all
`GRAPHICS-036..058` planning/implementation leaves (pipelined frames, async
compute, HZB occlusion, clustered lights, TAA, and the further modernization
seams), the CPU/GPU transfer foundation (`GRAPHICS-095..098`), and the
contract-first renderer/snapshot/recipe architecture sequence
(`GRAPHICS-099..103`, `RUNTIME-127`, `UI-023`) and the object-space normal
bake graphics dilation and shared-index follow-ups (`GRAPHICS-115` and
`GRAPHICS-128`) and runtime scheduling/production closure (`RUNTIME-129`) are
retired — see
[`rendering/README.md`](rendering/README.md) and the retirement log. Current
state also retires the former profile-gated AoS planning surface through
`RUNTIME-139`; the retained probe has no adoption claim. Open Theme B leaves
include the 2026-07-03 render-graph review leaves — pass contribution seam
(`GRAPHICS-116`), compile caching
(`GRAPHICS-117`), placed transient aliasing (`GRAPHICS-118`), and parallel
command recording (`GRAPHICS-119`); compiler/executor polish (`GRAPHICS-120`)
is retired; see
[`rendering/README.md`](rendering/README.md) and
[`docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`](../../docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md). The runtime GPU readback job/write-back leg
(`RUNTIME-126`) is retired with the transfer facade/readback ring wired into the
runtime derived-job graph.

`GRAPHICS-137` owns the trigger-gated shader-object realization spike from
[ADR-0028](../../docs/adr/0028-declarative-graphics-state-rhi-boundary.md).
`GRAPHICS-136` owns the mechanical RHI surface rename and remains blocked
until the spike records its boundary finding and a second realization lands.

`GRAPHICS-135` is the Theme J current-render-prep profiling owner. It measures
callback registration/rebinding, retained-plan execution, scheduler dispatch,
and material work before proposing changes. Compiled-plan reuse already
exists, and visualization can dirty materials between the two syncs. Its task
record retains the historical diagnostic captures; a negative A/B may close
the investigation without a production rewrite.

The research-control-surface recipe/config seam (see Theme H `PROC-010` for the
proposed framing) opens new Theme B leaves that close the gap where an activated
`RenderRecipeConfig` preview cannot yet reach the live frame. The fail-closed
renderer override seam is retired in `GRAPHICS-106`, and the runtime
activation/default-load lane is retired in `RUNTIME-130`. The
vocabulary/locality cleanup is retired in `GRAPHICS-107`:
`FrameRecipe*` is documented as the live frame driver, `RenderRecipe*` as the
contract/config overlay, and `ProjectFrameRecipeOverride(...)` is
CPU-contract-tested as the constrained bridge between them. `DOCS-004` is
retired: canonical `frame-graph.md` now documents the recipe-config lane.

The Issue 445 literature scan seeded a deliberately deferred incubation set:
`GEOM-066`/`067`, `GRAPHICS-123..126`, and `ASSETIO-009`. Every member lists
retired `REVIEW-003` as its first dependency, so that stability gate is now
satisfied; each leaf is selectable only when its remaining individual
dependencies are also satisfied. The leaves reopen only bounded evidence or vertical-pilot slices from the archived Slang,
meshlet, cluster-LOD, and differentiable-rendering plans; they do not recreate
those broad umbrellas. Historical FetchContent instructions in the archived
plans are superseded by the current vcpkg-manifest-only contract.

The 2026-07-16 old-engine consolidation opened one immediately actionable
Theme B remediation. `GRAPHICS-127` is now retired after completing the
already-exported RHI GPU timestamp profiler across native Vulkan pass
recording, truthful Null provenance, telemetry, and the existing Frame Graph
panel. Its `REVIEW-003` dependency edge is therefore satisfied. The conditional
`GRAPHICS-041` hot-reload watcher child remains unopened until its Slang
compile/reflection prerequisites and a live consumer exist.

### Theme C — Physics readiness (P1)

Define physics layer ownership before any solver code lands; then implement
the ECS authoring, CPU reference, and physics-world/runtime bridge behind that
contract. The ownership decision (ADR-0019), phenomena roadmap, rigid-body
reference method, ECS authoring contract, world/runtime bridge, and
broadphase/narrowphase and constraint/island/sleep solver contracts are retired — see
[`physics/README.md`](physics/README.md) and the retirement log.

Theme C's CPU reference foundation is complete: the rigid-body foundation
(ADR-0019, METHOD-001, PHYSICS-001..003) and all three non-rigid reference
method packages from the phenomena roadmap (METHOD-009 particles/springs,
METHOD-010 XPBD cloth, METHOD-011 SPH fluid) are retired at
`CPUContracted`.
`PHYSICS-004`
retired the otherwise test-only public `PhysicsBridge` after proving the first
real app-composed runtime physics module at `Operational`; optimized/GPU
backends still open separately only through the roadmap's evidence gates.

### Theme D — ECS hardening parity (P0, bounded contract convergence)

Promote ECS scene/hierarchy/component contracts out of `src/legacy` while
keeping `ecs -> core` and explicit geometry handles only. Historical members
(`HARDEN-060..068`, `HARDEN-081`, `HARDEN-083`, and `HARDEN-087`)
are retired. `HARDEN-087` closed the bounded correction that unifies graph
element-source components; see
[`ecs/README.md`](ecs/README.md) and the retirement log.

### Theme E — Geometry IO completion (P0, complete)

Finish geometry-owned IO parity so legacy graphics importers/exporters can
retire and asset ingest can route through promoted decoders. `GEOIO-002` and
its children are retired, and the follow-on geometry-module breadth task
`GEOIO-003` is also retired — see [`geometry/README.md`](geometry/README.md)
and the retirement log.

### Theme F — Architecture/runtime/UI foundation seeds

Keep cross-cutting backlog stubs honest with current state and reachable from
the convergence map. Retired members are indexed in the category READMEs and
the retirement log.

INFRA Option C is accepted through
[ADR-0020](../../docs/adr/0020-vcpkg-manifest-dependency-management.md): the
repository dependency path is vcpkg manifest mode with CI/local binary-cache
wiring. Active `INFRA-001` now tracks only final deprecation cleanup and
warm-cache CI timing evidence.

The geometry availability chain (`HARDEN-083`, `RUNTIME-117`, `RUNTIME-118`,
`RUNTIME-119`, and `UI-021`) is retired to `tasks/done`; ECS, runtime
extraction, progressive helpers, GPU availability inspection, and the sandbox
editor now share the promoted source/provenance and render-lane availability
contracts.

`RUNTIME-138` retired on 2026-08-01 after the coherence review found no
evidence for a broad selected-analysis program. Its delivered visibility
gating, immutable model caches, metadata, diagnostics, and bounded shared
`JobService` completion path remain factual behavior. Any future async
derivation belongs to the named feature whose measurement proves a material
full-buffer cost; no global selected-analysis service is planned.

The 2026-07-24 source-complete runtime surface audit opened the dependency-
ordered `RUNTIME-192..205` remediation set and `PHYSICS-004`, re-gating
`REVIEW-003`. It consolidates canonical property/presentation/work/readback/
clustering/residency/visualization/spatial-debug/import/history concepts,
migrates production workflows, retires the monolithic Sandbox facade,
internalizes one-consumer helpers, and withdraws dormant public modules. The
2026-08-01 coherence pass split final helper deletion into Engine-owned
RUNTIME-203 and SceneInteraction-owned RUNTIME-205, and added RUNTIME-139's
removal of speculative AoS planning. Every
member requires tests and production adoption before its old specialized path
is deleted; none may retire by leaving a permanent compatibility facade.

The 2026-07-16 Sandbox model-workflow audit opened four Theme F leaves:
`ASSETIO-010` owns asynchronous primary/companion-file preview on top of the
route-level prerequisite contract delivered by `BUG-093`; `ASSETIO-011` owns
the app-linked semantic File / Import matrix after its preview/timing/PLY/
queued-geometry dependencies retire; `UI-037` owns runtime-authoritative
linear action readiness and disabled-reason tooltips after `BUG-093`,
`BUG-096`, and `RUNTIME-202`; the 2026-08-02 method integration audit re-scoped
retired `UI-038` from securing a destructive Progressive Poisson conversion to
removing that invalid conversion and exposing the method across all compatible
element sources. The four reproducible Theme G defects
(`BUG-098..101`) are retired: completed-frame timing, binary PLY non-vertex
list consumption, queued manual geometry import, and near-linear UV edge
grouping now satisfy their recorded maturity targets.

The same final opt-in Vulkan/GPU gate exposed two pre-existing test-contract
drifts, now retired as `BUG-102` and `BUG-103`: the object-space bake layering
test recognizes the RUNTIME-178 import-placement ratchet, and the render-graph
lifetime fixture keeps its measured history chain live under execution-rank
semantics. Neither correction changes production behavior.

The 2026-07-16 old-engine consolidation also opened `HARDEN-086`, now retired
at `CPUContracted`: two runtime-local hierarchy walks moved onto deterministic,
all-or-nothing query helpers in the existing promoted ECS structure module.
This Theme F composition/right-sizing leaf did not reopen completed Theme D.

`RUNTIME-131` is retired to `tasks/done`: runtime exposes the agent/CLI
config-control facade for render-recipe preview/activation and the current
engine-config hot subset (`render.default_recipe_config_path`), with the
Sandbox Editor routed through the same facade. After `RUNTIME-149`, that facade
lives in `Extrinsic.Runtime.EngineConfigControl`. Retired `RUNTIME-181`
promoted that facade to an app-composed runtime module resolved through
`Engine::Services()`, preserved omission-safe startup recipe activation, and
removed the transitional Engine accessor. This satisfies the config-control
dependency for `RUNTIME-134`, which is now retired at `CPUContracted`.
`RUNTIME-136` is also retired; operational progressive-Poisson GPU parity is now
owned by `METHOD-014`.

`DOCS-003` is retired to `tasks/done`: `algorithm-variant-dispatch.md` became an
explicit target Strategy x Backend template using `RHI::IDevice`, method-policy
backend tokens, and honest requested-vs-actual fallback telemetry.

`GEOM-052` is retired to `tasks/done`: it introduced the `{CPU, GPU}` backend
tokens and requested-vs-actual fallback telemetry that satisfied `PROC-011`'s
backend-seam dependency. `RUNTIME-196` subsequently converged that historical
wrapper into `ClusteringService::RunKMeans`; the service is now the sole typed
CPU/GPU operation and keeps its RHI-visible Vulkan state private.

The 2026-07-03 main-loop/task-graph/render-graph review
([`docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`](../../docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md))
seeded a Theme F set spanning the core task system and the runtime
composition root: the retired non-blocking import-apply fix `RUNTIME-140`,
the retired async editor method-command lane `RUNTIME-141`, the retired
non-blocking frame-path fixes `RUNTIME-142`, the retired frame-hook/K-Means
decoupling seam `RUNTIME-143`, the retired post-import/import-UX/input-action
composition seam `RUNTIME-144`, the retired Sandbox editor ownership seam
`ARCH-006`, the retired domain-free task/DAG vocabulary seam `CORE-006`, the
retired non-blocking TaskGraph completion seam `CORE-005`, the retired
composition-root/config seam `CORE-009`, the retired scheduler-hardening
slice `CORE-007`, and the retired compiled-plan efficiency work `CORE-008`.
The retired steady-state frame
efficiency polish `RUNTIME-145` removed the recurring runtime frame-path waste
called out by the review. The retired correctness fix `BUG-055` (Theme G)
enabled `CORE-005`.

**North star: [`docs/architecture/kernel-target-state.md`](../../docs/architecture/kernel-target-state.md)**
— the living target and convergence scorecard for ADR-0024 as right-sized by
ADR-0027, owned by the umbrella task `ARCH-014`. Any agent adding runtime
functionality reads its knob-decision table first; the unchecked scorecard
rows are the remaining work.

**Priority entry point (P0 within Theme F): the ADR-0024 kernel/module
architecture seams.** The 2026-07-08 kernel/module architecture decision
record ([`docs/adr/0024-kernel-module-architecture.md`](../../docs/adr/0024-kernel-module-architecture.md))
seeded the seams-first migration set `ARCH-007`..`ARCH-012`; that set is now
retired. It created the registration/communication seams (command bus, event
bus, JobService, WorldRegistry, RuntimeModule contract) that the
`Runtime.Engine` decomposition set (`RUNTIME-146`..`164`) and the module
extractions (`ARCH-006`, `UI-034`) land onto. `ARCH-012` closed the
`Operational` proof by composing `ClusteringModule` through the full command →
job → event → commit path while keeping `KMeans*` out of
`Runtime.Engine.cppm`/`.cpp`. `RUNTIME-146` is retired as the free-standing
config-boot extraction, `RUNTIME-147` as the asset-import pipeline extraction,
`RUNTIME-148` as the scene-document extraction, `RUNTIME-149` as the
config-control extraction, `RUNTIME-150` as the private frame-loop partition
split, `RUNTIME-151` as the Engine-interface cleanup, `RUNTIME-152` as the
device-bootstrap policy extraction, `RUNTIME-153` as the mesh primitive-view
control extraction, `RUNTIME-154` as the reference-scene lifecycle-control
extraction, `RUNTIME-155` as the runtime input-action registry extraction, and
`RUNTIME-156` as the runtime-module contribution schedule extraction,
`RUNTIME-157` as the selection readback/cache state extraction,
`RUNTIME-158` as the frame-pacing diagnostics extraction, `RUNTIME-159`
as the ImGui editor bridge extraction, and `RUNTIME-160` as the
JobService GPU-queue bridge extraction, `RUNTIME-161` as the object-space
normal bake service extraction, `RUNTIME-162` as the gizmo frame service
extraction, `RUNTIME-163` as the render-extraction service extraction,
`RUNTIME-164` as the asset-residency service extraction, and `RUNTIME-165`
as the async-work service extraction. All five additive ADR-0024 seams
(`ARCH-007`..`ARCH-011`) and the collision sweep (`ARCH-013`) are retired;
`ARCH-014` retired on 2026-07-23 after the kernel-convergence scorecard became
all-green.
Retired `GRAPHICS-128` supplies the nonzero shared-index-slice contract;
retired `RUNTIME-129` supplies the production Vulkan normal-bake provider.
Retired `RUNTIME-190` relocated that provider from the AssetWorkflow owner
retired by `RUNTIME-183` into a dedicated `TextureBakeModule` and adds the
generalized interactive property-raster path.
Provider resolution, population state, camera-seed caching, reference-scene
teardown policy, input-action descriptor/state/dispatch policy, and
runtime-module contribution ordering/dispatch, selection readback correlation
and refined primitive cache state, frame-pacing diagnostics, copied
ImGui/render-graph counter mirroring, and Dear ImGui overlay/adapter/callback
bridge ownership plus renderer overlay attachment, plus JobService GPU-queue
renderer-hook ownership and participant shutdown sequencing, plus object-space
normal bake GPU-queue ownership, ready-frame dependency setup, JobService
participant registration, diagnostics access, shutdown dependency clearing,
transform-gizmo frame state, selected-entity scratch, gizmo/selection pointer
interlock, and transform-gizmo packet production now live outside
`Runtime.Engine.cpp`; render-extraction cache/pool/frame-index composition
state now lives directly in `Engine::Impl`, and GPU asset
cache/model-handoff residency ownership now lives in the app-composed
`AssetWorkflowModule`; the persistent streaming executor, derived-job
registry, world-retirement gate, maintenance drains, and shutdown now live in
the app-composed `Extrinsic.Runtime.AsyncWorkModule`, while the Engine derived
facades have been removed.
Accepted
[`ADR-0027`](../../docs/adr/0027-right-sized-runtime-composition.md)
corrects the literal destination: domain responsibilities must be explicitly
app-composed with stated global/world scope, but no wrapper, registry, schedule,
priority chain, extension slot, or experiment builder is created without a
production consumer. The bounded implementation graph is
`RUNTIME-179` AsyncWork, retired `RUNTIME-172` SceneDocument, retired
`RUNTIME-188` SceneInteraction, `RUNTIME-180` Camera, `RUNTIME-181`
ConfigControl, `RUNTIME-183` AssetWorkflow, retired `RUNTIME-182` EditorUi,
retired `RUNTIME-190` TextureBake,
retired post-`RUNTIME-188`/`RUNTIME-183` `RUNTIME-168` private Sandbox
provider/handle composition on the existing editor-facade surface, retired
`RUNTIME-129` operational normal bake after the retired independent
`GRAPHICS-128` shared-index prerequisite,
retired `RUNTIME-184` application-lifecycle removal, retired `RUNTIME-185`
mechanism pruning, retired `RUNTIME-186` residual auxiliary-surface cleanup,
and retired `RUNTIME-187` exact Engine-surface ratchet. The original normal-bake leaf is
retired; its right-sized generalized module consolidation is retired as
`RUNTIME-190`; the lifecycle and mechanism-pruning leaves are also retired, so
the convergence graph is closed at `12/0/0/5`. The detailed graph
and state scopes live in the [runtime backlog index](runtime/README.md).
Retired `RUNTIME-182` extracted the optional global ImGui/host owner while
preserving the existing paired frame bracket and one completed capture
snapshot. Retired
`RUNTIME-180` extracted the
camera owner and app-owned reference bootstrap; the corrected `RUNTIME-172`
document owner is retired with exact service publication. `RUNTIME-188` now
is retired with the separate interaction owner, exact optional selection
service, and `26/4/2/15` Engine ratchet. `RUNTIME-183` is retired with the
app-composed AssetWorkflow owner and `22/0/2/10` Engine ratchet.
`RUNTIME-168`, `RUNTIME-129`, `RUNTIME-184`, `RUNTIME-185`, `RUNTIME-186`,
and `RUNTIME-187` are retired; `ARCH-014` records the closed endpoint.
Sequencing note: tasks whose deliverable ADR-0024 supersedes are
front-matter gated on their seam dependencies — `RUNTIME-150` on
`ARCH-007`/`ARCH-008`, `RUNTIME-151` additionally on `ARCH-011`, `ARCH-006`
and `UI-034` on `ARCH-012`, `RUNTIME-138` on
`ARCH-007`/`ARCH-009` — are now unblocked where those dependencies were the
only front-matter blocker. `ARCH-013` is retired: it confirmed the gated rows,
recorded per-task decisions for the audit rows, re-scoped `RUNTIME-137` as the
`JobService` `GpuQueue`/readback substrate, re-gated `RUNTIME-129` on
`RUNTIME-137`, and marked `RUNTIME-147`'s `Engine::GetAssetImportPipeline()`
as a transitional composition accessor rather than a new cross-module pattern.
`RUNTIME-150` is retired as the private frame-loop partition split, and
`RUNTIME-151` is retired as the Engine-interface cleanup. `RUNTIME-137` is
retired as the JobService `GpuQueue`/async readback substrate, satisfying that
historical prerequisite for `RUNTIME-129`; retired `GRAPHICS-128` also closed
nonzero managed-index slices, and retired `RUNTIME-183` supplied its accepted
AssetWorkflow owner.
The non-blocking TaskGraph substrate (`CORE-005`) and scheduler hardening
(`CORE-007`) are retired, as is compiled-plan efficiency (`CORE-008`).

The 2026-08-02 element-domain/method-integration audit opened foundational
`HARDEN-087`, now retired, plus `RUNTIME-206..210` and `UI-038..042`. Its
2026-08-03 clarification made typed properties—not `Vertices` or
handle-specific wrappers—the semantic boundary and opened
`RUNTIME-211..213`, `UI-043..045`, and geometry API closures `GEOM-073..074`.
The `RUNTIME-208`/`UI-038` Progressive Poisson provenance pair remains valid
retired work; `RUNTIME-212`/`UI-044` own the newly explicit property-domain
extension without rewriting that history. See the
[runtime](runtime/README.md) and [UI](ui/README.md) indexes for the dependency
split and the
[audit](../../docs/reviews/2026-08-02-method-engine-integration-contract-audit.md)
for the revised scoped inventory.
The LOP property-domain pair `RUNTIME-206`/`UI-039` is now fully retired;
remaining rows keep their named runtime/UI owners.

Open members (kernel-seam priority set first):
- [`assets/ASSETIO-010-async-model-companion-preflight.md`](assets/ASSETIO-010-async-model-companion-preflight.md).
- [`assets/ASSETIO-011-semantic-sandbox-file-import-workflow-matrix.md`](assets/ASSETIO-011-semantic-sandbox-file-import-workflow-matrix.md) (blocked by `ASSETIO-010`; `BUG-098`, `BUG-099`, and `BUG-100` are satisfied dependencies).
- [`ui/UI-037-linear-domain-action-readiness-tooltips.md`](ui/UI-037-linear-domain-action-readiness-tooltips.md)
  (blocked by `BUG-096` and facade retirement; expensive readiness derivations
  are feature-owned only when concretely required).
- [`platform/PLATFORM-004-alternative-platform-backend-onboarding.md`](platform/PLATFORM-004-alternative-platform-backend-onboarding.md) (planning-only seed).

The 2026-08-07 Sandbox UI workflow pass (`sculpt.obj` driven end-to-end through
the promoted Vulkan build) opened the capability-gap set below alongside
`BUG-137..142` in Theme G. `UI-051` covers only the remaining domain windows;
the method panels stay owned by `RUNTIME-209/211/212/213` and
`UI-040/041/043/044/045`, and disabled-reason tooltips stay owned by `UI-037`.
- [`runtime/RUNTIME-218-default-scene-lighting-and-light-authoring.md`](runtime/RUNTIME-218-default-scene-lighting-and-light-authoring.md)
  (no light is authored anywhere, so every shading path collapses to ambient).
- [`ui/UI-046-sandbox-geometry-export.md`](ui/UI-046-sandbox-geometry-export.md)
  (the geometry IO writers are unreachable from the app).
- [`ui/UI-047-file-chooser-for-import-and-scene-paths.md`](ui/UI-047-file-chooser-for-import-and-scene-paths.md)
  (file chooser and drop hints; keyboard editing is repaired by retired `BUG-139`).
- [`ui/UI-048-first-run-workspace-and-layout-persistence.md`](ui/UI-048-first-run-workspace-and-layout-persistence.md).
- [`ui/UI-049-editor-panel-sizing-and-readability.md`](ui/UI-049-editor-panel-sizing-and-readability.md).
- [`ui/UI-050-vector-field-property-visualization.md`](ui/UI-050-vector-field-property-visualization.md).
- [`ui/UI-051-domain-agnostic-appearance-properties-selection-windows.md`](ui/UI-051-domain-agnostic-appearance-properties-selection-windows.md).
- [`assets/ASSETIO-012-single-source-format-capability-table.md`](assets/ASSETIO-012-single-source-format-capability-table.md).

### Theme G — Active bugs

Reproducible correctness/regression fixes only. Origin:
[`bugs/index.md`](bugs/index.md).

Open members are tracked in [`bugs/index.md`](bugs/index.md). The 2026-06-11
severe-bug audit set (`BUG-029` through `BUG-035`) is retired; BUG-030's
non-bug headless loop coverage follow-up is also retired. New reproducible bugs
open under `bugs/` per the index.

Resolved bug history lives in [`bugs/index.md`](bugs/index.md) and the
retirement log.

### Theme H — Agentic workflow hardening (P1)

Keep the agent contract mirrors, task indexes, task metadata, and audit
cadences mechanically honest. Origin: agentic-workflow review (2026-06-09) of
`AGENTS.md`, `docs/agent/*`, the skill mirrors, and the `tasks/` tree, which
found live skill-mirror drift, duplicate task IDs in `tasks/done/`,
history-clogged session-start indexes, stale warning-mode contract text, and
unwatched audit cadences. Docs/tooling/CI-policy surfaces only — no engine
code. `PROC-001` (mirror sync gate), `PROC-002` (ID uniqueness), `PROC-005`
(contract truth-up), `PROC-007` (prompt tightening), `PROC-003`
(index state/history split), `PROC-004` (front-matter + session brief),
and `PROC-006` (audit cadence visibility) are retired — see
[`process/README.md`](process/README.md) and the retirement log.

`PROC-001..009` are all retired. `PROC-010` is also retired: it promoted the
research-engine design principles `P1` (research pragmatism / structs over
ceremony), `P3` (config lane first-class + agent-controllable), and `P5`
(recipe-driven frames + readable main loop) into always-on `AGENTS.md` §5
invariants with matching per-PR review rows. The 2026-07-14 right-sizing
review retired `PROC-026` (task archive sweep + micro template) and seeded
`PROC-027` (validator rent audit) and `DOCS-006` (curated agentic-development
narrative). Its optional "Theme I — Research
control surface" proposal was dropped (all members had retired); the `I` letter
instead names the research/method implementation theme created by the retired
`PROC-024` (see Theme I above and the retirement log).

`PROC-028` and its contract-applicability follow-up `PROC-030` are retired.
Their generated evidence, fixed-surface review, experiment custody, and
contract-discovery rules remain the foundation for new process work.

The planned verification redesign is now the open Theme H program, on top of
the retired repository-native work-graph slice. Its
authoritative architecture, quality/admission rules, migration sequence, and
dependency DAG live in
[`verification-evidence-architecture.md`](../../docs/architecture/verification-evidence-architecture.md),
with the twelve open `CI-012..020`, `BUILD-005..006`, and `PROC-031` members,
plus retired `PROC-032`, indexed under [`process/`](process/README.md). The
program keeps current gates
authoritative through shadow admission, chooses any C++23-module build backend
through a claim-grade bake-off, and forbids legacy policy deletion until the
final `CI-020` cutover gate.

The CI-latency track is also Theme H because fast, trustworthy agent feedback is
workflow infrastructure. `CI-003` is retired after capturing the 2026-07-09
hosted-run measurements and compile hotspots and adding machine-readable
telemetry, a claim-grade aggregate baseline, and stale-run cancellation.
`CI-004` is retired after routing specialized gates through label-derived test
build aggregates. `CI-007` is retired after retaining a module-safe,
`pr-fast`-only ccache store with hosted cold/warm and interface-invalidation
evidence. Retired `BUG-107` established configure-history-independent target
inventories, and retired `BUG-106` established capability-truthful test
ownership. Retired `CI-010` established complete CPU source-coverage parity;
retired `CI-005` now makes PR-fast a
fail-closed touched-scope gate; retired `CI-006` isolates sanitizer variants;
retired `CI-011` uses comparable measurements to split only genuinely slow
cases; and retired `CI-008` groups five audited pure producers, preserves local
individual discovery, and retains the fastest absolute grouped full-CPU plan
at four CTest workers. Retired `BUILD-004` supplies source-complete,
normalized compile-hotspot evidence; retired `RUNTIME-166` consumed it to slim
and partition the RenderExtraction primary interface. Retired
`BUG-114` repaired the Release SLO contract before retired `CI-009` collected
five unchanged-SHA hosted samples, separated quick feedback from fail-closed
candidate confidence, and retained `ubuntu-24.04` below the documented
queue/total reopen thresholds. No comparable larger runner was registered, so
future scaling remains gated by quantified cost, benefit, maintenance, and
rollback criteria. Retired `PROC-025` also verified and refreshed the
repo-native research-ideation skills in StructSplat and Prospect on their
designated branches without copying IntrinsicEngine's placement or altering
either production tree.

`DOCS-005` is retired; the feature-module playbook now has the minimal-feature
floor and config/command artifact.
`PROC-011` is retired; the contract now routes architecture questions to the
canonical architecture index, and task authoring prompts record backend/config
control-surface intent.

### Theme I — Research method implementation (P1, paused by REVIEW-004)

Implement the paper/method reference-backend track per the method workflow
([`/AGENTS.md`](../../AGENTS.md) §6): CPU reference backend first, correctness
tests, benchmark harness, then optional optimized/GPU parity. The theme also
covers the geometry method-readiness seams that unblock those methods. Members
carry their own `depends_on` edges; the picker takes the earliest unblocked
member.

Work selection from this theme is temporarily paused while the Theme J product
gate is open. Existing results and task state remain authoritative; do not
delete, rewrite, or expand them. `METHOD-039` completed its bounded negative
result on 2026-09-03 under explicit user direction; that exception does not
unpause `METHOD-040` or the rest of the theme. The standing inventory exception
is `METHOD-015`, which the registered-feature inventory and `REVIEW-004` name
as an explicit Framework24 product dependency. Resume other work only after
`REVIEW-004` retires with an accepted product verdict or the operator gives a
new explicit direction.

Rationale: `AGENTS.md` §1 names "geometry processing, and method-driven research
integration" as co-equal mission, but every open `METHOD-*` / research `GEOM-*`
task previously sat in the session brief's `Unthemed` section while engine
plumbing held themes with explicit priorities — the research mission was
structurally deprioritized by the picker. This theme makes the track a
first-class, P1 scheduling target alongside Theme B (rendering) and Theme C
(physics). Origin: `PROC-024` (retired 2026-07-11); the research-engine design
*invariants* P1/P3/P5 are owned separately by `PROC-010` (`AGENTS.md` §5).

Open members: `METHOD-003`, `METHOD-004`, `METHOD-005`, `METHOD-006` (blocked by
`GEOM-024`), `METHOD-007`, `METHOD-014`, and `METHOD-015`. The LOP family
`METHOD-016..020` is retired; the remaining property-domain integration is
tracked separately in the runtime/UI indexes. `METHOD-007A` and `METHOD-033A`
own deferred integration intake after their CPU references and REVIEW-004.
The parameterization family uses the retired `GEOM-063`/`GEOM-064` shared
surfaces: `METHOD-021` ARAP, `METHOD-022` SLIM (blocked by `METHOD-021`),
`METHOD-024` SCP (blocked by `GEOM-024`), `METHOD-025` Progressive SLIM
optimized CPU (blocked by `METHOD-022`), `METHOD-026` iterative-strategy GPU (blocked by
`METHOD-025`; the retired `RUNTIME-176` dependency is satisfied; iterative
strategies only), and its delivered engine-integration/view leaves
`RUNTIME-176`, `UI-036`, and `GRAPHICS-122` (all retired 2026-07-15;
`GRAPHICS-122` delivered the optional GPU-shaded UV target at `Operational`).
The remaining method-readiness seams are `GEOM-013`,
`GEOM-024`, `GEOM-059`, `GEOM-060`, `GEOM-061`, plus `GEOM-068` weighted
Dijkstra, `GEOM-069` A* (blocked by `GEOM-068`), `GEOM-070` rectangular
LSQR/LSCM, and `GEOM-072` Catmull-Clark creases (its `GEOM-071` prerequisite
is satisfied). Retired `GEOM-058` and
`GEOM-062` delivered the Gaussian-mixture and shared projection-kernel
prerequisites consumed by the LOP reference family. The post-stability Issue
445 research incubations are `GEOM-065`, `METHOD-027..031`, and `HARDEN-084`;
their retired `REVIEW-003` prerequisite is satisfied, while remaining
dependencies, killing tests, and concrete two-consumer evidence still govern
broader engine integration. The
2026-07-19 in-house octree-parity normal-orientation method (original
formulation, no upstream paper) opens unblocked as `METHOD-032`; its
publication track seeds screened-Poisson reconstruction `METHOD-033`, the
iPSR baseline `METHOD-034` (blocked by `METHOD-033`), the PGR
winding-number baseline `METHOD-035`, and the shared-protocol comparison
evidence `METHOD-036` (blocked by `METHOD-032`/`034`/`035` and an accepted
METHOD-032 implementation, not retirement alone). It separates
orientation-only and joint estimation/reconstruction information contracts.
The premature
RUNTIME-189 Sandbox view is retired; a new presentation task requires a
positive method verdict, frozen public diagnostics, and concrete demand.

## Cross-domain dependency anchors

These edges constrain task selection across categories. Respect them when
promoting backlog tasks to active so per-category DAGs do not diverge. Only
anchors with at least one open endpoint are listed; fully satisfied anchors
are preserved in the retirement log.

- **GRAPHICS-035..058 ⇐ Theme A.** Theme A's visible-geometry foundation is
  complete; rendering modernization leaves are now gated by their individual
  task dependencies and the rendering DAG.
- **Issue 445 incubations ⇐ REVIEW-003.** The deferred Theme B/Theme I tasks
  created from the 2026-07-15 literature scan now have their commit-scoped
  architecture stability/right-sizing prerequisite satisfied; their other
  front-matter dependencies still govern selection.
- **REVIEW-003 ⇐ 2026-08-06 remediation leaves (satisfied).** Retired `HARDEN-086`,
  `GRAPHICS-127`, and the earlier runtime consolidation set satisfy their
  historical gates. The rejected clean baseline's nine leaves —
  `GRAPHICS-129..134`, `RUNTIME-217`, `HARDEN-088`, and `RORG-031E` — are all
  retired. The fresh audit then retired cleanly at commit `51e7fadd`; rejected
  partial evidence was not reused.
- **GEOM-061 ⇐ BUG-109.** Grid reduction strategies build only on the repaired
  fail-closed quantization and deterministic cell ordering baseline.

## Promotion checklist

Before promoting a backlog task to active:

1. Confirm the task scope is small and reviewable.
2. Confirm acceptance criteria and verification commands exist.
3. Confirm required docs updates are listed.
4. Confirm the cross-domain dependency anchors above are satisfied or are
   explicitly recorded as out-of-scope in the task file.

## Recurring audits

Two additive, non-CI-enforced audit sweeps keep accumulated drift visible
(merged into [`docs/agent/review.md`](../../docs/agent/review.md) §"Audit
sweeps" by the 2026-08 pair-workflow redesign). Neither gates PR merges; both
run on demand — preferably overnight — and file follow-up backlog tasks or
`tasks/HINTS.md` entries for findings.

- **Output audit** (formerly the weekly `REVIEW-001` sweep): window-scoped
  review of agent-authored commits.
- **Drift audit** (formerly the 2–4-week `REVIEW-002` sweep): whole-tree
  state audit; reports land at `docs/reports/<YYYY-MM-DD>-drift-audit.md`.

## Related

- [`/AGENTS.md`](../../AGENTS.md) — authoritative repository contract.
- [`tasks/README.md`](../README.md) — task lifecycle and ID prefix conventions.
- [`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md) — retirement narratives and satisfied anchors.
- [`docs/agent/contract.md`](../../docs/agent/contract.md) — expanded contract.
- [`docs/agent/task-format.md`](../../docs/agent/task-format.md) — task file structure.
- [`docs/agent/review.md`](../../docs/agent/review.md) — the pre-merge sweep, deep reviews, and audit sweeps.
- [`tasks/backlog/rendering/README.md`](rendering/README.md) — rendering DAG (Themes A and B detail).
- [`tasks/backlog/runtime/README.md`](runtime/README.md) — runtime backlog index (Themes A and F detail).
