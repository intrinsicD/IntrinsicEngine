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
bake graphics dilation follow-up (`GRAPHICS-115`) are retired — see
[`rendering/README.md`](rendering/README.md) and the retirement log. Current
open Theme B leaves include the object-space normal bake runtime scheduling
follow-up (`RUNTIME-129`), the profile-gated vertex attribute operational
follow-up (`RUNTIME-139`), and the 2026-07-03 render-graph review leaves —
pass contribution seam (`GRAPHICS-116`), compile caching
(`GRAPHICS-117`), placed transient aliasing (`GRAPHICS-118`), and parallel
command recording (`GRAPHICS-119`); compiler/executor polish (`GRAPHICS-120`)
is retired; see
[`rendering/README.md`](rendering/README.md) and
[`docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`](../../docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md). The runtime GPU readback job/write-back leg
(`RUNTIME-126`) is retired with the transfer facade/readback ring wired into the
runtime derived-job graph.

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
`REVIEW-003` as its first dependency, so none is selectable until the
architecture-stability/right-sizing audit retires cleanly. The leaves reopen
only bounded evidence or vertical-pilot slices from the archived Slang,
meshlet, cluster-LOD, and differentiable-rendering plans; they do not recreate
those broad umbrellas. Historical FetchContent instructions in the archived
plans are superseded by the current vcpkg-manifest-only contract.

The 2026-07-16 old-engine consolidation opened one immediately actionable
Theme B remediation: `GRAPHICS-127` completes the already-exported RHI GPU
timestamp profiler across native Vulkan pass recording, truthful Null
provenance, telemetry, and the existing Frame Graph panel. It is also a
dependency of `REVIEW-003` because the dead/misleading public seam must close
before architecture readiness can be accepted. The conditional
`GRAPHICS-041` hot-reload watcher child remains unopened until its Slang
compile/reflection prerequisites and a live consumer exist.

### Theme C — Physics readiness (P1)

Define physics layer ownership before any solver code lands; then implement
the ECS authoring, CPU reference, and physics-world/runtime bridge behind that
contract. The ownership decision (ADR-0019), phenomena roadmap, rigid-body
reference method, ECS authoring contract, world/runtime bridge, and
broadphase/narrowphase and constraint/island/sleep solver contracts are retired — see
[`physics/README.md`](physics/README.md) and the retirement log.

Theme C currently has **no open members**: the rigid-body foundation
(ADR-0019, METHOD-001, PHYSICS-001..003) and all three non-rigid reference
method packages from the phenomena roadmap (METHOD-009 particles/springs,
METHOD-010 XPBD cloth, METHOD-011 SPH fluid) are retired at
`CPUContracted`. Optimized/GPU physics backends and runtime integration
open as new tasks per the roadmap's engine-integration gates.

### Theme D — ECS hardening parity (P0, complete)

Promote ECS scene/hierarchy/component contracts out of `src/legacy` while
keeping `ecs -> core` and explicit geometry handles only. All members
(`HARDEN-060..068`, `HARDEN-081`) are retired — see
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

`RUNTIME-138` is the next runtime/editor responsiveness leaf: the Sandbox
selected-entity path should read cached state and submit commands/jobs, while
heavy selected-entity derivations run asynchronously and apply through bounded
main-thread results. Its first visibility-gated model-build slice is landed,
but the generation-keyed async analysis cache and bounded main-thread apply are
still open. `UI-031` is retired and consumes the visibility-gated model-build
slice for the domain-window information-architecture cleanup; the broader async
cache/job pipeline remains owned by `RUNTIME-138`.

The 2026-07-16 Sandbox model-workflow audit opened four Theme F leaves:
`ASSETIO-010` owns asynchronous primary/companion-file preview on top of the
route-level prerequisite contract delivered by `BUG-093`; `ASSETIO-011` owns
the app-linked semantic File / Import matrix after its preview/timing/PLY/
queued-geometry dependencies retire; `UI-037` owns runtime-authoritative
linear action readiness and disabled-reason tooltips after `BUG-096` and
`RUNTIME-138`; and `UI-038` owns explicit, undoable safety for destructive
Progressive Poisson mesh conversion. The four reproducible Theme G defects
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

