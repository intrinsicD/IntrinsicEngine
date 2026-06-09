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

Still-open Theme A stragglers:
- [`runtime/RORG-031-runtime-composition.md`](runtime/RORG-031-runtime-composition.md)
  (also Theme F) — runtime composition backlog seed.

### Theme B — Rendering modernization (P1, unblocked)

Promote the post-reorganization renderer toward 2026-era features without
breaking the foundation. Theme A's scoped working-sandbox acceptance is
retired, so Theme B leaves may be selected according to their own rendering
DAG and task-level dependencies. The umbrella roadmap (`GRAPHICS-035`) is
retired.

Members:
- `rendering/GRAPHICS-036..058` planning-only leaves: pipelined frames,
  async compute and multi-queue scheduling, HZB occlusion culling, clustered
  light binning, TAA and reconstructor seam, Slang shader pipeline, PBR
  completeness and IBL, visibility buffer, meshlets, ray tracing RHI extension,
  hybrid GI (ReSTIR/DDGI), virtual shadow maps, Gaussian splatting rasterizer,
  neural radiance cache, neural texture compression, differentiable rendering
  mode, deltaful GPU-resident scene, mesh shaders, work graphs, streaming
  virtual textures, virtualized meshes with cluster LOD, GPU decompression,
  frame generation. See [`rendering/README.md`](rendering/README.md) for the
  full list, DAG, and per-leaf status.

### Theme C — Physics readiness (P1)

Define physics layer ownership before any solver code lands; then implement
the ECS authoring, CPU reference, and physics-world/runtime bridge behind that
contract. The ownership decision (ADR-0019), phenomena roadmap, rigid-body
reference method, ECS authoring contract, world/runtime bridge, and
broadphase/narrowphase contract are retired — see
[`physics/README.md`](physics/README.md) and the retirement log.

Open members:
- [`physics/PHYSICS-003-constraints-islands-and-solver-diagnostics.md`](physics/PHYSICS-003-constraints-islands-and-solver-diagnostics.md) —
  unblocked; next physics leaf.
- [`methods/METHOD-009`](methods/METHOD-009-particle-spring-reference-backend.md),
  [`methods/METHOD-010`](methods/METHOD-010-xpbd-cloth-shell-reference-backend.md),
  [`methods/METHOD-011`](methods/METHOD-011-sph-fluid-reference-backend.md) —
  CPU-reference-first non-rigid method packages opened by the phenomena
  roadmap.

### Theme D — ECS hardening parity (P0, complete)

Promote ECS scene/hierarchy/component contracts out of `src/legacy` while
keeping `ecs -> core` and explicit geometry handles only. All members
(`HARDEN-060..068`, `HARDEN-081`) are retired — see
[`ecs/README.md`](ecs/README.md) and the retirement log.

### Theme E — Geometry IO completion (P0, complete)

Finish geometry-owned IO parity so legacy graphics importers/exporters can
retire and asset ingest can route through promoted decoders. `GEOIO-002` and
its children are retired — see [`geometry/README.md`](geometry/README.md) and
the retirement log.

### Theme F — Architecture/runtime/UI foundation seeds

Keep cross-cutting backlog stubs honest with current state and reachable from
the convergence map. Retired members are indexed in the category READMEs and
the retirement log.

Open members:
- [`architecture/RORG-031A-architecture-foundation.md`](architecture/RORG-031A-architecture-foundation.md).
- [`architecture/LEGACY-011-src-legacy-feature-reimplementation-map.md`](architecture/LEGACY-011-src-legacy-feature-reimplementation-map.md) —
  value-gated cross-domain map for remaining legacy feature candidates before
  mechanical `src/legacy/` subtree deletion.
- [`architecture/LEGACY-012-migrate-legacy-consumer-tests.md`](architecture/LEGACY-012-migrate-legacy-consumer-tests.md) —
  follow-up for tests and non-legacy consumers that still import bare legacy
  module names after promoted equivalents exist.
- [`runtime/RORG-031-runtime-composition.md`](runtime/RORG-031-runtime-composition.md) (also Theme A).
- [`runtime/RUNTIME-101-asset-ingest-state-machine.md`](runtime/RUNTIME-101-asset-ingest-state-machine.md),
  [`runtime/RUNTIME-103-geometry-algorithm-execution-queue.md`](runtime/RUNTIME-103-geometry-algorithm-execution-queue.md),
  [`runtime/RUNTIME-104-derived-overlay-producer-lifecycle.md`](runtime/RUNTIME-104-derived-overlay-producer-lifecycle.md).
- [`rendering/GRAPHICS-084-visualization-property-buffer-residency.md`](rendering/GRAPHICS-084-visualization-property-buffer-residency.md),
  [`rendering/GRAPHICS-085-overlay-packet-backend-parity.md`](rendering/GRAPHICS-085-overlay-packet-backend-parity.md),
  [`rendering/GRAPHICS-086-rhi-retirement-parity-and-cuda-decision.md`](rendering/GRAPHICS-086-rhi-retirement-parity-and-cuda-decision.md).
- [`geometry/RORG-031-geometry-method-readiness.md`](geometry/RORG-031-geometry-method-readiness.md).
- [`ui/RORG-031-ui-integration.md`](ui/RORG-031-ui-integration.md).
- [`platform/PLATFORM-004-alternative-platform-backend-onboarding.md`](platform/PLATFORM-004-alternative-platform-backend-onboarding.md) (planning-only seed).

### Theme G — Active bugs

Reproducible correctness/regression fixes only. Origin:
[`bugs/index.md`](bugs/index.md).

Members:
- No currently active bug records. Resolved bug history lives in
  [`bugs/index.md`](bugs/index.md) and the retirement log.

### Theme H — Agentic workflow hardening (P1)

Keep the agent contract mirrors, task indexes, task metadata, and audit
cadences mechanically honest. Origin: agentic-workflow review (2026-06-09) of
`AGENTS.md`, `docs/agent/*`, the skill mirrors, and the `tasks/` tree, which
found live skill-mirror drift, duplicate task IDs in `tasks/done/`,
history-clogged session-start indexes, stale warning-mode contract text, and
unwatched audit cadences. Docs/tooling/CI-policy surfaces only — no engine
code. `PROC-001` (mirror sync gate), `PROC-002` (ID uniqueness), `PROC-005`
(contract truth-up), and `PROC-007` (prompt tightening) are retired — see
[`process/README.md`](process/README.md) and the retirement log.

Open members (in dependency order):
- [`process/PROC-003`](process/PROC-003-split-task-index-state-from-retirement-history.md) —
  split task index state from retirement history (after PROC-002).
- [`process/PROC-004`](process/PROC-004-task-front-matter-and-generated-session-brief.md) —
  structured task front-matter and generated session brief (after PROC-003).
- [`process/PROC-006`](process/PROC-006-audit-cadence-lapse-visibility.md) —
  audit cadence lapse visibility (brief surface after PROC-004 Slice B).

## Cross-domain dependency anchors

These edges constrain task selection across categories. Respect them when
promoting backlog tasks to active so per-category DAGs do not diverge. Only
anchors with at least one open endpoint are listed; fully satisfied anchors
are preserved in the retirement log.

- **PHYSICS-003 ⇐ PHYSICS-001, PHYSICS-002.** Constraint/island/sleep solver
  diagnostics depend on world lifecycle and collision contacts. Both upstream
  tasks are retired, so `PHYSICS-003` is unblocked.
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
