---
id: ARCH-013
theme: F
depends_on:
  - ARCH-012
---
# ARCH-013 — Post-seam re-review of backlog tasks colliding with ADR-0024

## Goal
- After the kernel-seam set `ARCH-007`..`ARCH-012` has fully retired,
  re-review every open backlog task that overlaps the seams' surfaces or
  assumptions against
  [ADR-0024](../../../docs/adr/0024-kernel-module-architecture.md), and
  record a per-task decision — unchanged / re-scoped / re-gated / retired —
  in each task file, so no pre-seam task lands with a design the seams have
  since superseded.

## Non-goals
- No implementation work: this task only re-validates and re-writes task
  files; any material re-scope becomes edits to that task (or a follow-up
  task), not code changes here.
- No re-litigation of ADR-0024 decisions — the ADR is the yardstick; if a
  reviewed task exposes a genuine gap in the ADR, file a targeted ADR
  amendment task instead of stretching this one.
- No blanket re-gating: tasks that survive review unchanged keep their
  current gates.

## Context
- Owner/layer: process/backlog governance (Theme F).
- This task is deliberately `depends_on: ARCH-012` so the session brief
  keeps it blocked until the whole seam set (`ARCH-007`..`ARCH-011` via the
  proving extraction) is done, then surfaces it as unblocked — it is the
  scheduled "check the collisions again afterwards" step.
- Hard collisions are already machine-gated in front matter and excluded
  here except for confirmation: `RUNTIME-150` depends on
  `ARCH-007`/`ARCH-008` (RunFrame surface), `RUNTIME-151` depends on
  `ARCH-011` (Engine interface surface).
- Collision inventory to review (rationale per row; drop rows already
  retired by then, add new collisions discovered during the seam work):
  - `RUNTIME-146`..`RUNTIME-149` — the `Engine::GetX()` engine-owned-facade
    accessor pattern predates the RuntimeModule/service direction (ADR-0024
    D9/D12); decide per subsystem whether the extracted facade should become
    a module/Resolve-phase service instead. `RUNTIME-147` additionally owns
    the parked asset-boundary question (ADR-0024 open question 3).
  - `RUNTIME-150`/`RUNTIME-151` — confirm the front-matter gates held and
    the landed slices matched the seam wiring.
  - `ARCH-006` — Sandbox editor content out of runtime: re-scope onto the
    EditorUiModule extraction direction (ADR-0024 D11, migration step ⑦).
  - `UI-034` — window-contribution seam and capture contract: the capture
    contract became a kernel input-capture filter chain and the
    registration seam becomes the EditorUiModule panel registry (D11);
    its framework24-reference context needs rebasing onto those primitives.
  - `RUNTIME-137` — async GPU readback helper: re-check as the substrate
    for the JobService `GpuQueue` target (D8) rather than a free-standing
    helper.
  - `RUNTIME-138` — non-blocking selected-entity editor cache: the
    "submit commands/jobs, bounded main-thread apply" language must adopt
    CommandBus enqueue/drain and JobService snapshot-commit semantics (D5,
    D8) instead of bespoke lanes.
  - `RUNTIME-129` — GPU normal-bake scheduling after import: background
    scheduling should ride JobService; the "bake finished → attribute
    refresh" path is a standing event reaction (D6).
  - `CORE-005`, `CORE-007`, `CORE-008` — verify the scheduler/task-graph
    capabilities still match what JobService and the FrameGraph two-tier
    split (D8) actually consume (e.g. whether `CORE-005` submit/token
    underpins JobService or is superseded by it).
  - `CORE-009` — app-owned config sections: re-check against
    application-as-parts-list (D12) for where sections should live.
  - `GRAPHICS-105` — attribute-source authority: light check that it
    defines/consumes the attribute-changed event the D6 standing reaction
    relies on.
  - `PLATFORM-004` — platform-backend onboarding planning seed: light check
    against the kernel input-capture chain (D11).

## Required changes
- [ ] Re-review each row of the collision inventory against ADR-0024 and
      record the decision (unchanged / re-scoped / re-gated / retired) with
      a dated note in that task's `Context` (or retire it with the normal
      retirement flow).
- [ ] Apply resulting front-matter `depends_on` changes where re-gating is
      decided.
- [ ] Sweep `tasks/backlog/` for collisions that emerged during the seam
      work (new tasks referencing Engine internals, dispatcher `trigger`,
      bespoke job queues, or `Engine&` pass-through) and add them to the
      inventory with decisions.
- [ ] Update `tasks/backlog/README.md` (Theme F) and the affected category
      READMEs to reflect re-scopes/re-gates/retirements.

## Tests
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict` passes
      after all task-file edits.
- [ ] `python3 tools/docs/check_doc_links.py --root .` reports no broken
      links.

## Docs
- [ ] Regenerate `tasks/SESSION-BRIEF.md` after all re-gates.
- [ ] Note the completed re-review (date + summary of decisions) in
      ADR-0024's Validation section.

## Acceptance criteria
- [ ] Every inventory row (plus discovered additions) carries an explicit
      dated decision in its task file; none is left implicitly pre-seam.
- [ ] No open backlog task still prescribes a mechanism ADR-0024 rejected
      (immediate dispatch, bespoke job queues, `Engine&` pass-through,
      engine-resident domain machinery) without a recorded justification.
- [ ] Session brief and backlog READMEs reflect the outcome.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py
```

## Forbidden changes
- Code changes of any kind.
- Silently editing a reviewed task's scope without the dated decision note.
- Retiring tasks outside the normal retirement flow (done-file + log +
  brief regeneration).
