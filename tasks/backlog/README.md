# Backlog Tasks

Current proposed or approved work that has not started. Task bodies own scope,
acceptance criteria and dependency gates; [SESSION-BRIEF](../SESSION-BRIEF.md)
shows their resolved state. Completed work belongs in the
[retirement log](../done/RETIREMENT-LOG.md), not duplicated here.

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

### Theme J — Framework24 product convergence (P0, active gate)

Framework24 feature/workflow convergence is the standing P0 until REVIEW-004 accepts the product. Use the [product inventory and golden workflows](../../docs/product/framework24-convergence.md) and REVIEW-004's remaining dependencies. Explicit operator direction may select other work.

### Theme A — Shortest path to sandbox visible geometry (P0, complete)

The visible-geometry foundation is complete. Select current regressions and product gaps through Theme J.

### Theme B — Rendering modernization (P1)

Rendering leaves retain their own evidence and adoption gates. GRAPHICS-105 still owns the remaining material/source-authority consolidation; GRAPHICS-135 owns measured render-prep overhead. GRAPHICS-137/136 require their recorded ADR trigger before a spike or rename.

### Theme C — Physics readiness (P1)

The [physics queue](physics/README.md) contains operator-requested Vulkan
candidates for the existing SPH, particle-spring and XPBD reference methods.
Their tasks own runtime integration and retain the
[physics ownership boundary](../../docs/architecture/physics.md).

### Theme D — ECS hardening parity (P0, bounded contract convergence)

No ECS task is currently in this backlog. Existing contracts remain binding; current product deficiencies belong to their named owners.

### Theme E — Geometry IO completion (P0, complete)

Geometry IO completion work is retired. New export/UI and model-companion readiness gaps remain in their current task families.

### Theme F — Architecture/runtime/UI foundation seeds

Use the runtime, UI, assets and architecture task notes for current integration gaps. Reuse typed family operations, runtime prepared frames and shared panel support; completed facade/queue removals are not pending work.

### Theme G — Active bugs

Use the [bug index](bugs/index.md). Preserve reproducible failures until fixed and verified; age or a partial fix is not closure.

### Theme H — Agentic workflow hardening (P1)

The process backlog owns the proposed verification graph, receipt/cache and cutover work. Current touched-scope selection and BUILD-007 measurements are existing foundations, not proof that those future systems exist.

### Theme I — Research method implementation (P1, paused by REVIEW-004)

Research remains paused behind REVIEW-004 except explicit operator direction and named product dependencies such as METHOD-015. Preserve frozen positive/negative evidence; dependency retirement alone is not scientific adoption evidence.

## Compilation and reuse follow-ups

These operator-requested tasks continue from the improved engine, without
reopening completed cleanup slices. BUILD-009, RUNTIME-266, RUNTIME-268,
GRAPHICS-144, GRAPHICS-145 and RUNTIME-267 are complete. UI-037 and GRAPHICS-105 are independently
ready. Task front-matter owns prerequisite state.

| Area | Task |
| --- | --- |
| Shared action readiness and disabled reasons | [UI-037](../active/UI-037-linear-domain-action-readiness-tooltips.md) |
| Material/visualization authority consolidation | [GRAPHICS-105](../active/GRAPHICS-105-unified-mesh-shading-and-attribute-source-authority.md) |
| Stale shaders after material-authority decisions | [LEGACY-043](rendering/LEGACY-043-retire-stale-multiset-shaders.md) |
| Build/cache backend comparison | [BUILD-006](process/BUILD-006-cxx23-module-build-backend-bakeoff.md) |

## Cross-domain dependency anchors

Use task front-matter and SESSION-BRIEF as the single dependency inventory.
References to done/archive tasks are satisfied dependencies and retain useful
provenance; do not treat them as blockers or delete them merely because they
retired. Conditional evidence/adoption gates in task bodies still apply.

## Promotion checklist

1. Confirm the gap against current code and reuse its canonical owner.
2. Check live dependencies and any explicit evidence/adoption trigger.
3. Keep concrete acceptance criteria, verification and relevant docs updates.
4. Follow the [task workflow](../../docs/agent/task-format.md).

## Recurring audits

Run output/drift audits on demand using the
[review procedures](../../docs/agent/review.md). The
[2026-09-15 backlog reconciliation](../../docs/reports/2026-09-15-backlog-reconciliation.md)
records the current task-by-task audit and corrections.

## Related

- [Agent contract](../../AGENTS.md)
- [Active work](../active/)
- [Retirement history](../done/RETIREMENT-LOG.md)
