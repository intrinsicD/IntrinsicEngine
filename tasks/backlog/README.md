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
follow-up (`RUNTIME-139`), the immediate-mode debug-draw seam
(`RUNTIME-177`), and the 2026-07-03 render-graph
review leaves — pass contribution seam (`GRAPHICS-116`), compile caching
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

`RUNTIME-131` is retired to `tasks/done`: runtime exposes the agent/CLI
config-control facade for render-recipe preview/activation and the current
engine-config hot subset (`render.default_recipe_config_path`), with the
Sandbox Editor routed through the same facade. After `RUNTIME-149`, that facade
lives in `Extrinsic.Runtime.EngineConfigControl` behind
`Engine::GetConfigControl()`. This satisfies the config-control dependency for
`RUNTIME-134`, which is now retired at `CPUContracted`.
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
`ARCH-006`, remaining composition-root/abstractness seams (`CORE-006`,
`CORE-009`), scheduler/DAG capability and efficiency
(`CORE-005`, `CORE-007`, `CORE-008`). The retired steady-state frame
efficiency polish `RUNTIME-145` removed the recurring runtime frame-path waste
called out by the review. The retired correctness fix `BUG-055` (Theme G)
unblocks `CORE-005`.

**North star: [`docs/architecture/kernel-target-state.md`](../../docs/architecture/kernel-target-state.md)**
— the living target and convergence scorecard for the ADR-0024 kernel/module
migration, owned by the umbrella task `ARCH-014`. Any agent adding runtime
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
now lives behind `Extrinsic.Runtime.RenderExtractionService`, and GPU asset
cache/model-handoff residency ownership now lives behind
`Extrinsic.Runtime.AssetResidencyService`; persistent streaming executor,
derived-job registry, maintenance drains, and derived-job facade delegation now
live behind `Extrinsic.Runtime.AsyncWorkService`.
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
`RUNTIME-151` is retired as the Engine-interface cleanup. `RUNTIME-137` is now
retired as the JobService `GpuQueue`/async readback substrate, so `RUNTIME-129`
is unblocked for object-space normal bake GPU submission; the scheduler
substrate (`CORE-005`/`007`/`008`) may proceed independently when selected by
its owners.

Open members (kernel-seam priority set first):
- [`architecture/ARCH-014-kernel-convergence-tracking.md`](architecture/ARCH-014-kernel-convergence-tracking.md) (umbrella north-star; not a slice).
- [`geometry/RORG-031-geometry-method-readiness.md`](geometry/RORG-031-geometry-method-readiness.md).
- [`runtime/RUNTIME-138-nonblocking-selected-entity-editor-cache-pipeline.md`](runtime/RUNTIME-138-nonblocking-selected-entity-editor-cache-pipeline.md).
- [`architecture/CORE-005-nonblocking-taskgraph-submit-api.md`](architecture/CORE-005-nonblocking-taskgraph-submit-api.md).
- [`architecture/CORE-006-domain-free-core-task-vocabulary.md`](architecture/CORE-006-domain-free-core-task-vocabulary.md).
- [`architecture/CORE-007-scheduler-priority-wait-wake-hardening.md`](architecture/CORE-007-scheduler-priority-wait-wake-hardening.md).
- [`architecture/CORE-008-compiled-taskgraph-plan-reuse.md`](architecture/CORE-008-compiled-taskgraph-plan-reuse.md).
- [`architecture/CORE-009-app-owned-config-sections.md`](architecture/CORE-009-app-owned-config-sections.md).
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
evidence. `CI-005`, `CI-006`, and `CI-008` address real touched-scope PR
feedback, sanitizer duplication, and CTest/process oversubscription.
`CI-009` is deliberately last: heavy-gate lifecycle and larger-runner decisions
must use post-optimization median/p95 evidence rather than masking avoidable
cold-build work with hardware.

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
family on the `GEOM-063` shared surface — `METHOD-021` ARAP (blocked by
`GEOM-063`/`GEOM-064`), `METHOD-022` SLIM (blocked by
`GEOM-063`/`GEOM-064`/`METHOD-021`), `METHOD-023` BFF (blocked by `GEOM-063`),
`METHOD-024` SCP (blocked by `GEOM-063`/`GEOM-024`), `METHOD-025` optimized CPU
(blocked by `METHOD-021`/`022`), `METHOD-026` GPU (blocked by
`METHOD-025`/`RUNTIME-176`; iterative strategies only), and
its engine-integration leaves `RUNTIME-176` (blocked by `GEOM-063`), `UI-036`
(blocked by `RUNTIME-176`), and the optional `GRAPHICS-122` (blocked by
`UI-036`); and the method-readiness seams `GEOM-013`, `GEOM-014`, `GEOM-019`,
`GEOM-024`, `GEOM-058`, `GEOM-059`, `GEOM-060`, `GEOM-061`, `GEOM-062`,
`GEOM-063`, `GEOM-064`.

## Cross-domain dependency anchors

These edges constrain task selection across categories. Respect them when
promoting backlog tasks to active so per-category DAGs do not diverge. Only
anchors with at least one open endpoint are listed; fully satisfied anchors
are preserved in the retirement log.

- **GRAPHICS-035..058 ⇐ Theme A.** Theme A's visible-geometry foundation is
  complete; rendering modernization leaves are now gated by their individual
  task dependencies and the rendering DAG.
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