`GEOM-052` is retired to `tasks/done`: `Geometry.KMeans` is now the canonical
backend-seam exemplar with `{CPU, GPU}` backend tokens and requested-vs-actual
fallback telemetry, while `Extrinsic.Runtime.KMeansBackend` owns the
`RHI::IDevice`-visible fallback hook. This satisfies the backend-seam dependency
for `PROC-011`.

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
`ARCH-014` remains the open kernel-convergence umbrella.
`RUNTIME-129` still owns the production Vulkan provider work.
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
`Runtime.Engine.cpp`; render-extraction cache/pool/stats/frame-index ownership
now lives in an Engine-private `RenderExtractionService`, and GPU asset
cache/model-handoff residency ownership now lives in an Engine-private
`AssetResidencyService`; the persistent streaming executor, derived-job
registry, world-retirement gate, maintenance drains, and shutdown now live in
the app-composed `Extrinsic.Runtime.AsyncWorkModule`, while the Engine derived
facades have been removed.
Accepted
[`ADR-0027`](../../docs/adr/0027-right-sized-runtime-composition.md)
corrects the literal destination: domain responsibilities must be explicitly
app-composed with stated global/world scope, but no wrapper, registry, schedule,
priority chain, extension slot, or experiment builder is created without a
production consumer. The bounded implementation graph is
`RUNTIME-179` AsyncWork, re-scoped `RUNTIME-172` SceneEditing,
`RUNTIME-180` Camera, `RUNTIME-181` ConfigControl, `RUNTIME-183`
AssetWorkflow, retired `RUNTIME-182` EditorUi, re-scoped `RUNTIME-168` Sandbox
composition, existing `RUNTIME-129` operational normal bake,
`RUNTIME-184` application-lifecycle removal, `RUNTIME-185` mechanism pruning,
and `RUNTIME-186` residual auxiliary-surface cleanup followed by the
`RUNTIME-187` exact Engine-surface ratchet. The bake and lifecycle leaves may
proceed independently, then both gate mechanism pruning. The detailed graph
and state scopes live in the [runtime backlog index](runtime/README.md).
Retired `RUNTIME-182` extracted the optional global ImGui/host owner while
preserving the existing paired frame bracket and one completed capture
snapshot. The next eligible behavior-owner slice is
[`RUNTIME-180`](../active/RUNTIME-180-extract-camera-module.md), now active.
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
historical prerequisite for `RUNTIME-129`; the task is now gated on
`RUNTIME-183` so the GPU submission lands in the accepted AssetWorkflow owner.
The non-blocking TaskGraph substrate (`CORE-005`) and scheduler hardening
(`CORE-007`) are retired, as is compiled-plan efficiency (`CORE-008`).

Open members (kernel-seam priority set first):
- [`../active/ARCH-014-kernel-convergence-tracking.md`](../active/ARCH-014-kernel-convergence-tracking.md) (active umbrella north-star; not a slice).
- [`../active/RUNTIME-180-extract-camera-module.md`](../active/RUNTIME-180-extract-camera-module.md) (active behavior-owner slice), then [`runtime/RUNTIME-172-privatize-scene-document-surface.md`](runtime/RUNTIME-172-privatize-scene-document-surface.md) through [`runtime/RUNTIME-187-finalize-domain-free-engine-surface.md`](runtime/RUNTIME-187-finalize-domain-free-engine-surface.md) (remaining ADR-0027 behavior-owner, app-lifecycle, mechanism-pruning, semantic auxiliary-surface, and final-ratchet graph; see the runtime index for exact dependencies).
- [`architecture/REVIEW-003-architecture-stability-right-sizing-readiness-audit.md`](architecture/REVIEW-003-architecture-stability-right-sizing-readiness-audit.md) (one-shot post-convergence admission gate; blocked until known architecture/right-sizing/tool-rent work retires).
- [`geometry/RORG-031-geometry-method-readiness.md`](geometry/RORG-031-geometry-method-readiness.md).
- [`runtime/RUNTIME-138-nonblocking-selected-entity-editor-cache-pipeline.md`](runtime/RUNTIME-138-nonblocking-selected-entity-editor-cache-pipeline.md).
- [`assets/ASSETIO-010-async-model-companion-preflight.md`](assets/ASSETIO-010-async-model-companion-preflight.md).
- [`assets/ASSETIO-011-semantic-sandbox-file-import-workflow-matrix.md`](assets/ASSETIO-011-semantic-sandbox-file-import-workflow-matrix.md) (blocked by `ASSETIO-010`; `BUG-098`, `BUG-099`, and `BUG-100` are satisfied dependencies).
- [`ui/UI-037-linear-domain-action-readiness-tooltips.md`](ui/UI-037-linear-domain-action-readiness-tooltips.md) (blocked by `BUG-096` and `RUNTIME-138`).
- [`ui/UI-038-progressive-poisson-destructive-conversion-safety.md`](ui/UI-038-progressive-poisson-destructive-conversion-safety.md).
- [`platform/PLATFORM-004-alternative-platform-backend-onboarding.md`](platform/PLATFORM-004-alternative-platform-backend-onboarding.md) (planning-only seed).

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

### Theme I — Research method implementation (P1)

Implement the paper/method reference-backend track per the method workflow
([`/AGENTS.md`](../../AGENTS.md) §6): CPU reference backend first, correctness
tests, benchmark harness, then optional optimized/GPU parity. The theme also
covers the geometry method-readiness seams that unblock those methods. Members
carry their own `depends_on` edges; the picker takes the earliest unblocked
member.

Rationale: `AGENTS.md` §1 names "geometry processing, and method-driven research
integration" as co-equal mission, but every open `METHOD-*` / research `GEOM-*`
task previously sat in the session brief's `Unthemed` section while engine
plumbing held themes with explicit priorities — the research mission was
structurally deprioritized by the picker. This theme makes the track a
first-class, P1 scheduling target alongside Theme B (rendering) and Theme C
(physics). Origin: `PROC-024` (retired 2026-07-11); the research-engine design
*invariants* P1/P3/P5 are owned separately by `PROC-010` (`AGENTS.md` §5).

