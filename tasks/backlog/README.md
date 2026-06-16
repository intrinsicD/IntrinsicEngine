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
breaking the foundation. The umbrella roadmap (`GRAPHICS-035`) and all
`GRAPHICS-036..058` planning/implementation leaves (pipelined frames, async
compute, HZB occlusion, clustered lights, TAA, and the further modernization
seams) are retired — see [`rendering/README.md`](rendering/README.md) and the
retirement log. Theme B currently has **no open members**; new
implementation children should be opened under `rendering/` per the rendering
DAG when picked up.

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
its children are retired — see [`geometry/README.md`](geometry/README.md) and
the retirement log.

### Theme F — Architecture/runtime/UI foundation seeds

Keep cross-cutting backlog stubs honest with current state and reachable from
the convergence map. Retired members are indexed in the category READMEs and
the retirement log.

INFRA Option C is accepted through
[ADR-0020](../../docs/adr/0020-vcpkg-manifest-dependency-management.md): the
repository dependency path is vcpkg manifest mode with CI/local binary-cache
wiring. Active `INFRA-001` now tracks only final deprecation cleanup and
warm-cache CI timing evidence.

Open members:
- [`architecture/LEGACY-011-src-legacy-feature-reimplementation-map.md`](architecture/LEGACY-011-src-legacy-feature-reimplementation-map.md) —
  value-gated cross-domain map for remaining legacy feature candidates before
  mechanical `src/legacy/` subtree deletion.
- [`architecture/LEGACY-012-migrate-legacy-consumer-tests.md`](architecture/LEGACY-012-migrate-legacy-consumer-tests.md) —
  follow-up for tests and non-legacy consumers that still import bare legacy
  module names after promoted equivalents exist.
- [`geometry/RORG-031-geometry-method-readiness.md`](geometry/RORG-031-geometry-method-readiness.md).
- [`ui/RORG-031-ui-integration.md`](ui/RORG-031-ui-integration.md).
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

Theme H currently has **no open members**: `PROC-001..008` are all
retired. New workflow-hardening findings open under `process/`.

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
