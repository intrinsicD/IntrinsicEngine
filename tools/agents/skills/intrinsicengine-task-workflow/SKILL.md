---
name: intrinsicengine-task-workflow
description: Author, promote, slice, and retire IntrinsicEngine task notes. Selects the interactive micro or unattended template and routes contract declarations, maturity closure, and evidence requirements.
---

# IntrinsicEngine Task Workflow

Task notes preserve scope and decisions between sessions. `AGENTS.md` and
`references/session-onboarding.md` in the core skill own the execution policy;
this skill routes task-specific details. Read canonical docs or their generated
references once, without loading both.

## Choose the lane

- Single-session interactive work needs no task file.
- Interactive work needing persistent context uses `tasks/templates/task-micro.md`,
  regardless of slice count. Required sections: Goal, checkbox Acceptance
  criteria, and exact Verification commands. Context, decisions, and slice plans
  are optional when useful. `workflow_profile: micro` exempts completion reports,
  not applicable engineering contracts, risk review, or research evidence.
- Unattended non-mechanical work uses the full template or its bug/review/method
  variant and `standard` or higher profile. One-slice mechanical work may use
  micro with a concrete evidence exemption. Publication custody is opt-in.

Read `references/task-format.md` when creating or materially changing a task.
It owns front-matter, contract declarations, method integration fields, and
retirement. Read `references/task-template.md` only for the full unattended
format. Micro notes omit custody ownership fields unless useful as context.

Before materially changing a task, inspect
`docs/architecture/contract-catalog.yaml`, declare applicable IDs, and read their
canonical sources. If none apply, record a concrete `contract_review` reason.
Task wording must not narrow a canonical contract; method tasks still need the
applicable engine-integration matrix and follow-up ownership.

## Questions and scope

Follow the core session workflow's question protocol: inspect the repository
first, ask only material unresolved questions, and preserve answers and existing
authorization. A routine task does not trigger an interview or require a second
scope confirmation. Use `grilling` when the user requests a design stress test.
Record useful decisions in the task note or commit message.

## Lifecycle and maturity

Keep planned work in `tasks/backlog/`, ongoing work in `tasks/active/`, and
completed notes in `tasks/done/`. Retire with closed acceptance criteria,
completion date, commit/PR reference, and an append to `tasks/done/RETIREMENT-LOG.md`;
regenerate `tasks/SESSION-BRIEF.md` after opening, retiring, or re-gating work.
Do not create a separate root-level planning tree.

When a stop-state is ambiguous, use `references/task-maturity.md` to distinguish
`Scaffolded`, `CPUContracted`, `Operational`, `ParityProven`, and `Retired`.
Scaffolded/backend-facing CPUContracted closures name the next maturity owner
or explicitly justify the intended endpoint. CPU/null tests alone do not prove
an operational backend. See `intrinsicengine-review` for the completion sweep.

## Architecture decisions

Use an ADR only when the decision is hard to reverse, surprising without
context, and the result of a real trade-off. Otherwise retain the rationale in
the task note. For an ADR, follow `docs/adr/` numbering and link it from the task.

## Verification and unattended evidence

For task changes run:

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
```

`references/workflow-evidence.md` is needed only for unattended execution or
opt-in custody. It owns task claims, work graphs, reports, and independent
review. When that evidence is touched, run `workflow_evidence.py validate` and
`experiment_custody.py validate` as documented there. Interactive task notes
alone do not activate this machinery.
