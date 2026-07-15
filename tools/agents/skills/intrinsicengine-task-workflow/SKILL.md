---
name: intrinsicengine-task-workflow
description: How to author, promote, slice, and retire task files in the IntrinsicEngine `tasks/` directory tree (`tasks/backlog/`, `tasks/active/`, `tasks/done/`). Defines the required nine-section task template, the optional `## Maturity` section for ambiguous stop-states, and the full `Scaffolded → CPUContracted → Operational → ParityProven → Retired` taxonomy with the `Scaffolded` closure rule. Use this skill whenever creating a new task file, promoting a backlog task to active, retiring an active task to done, splitting work into slices, writing acceptance criteria, deciding what maturity level a slice closes at, or whenever the user mentions task IDs (e.g. `GRAPHICS-072`, `RUNTIME-095`), task slicing, "scaffold", "stub", "fail-closed", or "minimal" wording in a closing task.
---

# IntrinsicEngine Task Workflow

This skill governs task files under `tasks/` in IntrinsicEngine. Task files are
the unit of agent work: they capture the scope, slice plan, tests, docs, and
acceptance criteria for one reviewable change.

## The three task lifecycle directories

- `tasks/backlog/` — proposed or planned work; one file per task. May contain
  the slice plan even before activation.
- `tasks/active/` — work currently in-progress on a branch/owner. Promote here
  when you intend to land more than one slice.
- `tasks/done/` — retired tasks with a completion date and commit/PR reference.

Base every new task on a template in `tasks/templates/`. Do not create
long-lived root-level planning checklists once work belongs in one of these
directories.

## Required task file structure

All new task files use the nine-section template. See
`references/task-template.md` for the exact template, and
`references/task-format.md` for full field guidance and a worked example.

```markdown
# <TASK-ID> — <Task title>

## Goal
## Non-goals
## Context
## Required changes
## Tests
## Docs
## Acceptance criteria
## Verification
## Forbidden changes
```

Field rules (summary; see `references/task-format.md` for full detail):

- **Goal:** one specific outcome.
- **Non-goals:** explicit exclusions to prevent scope creep.
- **Context:** architectural owner/layer and relevant constraints.
- **Required changes:** concrete file-level work as markable checkbox todos (`- [ ]`).
- **Tests:** required verification commands and new/updated tests as checkbox todos.
- **Docs:** documentation updates as checkbox todos.
- **Acceptance criteria:** objective done-state checklist as checkbox todos.
- **Verification:** exact commands to run.
- **Forbidden changes:** things this task must not do.

Plain bullets for `Non-goals`, `Context`, and `Forbidden changes`. **Checkboxes
(`- [ ]` / `- [x]`) for actionable sections** so task status is visible at a
glance. Completed task files under `tasks/done/` must not contain unchecked
actionable todos — unresolved work goes into a follow-up task.

## Optional `## Maturity` section

For tasks where the stop-state is ambiguous — typically rendering, Vulkan, asset
ingest, hot reload, pass command bodies, runtime composition, and legacy
retirement — add a `## Maturity` section to pin the intended endpoint. The
validator does not enforce this field, but reviewers will ask for it whenever a
task could plausibly stop at multiple levels.

Suggested shape:

```markdown
## Maturity

- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Slice 1 closes `Scaffolded → CPUContracted`; `Operational` is owned by the
  follow-up <TASK-ID>.
```

## The maturity taxonomy

The taxonomy is shared vocabulary for distinguishing partial completion from
full capability. Levels are **cumulative** — a task at a higher level meets all
criteria below it.

| Level | Meaning |
| --- | --- |
| `Scaffolded` | Structure or API exists; behavior may be stubbed, fail-closed, or return defaults. The seam is reachable but does not yet prove the engine does the thing. |
| `CPUContracted` | CPU/null/backend-neutral contract tests exist for the seam. Default CPU gate (`ctest -LE 'gpu\|vulkan\|slow\|flaky-quarantine'`) verifies the contract. Backend-specific behavior may still be unverified. |
| `Operational` | A concrete backend or real runtime path exercises the seam under appropriate test labels (e.g. opt-in `gpu;vulkan` smoke, or wiring into `Engine::Run()` with the reference config). |
| `ParityProven` | The non-legacy path either matches legacy behavior with tests/evidence, or records an explicit "no-parity" decision. This is the gate that lets a legacy module retire. |
| `Retired` | The legacy path or shim is deleted; docs, generated inventories, and allowlists are updated. There is no compatibility re-export. |

**Critical reading rule:** CPU-only contract coverage is **insufficient** to
claim `Operational`. The corresponding backend-labeled or integration-labeled
run must be cited in the task's `Verification` as having actually run in the
session. Do not let "foundation exists" be read as "capability shipped".

For the full taxonomy with signals, the vocabulary mapping for older docs, and
how-to-use guidance, read `references/task-maturity.md`.

## The `Scaffolded` closure rule

A task that retires to `tasks/done/` at `Scaffolded` maturity must do **one** of:

1. **Name a follow-up task ID** that owns the `CPUContracted` (or higher) gate,
   linked from the done task's `Acceptance criteria` or `Status` block, **or**
