---
id: ARCH-013
theme: F
depends_on:
  - ARCH-012
completed: 2026-07-08
---
# ARCH-013 — Post-seam re-review of backlog tasks colliding with ADR-0024

## Status

- Retired 2026-07-08 as task-governance work.
- Maturity: `Retired` for the scheduled post-seam collision sweep; no code was
  allowed or needed.
- PR: pending.
- Commit: pending local change.
- Summary: every front-matter-gated row and audit-only row below now carries a
  dated `ARCH-013 re-review (2026-07-08)` note. `RUNTIME-129` was the only
  frontmatter re-gate and now depends on `RUNTIME-137`; `RUNTIME-137` was
  re-scoped as the `JobService` `GpuQueue`/readback substrate. The backlog sweep
  found no additional task prescribing rejected ADR-0024 mechanisms without a
  recorded decision; `CORE-006` was annotated as an adjacent discovered row
  because domain-free core task vocabulary supports the same seam direction.
- Verification (2026-07-08): `python3 tools/agents/check_task_policy.py --root
  . --strict`, `python3 tools/docs/check_doc_links.py --root .`,
  `python3 tools/docs/check_docs_sync.py --root .`, and
  `python3 tools/repo/check_test_layout.py --root . --strict` passed. Root
  hygiene stayed in warning mode for pre-existing root entries `ara/` and
  `imgui.ini`.

## Goal
- After the kernel-seam set `ARCH-007`..`ARCH-012` has fully retired, run
  the scheduled post-completion sweep over the collision inventory against
  [ADR-0024](../../docs/adr/0024-kernel-module-architecture.md):
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
- Completed decisions (2026-07-08):
  - `RUNTIME-150`: unchanged; gates held, remains a frame-loop TU/partition
    split with no new drains/pumps/queues.
  - `RUNTIME-151`: unchanged; lands last after `RUNTIME-146..150` and
    `ARCH-011`, removes interface leakage without `Engine&` pass-through.
  - `ARCH-006`: re-scoped onto the module-composition shape proven by
    `ClusteringModule`; Slice 0 must inventory generic editor shell versus
    Sandbox app panels.
  - `UI-034`: re-scoped onto the ADR-0024 EditorUiModule/panel-registry and
    single capture-snapshot direction.
  - `RUNTIME-137`: re-scoped as the `JobService` `GpuQueue`/async readback
    substrate and owner of remaining K-Means GPU participant migration.
  - `RUNTIME-138`: confirmed; remaining async selected-editor work must match
    `CommandBus`/`JobService` snapshot/result/bounded-apply semantics.
  - `RUNTIME-146`: unchanged free-standing config boot extraction.
  - `RUNTIME-147`: re-scoped; `Engine::GetAssetImportPipeline()` is a
    transitional composition accessor shapeable into a later service/module
    seam.
  - `RUNTIME-148`: unchanged scene-document extraction; future multi-world
    changes must use `WorldRegistry` handles.
  - `RUNTIME-149`: unchanged config-control extraction; app/method section
    ownership remains `CORE-009`.
  - `RUNTIME-129`: re-gated and re-scoped; Slice B GPU bake submission now
    depends on `RUNTIME-137` and must use the `JobService` `GpuQueue` target.
  - `CORE-005`: unchanged TaskGraph submit/completion substrate.
  - `CORE-006`: discovered adjacent row, unchanged; core task vocabulary stays
    domain-free while runtime owns job taxonomy.
  - `CORE-007`: unchanged scheduler hardening substrate.
  - `CORE-008`: unchanged task-graph plan-reuse substrate.
  - `CORE-009`: re-scoped onto app/module composition for registered config
    sections.
  - `GRAPHICS-105`: confirmed with a standing-event requirement for attribute
    source/readiness changes.
  - `PLATFORM-004`: unchanged; platform remains below runtime UI capture.

## Required changes
- [x] Confirm each front-matter-gated row: the gate held (no pre-seam
      landing) and the task's scope now builds on the seams; record the
      confirmation (or the corrective re-scope) as a dated note in that
      task's `Context`.
- [x] Re-review each audit-only row against ADR-0024 and record the
      decision (unchanged / re-scoped / re-gated / retired) with a dated
      note in that task's `Context` (or retire it with the normal
      retirement flow).
- [x] Apply resulting front-matter `depends_on` changes where re-gating is
      decided.
- [x] Sweep `tasks/backlog/` for collisions that emerged during the seam
      work (new tasks referencing Engine internals, dispatcher `trigger`,
      bespoke job queues, or `Engine&` pass-through) and add them to the
      inventory with decisions.
- [x] Update `tasks/backlog/README.md` (Theme F) and the affected category
      READMEs to reflect re-scopes/re-gates/retirements.

## Tests
- [x] `python3 tools/agents/check_task_policy.py --root . --strict` passes
      after all task-file edits.
- [x] `python3 tools/docs/check_doc_links.py --root .` reports no broken
      links.

## Docs
- [x] Regenerate `tasks/SESSION-BRIEF.md` after all re-gates.
- [x] Note the completed re-review (date + summary of decisions) in
      ADR-0024's Validation section.

## Acceptance criteria
- [x] Every inventory row (plus discovered additions) carries an explicit
      dated confirmation or decision in its task file; no row is left
      unexamined.
- [x] No open backlog task still prescribes a mechanism ADR-0024 rejected
      (immediate dispatch, bespoke job queues, `Engine&` pass-through,
      engine-resident domain machinery) without a recorded justification.
- [x] Session brief and backlog READMEs reflect the outcome.

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