Open members: `METHOD-003`, `METHOD-004`, `METHOD-005`, `METHOD-006` (blocked by
`GEOM-024`), `METHOD-007`, `METHOD-014`, `METHOD-015` (blocked by `GEOM-058`);
the LOP consolidation family `METHOD-016` (blocked by `GEOM-062`), `METHOD-017`
CLOP (blocked by `METHOD-016`/`GEOM-058`/`GEOM-062`), `METHOD-018`
EAR/anisotropic (blocked by `METHOD-016`/`GEOM-062`), `METHOD-019` optimized CPU
(blocked by `METHOD-016`/`017`/`018`), `METHOD-020` GPU (blocked by
`METHOD-019`), and its engine-integration leaves `RUNTIME-175` (blocked by
`METHOD-016`) and `UI-035` (blocked by `RUNTIME-175`); the parameterization
family on the retired `GEOM-063` shared surface — `METHOD-021` ARAP (blocked by
`GEOM-064`), `METHOD-022` SLIM (blocked by `GEOM-064`/`METHOD-021`),
`METHOD-023` BFF (retired),
`METHOD-024` SCP (blocked by `GEOM-024`), `METHOD-025` optimized CPU
(blocked by `METHOD-021`/`022`), `METHOD-026` GPU (blocked by
`METHOD-025`; the retired `RUNTIME-176` dependency is satisfied; iterative
strategies only), and its delivered engine-integration/view leaves
`RUNTIME-176`, `UI-036`, and `GRAPHICS-122` (all retired 2026-07-15;
`GRAPHICS-122` delivered the optional GPU-shaded UV target at `Operational`).
The method-readiness seams are `GEOM-013`,
`GEOM-024`, `GEOM-058`, `GEOM-059`, `GEOM-060`, `GEOM-061`, `GEOM-062`,
`GEOM-064`, plus the 2026-07-16 consolidation leaves `GEOM-068` weighted
Dijkstra, `GEOM-069` A* (blocked by `GEOM-068`), `GEOM-070` rectangular
LSQR/LSCM, `GEOM-071` shared sharp-feature classification, and `GEOM-072`
Catmull-Clark creases (blocked by `GEOM-071`). The post-stability Issue 445 research incubations are
`GEOM-065`, `METHOD-027..031`, and `HARDEN-084`; each is blocked first by
`REVIEW-003`, and their task files require killing tests or concrete
two-consumer evidence before opening broader engine integration.

## Cross-domain dependency anchors

These edges constrain task selection across categories. Respect them when
promoting backlog tasks to active so per-category DAGs do not diverge. Only
anchors with at least one open endpoint are listed; fully satisfied anchors
are preserved in the retirement log.

- **GRAPHICS-035..058 ⇐ Theme A.** Theme A's visible-geometry foundation is
  complete; rendering modernization leaves are now gated by their individual
  task dependencies and the rendering DAG.
- **Issue 445 incubations ⇐ REVIEW-003.** The deferred Theme B/Theme I tasks
  created from the 2026-07-15 literature scan remain blocked until the
  commit-scoped architecture stability and right-sizing audit retires cleanly.
- **REVIEW-003 ⇐ GRAPHICS-127 / HARDEN-086.** Architecture readiness remains
  blocked until the exported-but-unwired GPU profiler has truthful backend
  provenance and an operational native-Vulkan consumer path, and duplicated
  runtime hierarchy walks converge on the promoted checked ECS query contract.
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

Two additive, non-CI-enforced review cadences keep accumulated drift visible.
Neither gates PR merges; both rotate through the same reviewer pool and file
follow-up backlog tasks for findings.

- **Weekly agent-output audit** (`REVIEW-001`, done): window-scoped review of
  ~one week of agent-authored commits against
  [`docs/agent/agent-output-review-checklist.md`](../../docs/agent/agent-output-review-checklist.md).
- **Repo-state drift audit** (`REVIEW-002`, done): whole-tree state audit
  against [`docs/agent/drift-audit-checklist.md`](../../docs/agent/drift-audit-checklist.md),
  run on demand or every 2–4 weeks; reports land at
  `docs/reports/<YYYY-MM-DD>-drift-audit.md`.

## Related

- [`/AGENTS.md`](../../AGENTS.md) — authoritative repository contract.
- [`tasks/README.md`](../README.md) — task lifecycle and ID prefix conventions.
- [`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md) — retirement narratives and satisfied anchors.
- [`docs/agent/contract.md`](../../docs/agent/contract.md) — expanded contract.
- [`docs/agent/task-format.md`](../../docs/agent/task-format.md) — task file structure.
- [`docs/agent/review-checklist.md`](../../docs/agent/review-checklist.md) — pre-commit/PR review checklist.
- [`tasks/backlog/rendering/README.md`](rendering/README.md) — rendering DAG (Themes A and B detail).
- [`tasks/backlog/runtime/README.md`](runtime/README.md) — runtime backlog index (Themes A and F detail).
