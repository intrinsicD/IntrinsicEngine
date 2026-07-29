# Agent workflow evidence

This document defines the prospective, versioned evidence and custody workflow
introduced by `PROC-028`. It extends the existing task, review, benchmark, and
ARA authorities; it does not replace them.

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
| `micro` | One-slice mechanical work allowed by the micro template | No full report; `evidence: not_applicable` and a concrete `evidence_skip_reason` |
| `standard` | All other routine non-trivial work | Generated `report.yaml` and at least one successful required command receipt |
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
Cooperative owner labels are routing metadata, not authentication.

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

For a report with `source.dirty: false`, validation reads the recorded surface
and referenced artifact blobs from the exact `source.head_revision` commit and
compares `source.base_revision..source.head_revision`. This keeps a
fixed-revision draft auditable after unrelated work lands on the current
branch, including later edits to a shared artifact path. A report with
`source.dirty: true` remains bound to the current worktree and is invalidated
by any subsequent worktree or referenced-artifact change.

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
from the driver, and bound to the report's current content digest. More than
three revision rounds requires an escalation record. Other terminal
dispositions are `rejected`, `inconclusive`, and `superseded`;
`revision-requested` is non-terminal.

## Claim-grade experiment custody

`tools/agents/experiment_custody.py` owns plain-file experiment state. A ready
protocol declares:

- question, hypothesis, claim boundary, evidence phase, and explicit claim
  eligibility;
- byte-hashed datasets, disjoint splits, seeds, and input policy;
- matched comparators/budgets, primary metrics, statistical units/tests, and
  killing gates;
- screening/confirmation policy, resources, exact argv, expected artifacts,
  and blockers;
- exact source revision/cleanliness plus hashed config, environment, and
  implementation files.

`freeze-protocol` validates and seals that content. A frozen protocol is
immutable; changing it invalidates its digest and requires a new run identity.
`init-run` creates a non-overwriting canonical run root and binds the frozen
protocol, task hash, source, config, environment, datasets, and implementation.
Claim-eligible initialization requires a clean worktree and an exact commit.
Scratch-phase protocols can never be claim eligible.

`append-cell` maintains a hash-chained journal with stable cell keys.
Transitions are `started` to `completed`, `failed`, or `missing`; terminal
keys cannot be reused. Failures retain errors, missing cells retain reasons,
and abandoned/negative work is not erased.

`create-bundle` writes resolved configuration and provenance, raw tidy rows,
recomputed summaries and gates, diagnostics, relative hashed links,
previews/readbacks for visual work, exact replay/view argv, and successful
smoke receipts. Large artifacts remain external but must have path/hash
bindings. `audit-bundle` independently recomputes summaries and gates from raw
rows, validates links and smoke receipts, and writes a separate terminal audit
receipt. An audit never authorizes a claim.

For an existing canonical benchmark result, pass `--benchmark-result` instead
of `--raw-rows` and point `--benchmark-manifests-root` at its manifest tree.
The bundle command invokes the strict schema-v2 benchmark validator, derives a
single tidy row from the bound metric payload, and records the result hash plus
stable benchmark/run/attempt identities. The audit repeats strict validation
and rejects a stale result binding or raw rows that no longer derive exactly
from those metrics. The checked-in `geometry.example.small` payload is the
non-claim-eligible regression fixture for this bridge.

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
python3 tests/regression/tooling/Test.WorkflowEvidence.py
python3 tests/regression/tooling/Test.ExperimentCustody.py
python3 tests/regression/tooling/Test.TaskClaim.py
```

These run in `ci-docs.yml`. Task evidence does not replace build, test,
benchmark, documentation, maturity, or ARA gates owed by the touched scope.
