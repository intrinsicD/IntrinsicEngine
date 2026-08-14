# Task Format

Use this template for new task files on the unattended overnight lane.
Interactive work (pair/delegate/advisor sessions) and single-slice mechanical
work use the reduced **micro template** instead — see §"Micro tasks" below.

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

The Docs checklist names the canonical current-state documentation affected by
the task. Do not require a source-tree README update merely to record that a
feature or slice landed. Update a README only when its directory role,
ownership, navigation, configuration, or verification entry points change;
keep chronology in the task, ADR, migration record, report, or retirement log.
See [`source-documentation-policy.md`](../../../../../docs/agent/source-documentation-policy.md).

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
  [`task-maturity.md`](../../../../../docs/agent/task-maturity.md).
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
  [`contract-catalog.yaml`](../../../../../docs/architecture/contract-catalog.yaml). Determine
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
[`workflow-evidence.md`](../../../../../docs/agent/workflow-evidence.md) for profile triggers and
evidence custody. After opening, retiring, or re-gating a task, regenerate the session brief with
`python3 tools/agents/generate_session_brief.py`.

Contract enrollment is independently prospective across
`tasks/active|backlog|done|archive`; creating a task directly in a retired
lifecycle is not an escape hatch. `tools/agents/contract_legacy_tasks.json`
pins the exact pre-policy Git revision. Its `tasks` mapping grandfathers the
remaining byte-identical open snapshots, while unchanged done/archive history
is checked directly against that revision. Editing, renaming, promoting, or
moving one enrolls it. When a mapped open snapshot is enrolled or removed,
move its original path/hash to `consumed`; the validator rejects both stale
live entries and replay of consumed bytes. Historical task contents are not
backfilled.

When a branch that genuinely retired work before the policy effective point is
merged after that point, `parallel_retired` may bind those byte-identical
`done/` or `archive/` records to one exact 40-hex revision from the parallel
history and their SHA-256 digests, with `parallel_policy_task` naming the
high-risk task that reviewed the exception. This is a path-scoped historical
baseline, not a second effective date: it cannot cover active/backlog work,
every entry is verified against Git, and any later byte change still enrolls
prospectively. Preserve sealed task/experiment bytes; do not rewrite custody
hashes to make a parallel merge pass.

`tools/agents/validate_tasks.py --strict` applies this gate before the ordinary
done/archive format exemptions, resolves declared IDs against the catalog, and
validates each catalog source/proof path. Add a catalog entry only for a
reusable rule with canonical prose and an executable validator or test; do not
make a task, skill, generated file, or code comment its sole authority.

For method-backed work, declaring `method.engine-integration` also requires an
`## Engine integration` matrix. Record the least-structured input, every
compatible entity source, `RuntimeModule` binding, config/agent path, UI
domains, publication/cardinality policy, and end-to-end tests. A method-only
slice may defer a surface, but the matrix must name the owning follow-up task;
`N/A` is valid only when the canonical method contract makes engine integration
inapplicable.

For geometry methods, “least structured” is property/topology based, not
container based. A point-set input names a compatible typed property/span on
any element domain (including face, edge, or halfedge properties); it must not
be narrowed to `Vertices`, point-cloud provenance, or `VertexProperty`. A graph
input names the adjacency/property sources it needs and includes meshes that
satisfy them. The matrix must separately state same-domain output publication
and any explicit topology/cardinality-changing owner. Paired UI work must use
the same runtime preflight and expose compatible properties under every
appropriate provenance menu.

The general, bug, and review templates default to `standard`; authors promote
the profile when a trigger applies. The method template defaults to
`claim-grade` because its lifecycle is designed to support scientific
correctness/parity evidence.

## Live work graph for claimed tasks

This section applies to the unattended overnight lane only; interactive
sessions ride the micro lane and never start the graph.

After acquiring a claim for an enrolled non-micro task, start or resume the
default repository work graph:

```bash
python3 tools/agents/agent_work_graph.py start \
  --task-id <TASK-ID> \
  --owner <claim-owner> \
  --recipe tools/agents/work_graphs/review-diamond.v1.json
```

When an unfinished claim is released, recovered, or reacquired, use the
tool's explicit `resume` command even when the replacement keeps the same
owner label/worktree. It rebinds the exact claim generation, preserves
completed nodes and attempt history, and invalidates abandoned running work
and its descendants.

