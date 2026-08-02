# Task Format

Use this template for all new task files under `tasks/`.

Single-slice mechanical work (small fixes, doc/link sweeps, config toggles,
test-only additions) may use the reduced **micro template** instead — see
§"Micro tasks" below.

## Required structure

```md
---
id: <TASK-ID>
theme: <theme letter, or `none`>
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: <required explanation when contracts is empty>
---
# <TASK-ID> <Title>

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

## Field guidance

- **Goal:** one specific outcome.
- **Non-goals:** explicit exclusions to prevent scope creep.
- **Context:** architectural owner/layer and relevant constraints.
- **Required changes:** concrete file-level work as markable checkbox todos.
- **Tests:** required verification commands and new/updated tests as markable checkbox todos.
- **Docs:** documentation updates required by the task as markable checkbox todos.
- **Acceptance criteria:** objective done-state checklist as markable checkbox todos.
- **Verification:** exact commands to run.
- **Forbidden changes:** things this task must not do.

Use plain bullets for context, non-goals, and forbidden changes. Use markdown
checkboxes (`- [ ]` for open work, `- [x]` for completed work) in actionable
sections so task status is visible at a glance. Completed task files under
`tasks/done/` should not contain unchecked actionable todos; unresolved work
belongs in a follow-up task.

## Front-matter (open tasks)

Open tasks under `tasks/active/` and `tasks/backlog/` must start with a YAML
front-matter block; it is the machine-readable home of dependency edges and
feeds the generated `tasks/SESSION-BRIEF.md`:

- `id` (required) — must equal the title-line task ID.
- `theme` (required) — convergence-theme letter from
  `tasks/backlog/README.md`, or `none` for unthemed work.
- `depends_on` (required, may be `[]`) — task IDs this task is gated by;
  every entry must resolve to a task file under
  `tasks/active|backlog|done|archive`. A dependency is satisfied when the
  referenced task is in `tasks/done/` or `tasks/archive/`.
- `maturity_target` (optional) — intended stop-state per
  [`task-maturity.md`](task-maturity.md).
- `template: micro` (optional) — marks a micro task (see §"Micro tasks").
- `workflow_schema` (required for new/changed tasks) — currently `1`.
- `workflow_profile` — `micro`, `standard`, `high-risk`, `claim-grade`, or
  `protected`; profiles are cumulative.
- `evidence` — `required` except for `micro`, which uses `not_applicable`.
- `evidence_skip_reason` — required for the micro exemption.
- `owner`, `branch`, `worktree`, `claimed_at` — null while a backlog task is
  unclaimed and non-empty ISO-8601 ownership metadata in `tasks/active/`.
- `contract_schema` (required for new/changed tasks) — currently `1`.
- `contracts` (required, may be `[]`) — unique stable IDs from
  [`contract-catalog.yaml`](../architecture/contract-catalog.yaml). Determine
  applicability from both owning and consuming layers, the least-structured
  data domain, result publication/cardinality, and every config, agent,
  runtime, and UI control surface. Task wording cannot narrow a listed
  contract.
- `contract_review` — required and non-empty when `contracts: []`; records why
  the catalog was reviewed and no contract applies. It is optional when IDs are
  declared.

`tools/agents/validate_tasks.py --strict` enforces the schema for open tasks;
enrolled retired tasks retain the workflow fields. Exact hashes in
`tools/agents/workflow_legacy_tasks.json` grandfather only untouched open
tasks that predate `PROC-028`; a new, renamed, promoted, or edited task must
enroll. Historical done/archive tasks are not backfilled. See
[`workflow-evidence.md`](workflow-evidence.md) for profile triggers and
evidence custody. After opening, retiring, or re-gating a task, regenerate the session brief with
`python3 tools/agents/generate_session_brief.py`.

Contract enrollment is independently prospective. Exact hashes in
`tools/agents/contract_legacy_tasks.json` grandfather only byte-identical open
tasks that predate `PROC-030`; editing, renaming, or promoting one enrolls it.
`tools/agents/validate_tasks.py --strict` resolves declared IDs against the
catalog and validates each catalog source/proof path. Add a catalog entry only
for a reusable rule with canonical prose and an executable validator or test;
do not make a task, skill, generated file, or code comment its sole authority.

For method-backed work, declaring `method.engine-integration` also requires an
`## Engine integration` matrix. Record the least-structured input, every
compatible entity source, `RuntimeModule` binding, config/agent path, UI
domains, publication/cardinality policy, and end-to-end tests. A method-only
slice may defer a surface, but the matrix must name the owning follow-up task;
`N/A` is valid only when the canonical method contract makes engine integration
inapplicable.

The general, bug, and review templates default to `standard`; authors promote
the profile when a trigger applies. The method template defaults to
`claim-grade` because its lifecycle is designed to support scientific
correctness/parity evidence.

## Micro tasks

Seed from [`tasks/templates/task-micro.md`](../../tasks/templates/task-micro.md)
and set `template: micro` in the front-matter; `validate_tasks.py` then
requires only `## Goal`, `## Acceptance criteria` (with checkbox todos), and
`## Verification`.

Micro tasks are for **single-slice mechanical work only**: small fixes,
doc/link sweeps, config toggles, test-only additions. They are **not**
allowed for work that changes dependency boundaries, module ownership,
public module surfaces (`.cppm`), methods/benchmarks, or anything with an
ambiguous maturity stop-state — that work uses the full template (or the
method/bug/review variants). They set `workflow_profile: micro`,
`evidence: not_applicable`, and a concrete `evidence_skip_reason`; they do not
inherit high-risk, claim-grade, or protected custody. Retirement rules
otherwise remain unchanged.

