# Agent workflow evidence

This document defines the prospective, versioned evidence and custody workflow
introduced by `PROC-028`. It extends the existing task, review, benchmark, and
ARA authorities; it does not replace them.

## Scope

This machinery serves two situations:

- **Unattended overnight runs** — loop or fleet sessions executing night-ready
  tasks without a human present (`docs/agent/prompt/prompt.md` §"Unattended
  overnight mode"). Claims, the live work graph, receipts, and completion
  evidence substitute for the human who is not there.
- **Opt-in custody** — the `claim-grade` and `protected` profiles for
  publication-bound research claims.

Interactive pair/delegate/advisor sessions do not use this machinery. They
ride the micro lane (`docs/agent/task-format.md` §"Micro tasks"), where the PR
diff, tests, and CI are the evidence.

## Prospective migration

`workflow_schema: 1` enrolls a task. New tasks copied from
`tasks/templates/task.md` or `tasks/templates/task-micro.md` are enrolled by
default. The exact content hashes of open tasks that predate the policy are
frozen in `tools/agents/workflow_legacy_tasks.json`. An untouched path/hash
pair remains grandfathered. A new, renamed, promoted, or edited open task that
is not byte-identical to that inventory must enroll; this makes the effective
date deterministic without rewriting historical task records.

Archived tasks and historical done tasks are never backfilled. An enrolled
task keeps its workflow fields when it moves to `tasks/done/`.

## Cumulative profiles

| Profile | Trigger | Additional required custody |
|---|---|---|
| `micro` | Interactive sessions (pair/delegate/advisor) and one-slice mechanical work | No full report; `evidence: not_applicable` and a concrete `evidence_skip_reason` |
| `standard` | Routine non-trivial work in unattended overnight runs | Generated `report.yaml` and at least one successful required command receipt |
| `high-risk` | Architecture/layer policy, public contracts, strict CI/policy, releases, or an explicitly high-risk task | Standard evidence plus append-only handoff and fixed-surface independent terminal review |
| `claim-grade` | Work intended to support a research, performance, parity, or capability claim | High-risk custody plus frozen protocol, sealed run, cell journal, portable bundle, and independent audit |
| `protected` | Claim-grade work with held-out/private data or a scientifically single-use attempt | Claim-grade custody plus result-free prospective review, separate launch authorization, and atomic one-shot consumption |

The profiles are cumulative. A report or bundle does not make a result claim
eligible. `claim_eligible` is explicit and defaults to false.

## Task metadata and claims

An active enrolled task has these front-matter fields:

```yaml
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: agent-label
branch: topic/branch
worktree: /absolute/worktree/path
claimed_at: "2026-07-29T12:00:00Z"
```

Use one writer per worktree. Parallel coding work uses separate branches and
worktrees. Acquire ownership before substantive edits:

```bash
python3 tools/agents/task_claim.py acquire \
  --task-id PROC-999 \
  --owner agent-label \
  --path tools/agents
```

Claims live under the Git common directory, so sibling worktrees see the same
ledger. A task identity cannot have two live owners, a worktree cannot have
two live writers, and explicit path claims cannot overlap by ancestor or
descendant. Paths are optional because isolated tasks normally need only the
task claim. Claims expire after their bounded lease; a stale claim is
diagnosed and must be explicitly recovered with an actor and reason. Release
and recovery records move to retained history rather than disappearing.
Every acquisition carries a unique generation value so a release/reacquire is
distinguishable even when owner, branch, worktree, and wall-clock second are
unchanged. Cooperative owner labels are routing metadata, not authentication.

## Live agent work graph

Every claimed non-micro task uses the repository-native work graph after the
task claim is acquired. The graph makes the current action, ready parallel
checks, blocking findings, bounded repair loop, and profile-gated review step
visible to the operator and to sibling worktrees. It is deliberately a live
projection rather than another durable task backlog.

Start the checked-in review diamond with the same owner label as the live task
claim:

```bash
python3 tools/agents/agent_work_graph.py start \
  --task-id PROC-999 \
  --owner agent-label \
  --recipe tools/agents/work_graphs/review-diamond.v1.json \
  --base-ref origin/main

python3 tools/agents/agent_work_graph.py show --task-id PROC-999
```

If an unfinished task claim is released, recovered, or reacquired, the live
claim holder explicitly resumes the existing run instead of starting a second
history. This is required even when the replacement uses the same owner label
and worktree:

```bash
python3 tools/agents/agent_work_graph.py resume \
  --task-id PROC-999 --owner replacement-label \
  --reason "Continue after recovered claim"
```

Resume preserves succeeded nodes and attempt counts, rebinds the exact claim
generation plus owner, branch, and worktree, and invalidates any abandoned
running node plus its descendants. It rejects recipe/profile drift, terminal
runs, and stale replacement claims. An abandoned running node that already
spent its retry budget becomes `failed`, leaving the rebound owner a visible
blocked run that can be aborted instead of an unrecoverable stale record.

The schema-v1 recipe is strict JSON. It declares stable node IDs, dependency
edges, node kind, read/write permission, minimum workflow profile, attempt
budget, independent-actor requirement, and exactly one final source-binding
node. Validation requires an acyclic graph, makes every node lead to that final
node, rejects parallel write-capable nodes, and requires at least one writer
plus the final binder to remain active for every supported non-micro profile.
The default graph contains:

```text
context -> plan -> implement -> freeze_diff
                                  |-> architecture_review --|
                                  |-> verification ---------|-> findings_join
                                  |-> docs_evidence_review --|        |
                                                                     v
                           profile-gated independent_review -> finalize
```

Nodes above the task's profile become `profile_skipped`, but skipping a node
does not bypass its prerequisites. A standard task therefore reaches
`finalize` only after the full standard review join, while a high-risk task
also requires the independent node.

Use explicit transitions; the tool never launches a model or shell command:

```bash
python3 tools/agents/agent_work_graph.py begin \
  --task-id PROC-999 --node implement --actor agent-label
python3 tools/agents/agent_work_graph.py finish \
  --task-id PROC-999 --node implement --actor agent-label \
  --outcome succeeded --note "Implementation and focused tests complete."
```

Only the live claim owner may enter a write-capable node. An `independent`
node rejects that owner label; labels remain cooperative routing metadata, so
the durable high-risk `reviews.jsonl` record still supplies the actual
fixed-surface acceptance gate. A node may finish as `succeeded`, `failed`, or
`blocked`. Successful writer completion freezes the current non-evidence
source digest. Every downstream begin/finish and final binding rejects a later
source change; the changed surface can proceed only by reopening the write
lane, which clears the frozen digest, invalidates every descendant, and
retains prior attempts in the append-only event history:

```bash
python3 tools/agents/agent_work_graph.py reopen \
  --task-id PROC-999 --node implement --actor agent-label \
  --reason "Address blocking review findings"
```

`reopen` is the bounded repair transition inside one slice: it retains that
node's attempt count and rejects an exhausted budget. A task whose checked-in
`## Slice plan` declares another implementation slice uses `advance-slice`
instead. The transition preserves the run identity and append-only event chain,
records the prior node/source projection and its `reviewed` disposition, then
starts the selected plan subgraph with fresh per-slice attempt counters at the
exact clean `HEAD` commit:

```bash
python3 tools/agents/agent_work_graph.py advance-slice \
  --task-id PROC-999 --owner agent-label --from-node plan \
  --reason "Begin the next declared slice"
```

The task must remain under `tasks/active/`, its live claim generation, owner,
branch, worktree, profile, and checked-in recipe must match exactly, and the
worktree must be clean. The reset root and its descendants must contain every
active write node and the final surface binder; nodes outside that region must
already have succeeded. Notes and ancestors are retained, while status,
attempts, actors, timestamps, outcomes, artifacts, and surface bindings reset
only inside the selected region. `show` and `list` expose the resulting slice
index. Runs created before slice indexing are read as slice 1.

There is one narrow recovery form for a committed source change after the last
writer succeeded but before any downstream review/freeze node started. The
operator must inspect and acknowledge both facts explicitly:

```bash
python3 tools/agents/agent_work_graph.py advance-slice \
  --task-id PROC-999 --owner agent-label --from-node plan \
  --reason "Roll the clean checkpoint into the next declared slice" \
  --accept-pre-review-checkpoint --accept-stale-source
```

That event is recorded as `rolled-forward-before-review`, including the
accepted stale bindings. It is not review evidence. Advancement rejects dirty
state, inactive or plan-less tasks, claim/profile/recipe drift, running nodes,
failed or blocked outcomes, any started downstream review attempt, and reset
regions that omit a writer or final binder. A stale reviewed/final binding also
requires `--accept-stale-source`; the flag acknowledges a committed transition
to the next baseline and never permits dirty bytes.

Later conversational input is attached to the node where it must be honored,
without mutating graph topology:

```bash
python3 tools/agents/agent_work_graph.py note \
  --task-id PROC-999 --node verification --actor operator \
  --kind constraint --text "Retain the sanitizer reproduction."
```

Current state is one atomic JSON record under
`<git-common-dir>/intrinsic-agent-work-graphs/v1/`; its sibling JSONL log is
append-only and hash-chained. Both bind the task ID, claim owner, branch,
worktree, exact claim-record generation, task profile, checked-in recipe
digest, base revision, writer-frozen review digest, and node events. `show` and
`list` take the same projection lock as transitions, so they cannot observe
the normal event-append/state-replace window.

That projection lock is a directory mutex whose holder is identified rather
than inferred from elapsed time. Each holder publishes a record naming its
token, pid, host, and acquisition time inside the lock directory. A waiter
breaks the lock only when the holder is provably gone — same host, dead pid —
or when the record is genuinely stale: unreadable past a short grace period, or
belonging to a foreign host past a much longer window. A holder that is simply
slow, such as a `finish` hashing a large changed surface, therefore keeps its
lock, and a contending reader waits or fails with `timed out waiting for the
work-graph lock` rather than reading a torn projection. On release a holder
compares the recorded token and refuses to remove a lock it no longer owns,
reporting that the lock `was taken over by another holder` instead of deleting
its successor's; when the critical section itself failed, that release stays
quiet so the original error is not masked.

`finalize` records the exact
already-reviewed non-evidence changed-surface digest; it cannot absorb a later
source edit. Subsequent source/task/docs changes make the completed graph
visibly `stale`. Success and abort are immutable terminal states and retain
both files. They remain inspectable after the task claim is released; active
runs still require their exact bound live claim for normal status and
transitions, or an explicit `resume` against a replacement generation.

This state is not checked in and is not completion evidence. CI validates the
recipe, CLI, and regression contract, not a developer's live common-directory
record. Task scope remains in `tasks/`, ownership remains in `task_claim.py`,
command and completion evidence remain in `workflow_evidence.py`, independent
review remains in `reviews.jsonl`, and claim-grade/protected state remains in
`experiment_custody.py`. `CI-012`, `CI-013`, and `PROC-031` separately own the
planned verification graph and receipt binding; work-graph nodes must not
pretend those planned receipts already exist.

## Completion evidence

Run important commands through the receipt wrapper:

```bash
python3 tools/agents/workflow_evidence.py record-command \
  --task-id PROC-999 \
  --label task-policy \
  -- python3 tools/agents/check_task_policy.py --root . --strict
```

The receipt records exact argv, repository-relative working directory,
timestamps, duration, exit code, required/optional status, and hashed stdout
and stderr logs. Labels are non-overwriting.

Command stdout/stderr logs are preserved as raw hash-bound bytes.
`tasks/evidence/.gitattributes` marks them as binary so Git does not normalize
line endings or reinterpret captured tool whitespace.

Generate the report after task checkboxes and the review surface are final:

```bash
python3 tools/agents/workflow_evidence.py generate-report \
  --task-id PROC-999 \
  --base-ref origin/main \
  --complete \
  --extension-seam "Schema-versioned plain files" \
  --future-change-plan "Add fields only through a schema migration" \
  --self-review-complete
```

The generator derives the task/profile, acceptance checklist, Git base/head,
changed paths, per-file hashes, aggregate content digest, receipts, branch,
worktree, and dirty state. Caller-supplied fields cover touched layers/modules,
public contracts, diagnostics/previews, benchmark/parity evidence, artifacts,
residual risks, and justified skips. Referenced artifacts and logs carry
SHA-256 hashes.

Validate enrolled work with:

```bash
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/workflow_evidence.py validate \
  --root . --require-complete PROC-999
```

The ordinary repository scan requires complete evidence for enrolled tasks in
`tasks/done/`. `--require-complete` applies the same retirement gate while a
task remains active. Missing or failed required receipts, unchecked or
unaddressed acceptance criteria, task/profile mismatch, changed source
surface, stale artifact hashes, missing reasons, and absent profile-specific
records are blocking errors. A justified skipped check is an explicit warning
and remains visible in the report.

The aggregate content digest is always recomputed from the recorded surface,
including the valid empty-surface case. An empty diff therefore has one
deterministic digest; it is not an escape hatch for a caller-selected value.
Tracked symlink entries are hashed as their lexical link bytes in the worktree,
matching the mode-120000 Git blob used by fixed-revision validation; report
generation never substitutes the current symlink target's contents.
Referenced artifacts retain that lexical entry hash but are stricter than the
changed-path surface: their symlink chain must remain inside the repository and
terminate at a regular file. Generation and validation enforce the same rule in
the current worktree for dirty reports and in the exact recorded Git tree for
clean reports, so an external or broken link cannot be substituted after
review. Resolution walks components and expands each symlink before applying a
following parent-directory component; it never pre-normalizes across a symlink
or permits an intermediate target to leave and re-enter the repository.

For a report with `source.dirty: false`, validation reads the recorded surface
and referenced artifact blobs from the exact `source.head_revision` commit and
compares `source.base_revision..source.head_revision`. This keeps a
fixed-revision draft auditable after unrelated work lands on the current
branch, including later edits to a shared artifact path.

An active report with `source.dirty: true` remains bound to the current
worktree and is invalidated by any subsequent worktree or referenced-artifact
change. Once that report is complete and the task retires, commit its final
report, handoff/review records, receipts, and source surface, then create a
post-commit historical seal:

```bash
python3 tools/agents/workflow_evidence.py seal-report \
  --task-id PROC-999 --revision HEAD \
  --reason "Bind the completed report to its reviewed retirement commit"
git add tasks/evidence/PROC-999/seal.yaml
git commit -m "PROC-999 seal completion evidence"
```

`seal-report` with `HEAD` requires a clean worktree. The generated
`tasks/evidence/<TASK-ID>/seal.yaml` records the exact commit, unchanged report
hash and source digest, plus exact handoff/review record hashes. The referenced
commit must be an ancestor of the current branch and contain exactly one
canonical task record, the byte-identical report and review records, every
recorded changed-surface blob, and every artifact at its recorded hash.
Validation then reuses the fixed-revision source/artifact path instead of
comparing a retired report with today's worktree. The current report and
handoff/review files must still match the seal, so editing the visible custody
record fails closed; later tasks may legitimately change or move shared source,
artifact, and experiment paths without rewriting history.

An existing unchanged retired report may be migrated only by naming an exact
40-hex historical commit with `--revision`. The command proves that commit
contains the current report/task/records and recomputes its source diff and
artifacts before writing a seal. It rejects a missing, unrelated, ambiguous, or
hash-mismatched revision. Never regenerate an accepted report against a later
broader surface merely to make validation green.

## High-risk handoff and review

`high-risk` and higher reports point to append-only, hash-chained JSONL:

- `handoff.jsonl` records the exact commit or `worktree:<sha256>` revision,
  content digest, changed surface, evidence, assumptions, failed hypotheses,
  known traps, untested surfaces, human decisions, disagreements, and next
  action.
- `reviews.jsonl` records driver and reviewer labels, cooperative identity
  assurance, exact reviewed revision/content digest, revision count,
  findings, untested surfaces, disagreements, escalation, and disposition.

Append them with `append-handoff` and `append-review` subcommands. Self-review
is explicitly provisional and cannot emit `accepted`. A complete high-risk
report requires the latest review to be `accepted`, independent, label-distinct
from the driver, and bound to the report's current content digest and exact
source revision. The exact revision is derived rather than caller-selected: a
clean report binds `source.head_revision`, while a dirty report binds
`worktree:<source.content_digest>`. Both append commands reject a mismatched
revision or digest, and validation independently rechecks the latest handoff
and review against the report. Historical rounds remain append-only and may
bind older surfaces. More than three revision rounds requires an escalation
record. Other terminal dispositions are `rejected`, `inconclusive`, and
`superseded`; `revision-requested` is non-terminal.

## Claim-grade experiment custody

`tools/agents/experiment_custody.py` owns plain-file experiment state. A ready
protocol uses experiment-protocol schema version 2 and declares:

- question, hypothesis, claim boundary, evidence phase, and explicit claim
  eligibility;
- byte-hashed datasets, disjoint splits, seeds, and input policy;
- matched comparators/budgets, primary metrics, statistical units/tests,
  frozen raw-column/summary-statistic definitions, and killing gates;
- screening/confirmation policy, resources, exact argv, expected artifacts,
  and blockers;
- exact source revision/cleanliness plus hashed config, environment, and
  implementation files.

Protocol schema version 2 adds the mandatory frozen `summaries` declarations.
Version 1 protocols are rejected with an explicit migration diagnostic rather
than silently inferring result-sensitive aggregation after the fact. Run,
bundle, audit, and task-workflow records retain their own existing schema
versions.

`freeze-protocol` validates and seals that content. A frozen protocol is
immutable; changing it invalidates its digest and requires a new run identity.
`init-run` creates a non-overwriting canonical run root and binds the frozen
protocol, task hash, source, config, environment, datasets, and implementation.
Claim-eligible initialization requires a clean worktree and an exact commit.
For every protocol that declares `source.clean: true`, whether claim eligible
or not, the source revision must be an exact resolving commit that is an
ancestor of the validating `HEAD`. Every declared dataset, config, environment,
and implementation path must be a repository-relative regular file with its
declared hash in that exact source commit. Those input seals continue to
validate against the fixed revision after later commits change or remove the
current-worktree paths. The declared path is looked up lexically in the Git
tree, so a current-worktree symlink cannot redirect the revision check. A
dirty-source protocol remains bound to its live worktree inputs instead.

A clean fixed source identity does not authorize a claim. `claim_eligible`
remains a separate explicit decision, defaults to false, and still adds the
clean-worktree requirement at initialization. Scratch-phase protocols can
never be claim eligible.

Run validation compares the recorded task identity, claim eligibility, source,
config, environment, dataset seals, implementation digest, and exact command
with the frozen protocol; merely pointing each field at an existing hash-valid
file is insufficient. Substituting one valid seal for another therefore fails.
An independently audited rejected run with a clean exact source may retain its
recorded task digest against the task blob under the canonical
`tasks/active|backlog|done|archive` lifecycle roots at that frozen source
revision after the task advances to a corrected run. This is a
negative-evidence exception:
the canonical bundle and audit must still recompute to the recorded rejection,
and accepted, unaudited, dirty-source, or malformed runs remain bound to the
current task bytes with no historical fallback.

`append-cell` maintains a hash-chained journal with stable cell keys.
Transitions are `started` to `completed`, `failed`, or `missing`; terminal
keys cannot be reused. Failures retain errors, missing cells retain reasons,
and abandoned/negative work is not erased.

`create-bundle` writes resolved configuration and provenance, raw tidy rows,
recomputed summaries and gates, diagnostics, relative hashed links,
previews/readbacks for visual work, exact replay/view argv, and successful
smoke receipts. Large artifacts remain external but must have path/hash
bindings. `audit-bundle` rejects any bundle summary or gate declaration that
differs from the frozen protocol, recomputes summaries from raw rows using the
frozen column/statistic definitions, and then recomputes gate dispositions
using the frozen metric, operator, and threshold. Bundle source, task,
implementation, dataset, resolved-config, and environment provenance must
exactly equal the initialized frozen run. The audit also validates links and
smoke receipts and writes a separate terminal audit receipt. A bundle link that
names one of the protocol's sealed inputs may resolve to a regular file at the
fixed clean source revision when the current path has changed. Raw rows,
results, receipts, previews, audits, and all other post-run evidence remain
current-tree artifacts with no historical fallback; neither bundle nor audit
may authorize a claim.

Workflow completion for `claim-grade` and `protected` profiles invokes the
experiment-custody completion gate. At least one canonical run must bind a
frozen matching protocol, have a non-empty cell journal with no still-started
cells, carry a valid portable bundle, and carry an accepted independent audit
bound to the frozen source revision and current bundle. Failed, abandoned, or
incomplete runs remain visible but do not satisfy completion. A `protected`
completion additionally requires current result-free prospective review,
separate authorization, and the initialized attempt recorded terminally as
`failed` or `completed`.

For an existing canonical benchmark result, pass `--benchmark-result` instead
of `--raw-rows` and point `--benchmark-manifests-root` at its manifest tree.
The bundle command invokes the strict schema-v2 benchmark validator, derives a
single tidy row from the bound metric payload, and records the result hash plus
stable benchmark/run/attempt identities. The audit repeats strict validation
and rejects a stale result binding or raw rows that no longer derive exactly
from those metrics. A bundle may retain a canonical `skipped`, `failed`, or
`error` result as inspectable negative evidence, but positive audit requires
both `execution_status: passed` and recomputed `status: passed`; otherwise the
audit is rejected and cannot satisfy completion. The checked-in
`geometry.example.small` payload is the non-claim-eligible regression fixture
for this bridge.

## Protected attempts

The `protected` profile adds two separate, result-free records:

1. `prospective-review` binds the exact frozen protocol and implementation
   digests, review method/boundary, source coverage, distinct reviewer label,
   and exactly zero protected interactions/private draws.
2. `authorize-protected` binds that review and records a different authorizer
   label and launch boundary.

A machine rehearsal is recordable but cannot authorize launch. The reviewer
cannot self-authorize. Any protocol, implementation, or review digest change
invalidates authorization.

After initialization, `consume-attempt` creates the attempt identity with an
exclusive filesystem create. `started` consumes the identity even if the
process later becomes `failed`; `completed` and `failed` retain the original
start event. No state permits a retry under that attempt ID.

The checked-in fixture under
`tools/agents/fixtures/protected-synthetic/` contains only public synthetic
rows and no network reference. Regression tests use it to exercise the full
review/authorization/consumption policy without touching real protected data.

## Required gates

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/experiment_custody.py validate --root .
python3 tools/agents/experiment_custody.py validate-completion \
  --root . --task-id METHOD-999 --profile claim-grade \
  --experiment-root tasks/evidence/METHOD-999/experiment
python3 tests/regression/tooling/Test.WorkflowEvidence.py
python3 tests/regression/tooling/Test.ExperimentCustody.py
python3 tests/regression/tooling/Test.TaskClaim.py
```

These run in `ci-docs.yml`. Task evidence does not replace build, test,
benchmark, documentation, maturity, or ARA gates owed by the touched scope.