Use `show` to expose the current/ready nodes, `note` to attach later ideas or
constraints to the step where they must be honored, and `reopen` to invalidate
the write node's descendants after blocking review findings or any source
change past the writer-frozen review digest. `reopen` spends the current
slice's bounded attempt budget; it does not start another declared slice.

A multi-slice task that may continue under one claim/run includes a non-empty
`## Slice plan`. After a complete reviewed cycle, use `advance-slice --from-node
plan` to record the prior disposition, bind the next slice to the exact clean
`HEAD`, and reset only that plan/write/review subgraph's attempt counters. A
clean writer-complete checkpoint whose downstream review nodes never started
requires the explicit `--accept-pre-review-checkpoint` acknowledgement; stale
writer/review bindings additionally require `--accept-stale-source`. The
command rejects inactive tasks, dirty state, started/failed review cycles,
claim/profile/recipe drift, and a reset root that omits an active writer or the
final binder. See [`workflow-evidence.md`](../../../../../docs/agent/workflow-evidence.md) for the exact
transition contract.

Micro tasks are exempt. The live graph never grants a claim, changes task
dependencies, lowers the workflow profile, or satisfies completion evidence;
those authorities remain in the task front-matter, claim ledger, and
profile-specific evidence described in
[`workflow-evidence.md`](../../../../../docs/agent/workflow-evidence.md).

## Micro tasks

Seed from [`tasks/templates/task-micro.md`](../../../../../tasks/templates/task-micro.md)
and set `template: micro` in the front-matter; `validate_tasks.py` then
requires only `## Goal`, `## Acceptance criteria` (with checkbox todos), and
`## Verification`.

Micro tasks are the lane for work whose evidence is the PR itself:

- **interactive sessions** (pair/delegate/advisor postures per
  `docs/agent/prompt/prompt.md`), regardless of slice count — the human, the
  risk gates, the diff, and CI supply the review; and
- **one-slice mechanical work** in any mode: small fixes, doc/link sweeps,
  config toggles, test-only additions.

Unattended overnight work uses the full template (or the method/bug/review
variants) with the `standard` or higher profile. The risk gates apply on top
of the lane: dependency-boundary or public `.cppm` surface changes need the
explicit human OK and inventory refresh, and research claims need
ARA/benchmark evidence regardless of profile. Record the maturity stop-state
in the note when it is ambiguous. Micro tasks set `workflow_profile: micro`,
`evidence: not_applicable`, and a concrete `evidence_skip_reason`; they do not
inherit high-risk, claim-grade, or protected custody. Retirement rules
otherwise remain unchanged.

An open enrolled task that finishes interactively may be re-profiled to the
micro lane in its retiring change (set `template: micro`,
`workflow_profile: micro`, `evidence: not_applicable`, and the skip reason)
instead of generating a completion report; validators check the consistency of
the final front-matter, not the lane history.

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
   [`tasks/done/RETIREMENT-LOG.md`](../../../../../tasks/done/RETIREMENT-LOG.md), and
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

   If the final completed report records `source.dirty: true`, commit the
   retirement surface and evidence first, run `workflow_evidence.py
   seal-report --task-id <TASK-ID> --revision HEAD --reason <why>`, and commit
   the generated `tasks/evidence/<TASK-ID>/seal.yaml` before the final
   validation. Active dirty reports remain live-worktree-bound; the seal is the
   post-commit identity that lets a retired report validate against its own
   immutable source/artifact tree after later tasks update shared files. See
   [`workflow-evidence.md`](../../../../../docs/agent/workflow-evidence.md#completion-evidence) for the
   migration and tamper rules.

Category READMEs describe current task state only. Remove retired entries from
open lists and use `tasks/done/RETIREMENT-LOG.md`, the retired task file, and the
archive index for history. `check_task_state_links.py` enforces lifecycle
accuracy for task indexes; explicitly mixed dependency-DAG sections keep their
existing state-link-guard comment until their tracked cleanup.

Retired files stay in `tasks/done/` short-term; they are periodically swept
to `tasks/archive/` (frozen read-only history — see
[`tasks/archive/README.md`](../../../../../tasks/archive/README.md)). Archived IDs
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

See [`task-maturity.md`](../../../../../docs/agent/task-maturity.md) for the taxonomy and the
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