## Optional `## Control surfaces` and `## Backends` fields

For feature work that introduces or changes user/agent control, add a
`## Control surfaces` section before `## Required changes` and name which
surfaces can drive the behavior:

```md
## Control surfaces
- Config: `EngineConfig.render.default_recipe_config_path`
- UI: Sandbox editor recipe panel
- Agent/CLI: resolved `EngineConfigControl::LoadAndApplyRenderRecipeConfigFile(...)` service
```

Use `N/A` only when the task cannot be externally controlled by design, for
example a purely internal mechanical migration.

For parallelizable engine algorithms or backend-facing features, add a short
`## Backends` section:

```md
## Backends
- Backend axis: CPU reference now; GPU deferred to `GRAPHICS-108`.
```

This field is non-enforcing, like `## Maturity`; it is an authoring prompt so
reviewers can see whether a CPU/GPU backend hook is present or intentionally
deferred to a named task.

## Retiring a task

When a task completes:

1. Mark all checkbox todos `- [x]` (unresolved work moves to a follow-up
   task) and add a completion note with the date (`YYYY-MM-DD`) and a
   commit/PR reference.
2. `git mv` the file to `tasks/done/`. Remove it from open-member lists,
   update its category README, append a short narrative (what landed,
   maturity, what remains owned elsewhere) to the top of
   [`tasks/done/RETIREMENT-LOG.md`](../../tasks/done/RETIREMENT-LOG.md), and
   regenerate `tasks/SESSION-BRIEF.md`. Do **not** leave a done-task link in
   `tasks/active/README.md` or an open backlog section.
3. For enrolled non-micro work, generate the final
   `tasks/evidence/<TASK-ID>/report.yaml` after the retirement surface is
   stable. `high-risk` and higher profiles also require the final handoff and
   accepted independent review bound to that exact content digest. Run:

   ```bash
   python3 tools/agents/workflow_evidence.py validate \
     --root . --require-complete <TASK-ID>
   ```

   If review requests a revision, change the source, regenerate the report,
   append a new handoff/review round, and validate again.

Category READMEs may keep retired entries only under a history-marked heading
(retired/history/closed/completed/resolved/verified/done). Open lists cite
retired tasks as plain code spans, not links. `check_task_state_links.py`
enforces this for task indexes; explicitly mixed dependency-DAG sections keep
their existing state-link-guard comment.

Retired files stay in `tasks/done/` short-term; they are periodically swept
to `tasks/archive/` (frozen read-only history — see
[`tasks/archive/README.md`](../../tasks/archive/README.md)). Archived IDs
remain authoritative: they resolve `depends_on` references, participate in
duplicate-ID detection, and can never be reallocated.

## ID allocation

Task IDs must be unique across `tasks/active/`, `tasks/backlog/`,
`tasks/done/`, and `tasks/archive/`; `tools/agents/validate_tasks.py`
enforces this in strict mode (a small set of pre-2026-06-09 collisions is
grandfathered in place). Before opening `<PREFIX>-<N>`, take the highest
existing number for that prefix across **all four** directories and add one:

```bash
grep -rhoE '^# <PREFIX>-[0-9]+' tasks/active tasks/backlog tasks/done tasks/archive | sort -V | tail -1
```

Letter-suffixed child slices (e.g. `GRAPHICS-033A`) extend their parent's
number and do not claim a new one.

Prefixes come from the canonical list in `tasks/README.md` §"Task ID
prefixes"; do not invent a new prefix without adding it there in the same
change (historical `tasks/archive/` entries contain retired prefix variants —
they are not precedent).

When **batch-seeding** several tasks (e.g. converting review findings into a
task series), allocate the whole contiguous range up front and run
`python3 tools/agents/validate_tasks.py --root tasks --strict` locally before committing —
CI enforces uniqueness, but a concurrent session may claim the same numbers
in flight (`GEOM-027` collided this way and had to be renumbered by
`PROC-012`). If two branches race, the first one merged keeps the numbers and
the later branch renumbers.

## Optional `## Maturity` field

For tasks where the stop-state is ambiguous — typically rendering, Vulkan,
asset ingest, hot reload, pass command bodies, runtime composition, and
legacy retirement — an optional `## Maturity` section makes the intended
endpoint explicit. The section is not required and the validator does not
enforce it, but reviewers should ask for it when a task could plausibly stop
at multiple levels.

Suggested shape:

```md
## Maturity

- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere
  else.
- Slice 1 closes `Scaffolded → CPUContracted`; `Operational` owned by `<TASK-ID>`.
- For CPU/null-only endpoints: no `Operational` follow-up is owed.
```

See [`task-maturity.md`](task-maturity.md) for the taxonomy and the
`Scaffolded` closure rule that applies even when the field is absent. Open
backend-facing task files with `CPUContracted` maturity must use one of the
accepted `Operational` follow-up statements above.

## Example

```md
# RORG-999 Example mechanical move

## Goal
Move subsystem docs into canonical location.

## Non-goals
- No semantic code changes.

## Context
Owned by docs/migration layer.

## Required changes
- [ ] `git mv docs/old/path.md docs/new/path.md`
- [ ] Update links in `docs/index.md`

## Tests
- [ ] `python3 tools/docs/check_doc_links.py --root .`

## Docs
- [ ] Update migration index with new location.

## Acceptance criteria
- [ ] All links resolve.
- [ ] No code files changed.

## Verification
- `python3 tools/docs/check_doc_links.py --root .`

## Forbidden changes
- No C++ behavior edits.
```