2. **Record an explicit `Non-goals` line** stating that the scaffold is the
   intended endpoint and that no follow-up gate is owed.

The same rule applies one level up: a task that retires at `CPUContracted` when
the seam exists to be operational on a real backend (graphics, Vulkan, CUDA,
runtime composition) should name the `Operational` follow-up or explicitly
record the deferral. For open task files, the accepted deterministic forms are
`` `Operational` owned by `<TASK-ID>` `` or `` no `Operational` follow-up is owed ``.

The rule is enforced by review (see `intrinsicengine-review`), not by the
validator, because "scaffold", "stub", "fail-closed", and "minimal" are domain
language that legitimately appears in many tasks.

## Slice planning

For tasks too large to land in a single reviewable patch (typical of rendering
work), write the slice plan into the task file **before** implementing. The
GRAPHICS-072/073/074 series is the reference pattern:

- Each slice is independently reviewable.
- Earlier slices preserve the CPU/null correctness gate.
- Only the final slice exercises the operational backend.
- The plan names what each slice owns and what each slice **defers** to later
  slices, so reviewers can confirm scope.

A slice plan reads like:

```markdown
## Slice plan

- **Slice A (this slice).** Wire the seam at <layer>. Preserves CPU gate.
  Test: `<contract test name>`. Defers <X, Y> to Slices B/C.
- **Slice B.** ...
- **Slice C.** ...
- **Slice D.** Operational backend wiring + smoke. Cites `gpu;vulkan` run.
```

## Grilling alignment before authoring a task

This section is the IntrinsicEngine-specialized form of the `grilling` skill;
if the interview mechanics here ever diverge from `grilling/SKILL.md`, follow
`grilling` for the mechanics and this section for the engine-specific probes.

Before writing a task file for any non-trivial change, **interview the user
relentlessly** about the change until you reach a shared understanding. Walk
down each branch of the design tree, resolving dependencies one at a time.
For every question, provide your recommended answer.

- Ask **one question at a time** — wait for the user's answer before asking
  the next.
- If a question can be answered by exploring the codebase or reading an ADR
  under `docs/adr/`, explore instead of asking.
- When the user uses a term that conflicts with existing engine vocabulary
  (`AGENTS.md` layer names, maturity levels, backend identities, RHI
  terminology), call it out immediately and ask which is meant.
- When the user uses a fuzzy term ("the renderer", "the pipeline", "this
  pass"), propose the precise canonical name (e.g. `Extrinsic.Graphics.Renderer.PassRegistry`)
  before continuing.
- Stress-test domain relationships with **concrete scenarios** that probe
  edge cases — what happens on a Vulkan-incapable host, with a 0-element
  mesh, on hot reload, with the legacy path active.
- Cross-reference what the user says with the code. If they contradict, surface
  it: "you said the asset service drives this, but the call site is in
  `runtime` — which is right?".

The output of the grilling is the task file (sections per
`references/task-template.md`). Do not start implementing before the file is
written and the user has confirmed scope.

## When to record an ADR

The engine already keeps ADRs under `docs/adr/` with the `NNNN-<slug>.md`
naming. Add a new ADR only when **all three** are true:

1. **Hard to reverse** — the cost of changing your mind later is meaningful
   (layering decisions, backend selection rules, public method contracts,
   data-format decisions, runtime composition order).
2. **Surprising without context** — a future reader will wonder "why was it
   done this way?" and the answer is not obvious from the code.
3. **The result of a real trade-off** — there were genuine alternatives and
   you picked one for specific reasons.

If any of the three is missing, skip the ADR. Capture the decision in the
task file's `Context` or `Non-goals` instead. ADRs are expensive; over-using
them dilutes the signal of the ones that matter.

When you do write one, follow the numbering of the existing ADRs and link it
from the owning task's `Context` section.

## Task execution sequence

Every task execution should follow:

1. Inspect existing code and docs.
2. Identify owning subsystem and layer (see `intrinsicengine-core` for layering).
3. If the change is non-trivial, run a grilling alignment pass (see above).
4. Write or update task file from `tasks/templates/`.
5. Implement the smallest useful patch.
6. Add or update tests with correct labels.
7. Add or update docs; record an ADR only if the three-condition rule applies.
8. Run verification (focused targets first, then broaden).
9. Update generated inventories if module surfaces changed.
10. Self-review against the review checklist (see `intrinsicengine-review`).

## Validation tools

```bash
# Strict task-policy check (must pass for tasks/ changes)
python3 tools/agents/check_task_policy.py --root . --strict

# Full task validator
python3 tools/agents/validate_tasks.py --root tasks --strict
```

The validator enforces the nine required sections; the optional `## Maturity`
section is not validator-enforced.

## References

- `references/task-format.md` — full field guidance, worked example, optional
  `## Maturity` shape. Read this when authoring or materially editing any task.
- `references/task-maturity.md` — full taxonomy with signals per level,
  `Scaffolded` closure rule with both forms of follow-up, vocabulary mapping
  for older docs. Read this when deciding what maturity level a slice closes
  at, or when reviewing a `Scaffolded`/`CPUContracted` retirement.
- `references/task-template.md` — the bare template to copy when creating a
  new task file.
