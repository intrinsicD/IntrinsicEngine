---
id: ARCH-013
theme: F
depends_on:
  - ARCH-012
---
# ARCH-013 — Post-seam re-review of backlog tasks colliding with ADR-0024

## Goal
- After the kernel-seam set `ARCH-007`..`ARCH-012` has fully retired, run
  the scheduled post-completion sweep over the collision inventory against
  [ADR-0024](../../../docs/adr/0024-kernel-module-architecture.md):
  confirm that the front-matter-gated tasks actually built on the seams
  they were gated on, and for the audit-only rows (tasks allowed to land
  pre-seam by design) record a per-task decision — unchanged / re-scoped /
  re-gated / retired — in each task file. Enforcement against landing
  superseded designs is NOT this task's job: it lives in the `depends_on`
  gates already stamped into the affected tasks' front matter (inventory
  below); this task is the audit that closes the loop.

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
- **Front-matter-gated rows (enforcement lives in `depends_on`; this task
  confirms the gates held and the landed slices built on the seams):**
  - `RUNTIME-150` — gated on `ARCH-007`/`ARCH-008` (shared `RunFrame()`
    surface).
  - `RUNTIME-151` — gated additionally on `ARCH-011` (shared Engine
    interface surface).
  - `ARCH-006` — gated on `ARCH-012`: Sandbox editor content re-scopes onto
    the EditorUiModule extraction direction (ADR-0024 D11, migration
    step ⑦).
  - `UI-034` — gated on `ARCH-012`: the capture contract became a kernel
    input-capture filter chain and the window-registration seam becomes the
    EditorUiModule panel registry (D11); its framework24-reference context
    needs rebasing onto those primitives before implementation.
  - `RUNTIME-137` — gated on `ARCH-009`: the readback helper is the
    substrate for the JobService `GpuQueue` target (D8), not a
    free-standing helper.
  - `RUNTIME-138` — gated on `ARCH-007`/`ARCH-009`: "submit commands/jobs,
    bounded main-thread apply" must adopt CommandBus enqueue/drain and
    JobService snapshot-commit semantics (D5, D8) instead of bespoke lanes.
- **Audit-only rows (allowed to land pre-seam by design — mechanical,
  substrate, or incidental overlap; record a dated decision per row; drop
  rows already retired by then, add collisions discovered during the seam
  work):**
  - `RUNTIME-146`..`RUNTIME-149` — mechanical decomposition moves, useful
    either way; decide per subsystem whether the landed `Engine::GetX()`
    facade becomes a module/Resolve-phase service (D9/D12). `RUNTIME-147`
    additionally owns the parked asset-boundary question (ADR-0024 open
    question 3).
  - `RUNTIME-129` — schedules onto the existing bake queue (Theme B
    progress, acceptable pre-seam); post-seam, background scheduling should
    migrate to JobService and the "bake finished → attribute refresh" path
    to a standing event reaction (D6).
  - `CORE-005`, `CORE-007`, `CORE-008` — scheduler substrate beneath the
    seams; verify the capabilities match what JobService and the FrameGraph
    two-tier split (D8) actually consume (e.g. whether `CORE-005`
    submit/token underpins JobService or is superseded by it).
  - `CORE-009` — app-owned config sections: re-check against
    application-as-parts-list (D12) for where sections should live.
  - `GRAPHICS-105` — attribute-source authority: light check that it
    defines/consumes the attribute-changed event the D6 standing reaction
    relies on.
  - `PLATFORM-004` — platform-backend onboarding planning seed: light check
    against the kernel input-capture chain (D11).

## Required changes
- [ ] Confirm each front-matter-gated row: the gate held (no pre-seam
      landing) and the task's scope now builds on the seams; record the
      confirmation (or the corrective re-scope) as a dated note in that
      task's `Context`.
- [ ] Re-review each audit-only row against ADR-0024 and record the
      decision (unchanged / re-scoped / re-gated / retired) with a dated
      note in that task's `Context` (or retire it with the normal
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
      dated confirmation or decision in its task file; no row is left
      unexamined.
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
