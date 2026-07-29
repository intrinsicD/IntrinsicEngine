---
id: PROC-028
theme: H
depends_on: [BUG-120]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex"
branch: "codex/proc-028-enforced-agent-workflow"
worktree: "/home/alex/Documents/IntrinsicEngine-proc-028"
claimed_at: "2026-07-29T22:27:54Z"
---
# PROC-028 — Enforce agent evidence, review, and experiment custody

## Status

- Implementation complete on `codex/proc-028-enforced-agent-workflow`;
  owner: Codex.
- Isolated worktree: `/home/alex/Documents/IntrinsicEngine-proc-028`.
- Focused workflow/benchmark regressions, strict touched structural gates,
  `ci` configuration, `IntrinsicTests`, `IntrinsicBenchmarks`, strict generated
  benchmark validation, and the complete default CPU-supported CTest selector
  pass locally.
- Retirement remains blocked on a distinct fixed-revision independent review
  and on pre-existing `BUG-120`: `Test.WorkflowConcurrency.py`, already wired
  into `ci-docs`, fails against byte-identical `origin/main` sources because
  its expected CTest reservation/config-root snapshot has drifted.

## Goal

- Implement one versioned, risk-tiered agent workflow that turns non-trivial
  task completion, high-risk review, claim-grade experiments, protected
  one-shot attempts, result bundles, and concurrent task ownership into
  machine-verifiable repository evidence.

## Non-goals

- No engine feature, C++ module, runtime, graphics, geometry, asset, ECS,
  physics, platform, or app behavior changes.
- No wholesale installation of agent-kit or replacement of IntrinsicEngine's
  existing task tree, skills, ARA ledger, method workflow, benchmark workflow,
  maturity taxonomy, or CI gate topology.
- No mandatory second-agent review, frozen experiment protocol, result bundle,
  or one-shot attempt custody for routine and micro tasks.
- No retrospective evidence-report backfill across every retired or archived
  task. Migration applies prospectively, with a bounded rule for open tasks
  materially changed after the effective date.
- No cryptographic or organizational identity claim. Driver, reviewer, owner,
  and author fields are cooperative repository metadata.
- No external workflow service, database, daemon, plugin framework, or network
  dependency.
- No performance, parity, or capability claim merely because the workflow
  records or validators exist.

## Context

- The user explicitly requested on 2026-07-29 that all nine findings from the
  four-repository agent-workflow audit remain one batched task for later work.
- ARA decisions `N123` through `N125` already require generated
  `tasks/evidence/<TASK-ID>/report.yaml` records for completed non-trivial
  tasks, blocking validation for missing required evidence, and explicit
  not-applicable reasons for trivial work. The directory, generator, schema,
  and validator do not exist.
- ARA question/experiment `N289`/`N290` and staged observations `O71` through
  `O75` record the source audit and the bounded recommendations this task
  adopts.
- `tools/benchmark/validate_benchmark_results.py` currently accepts `NaN`,
  positive infinity, and negative infinity; ordinary Python JSON parsing also
  overwrites duplicate keys. It does not bind a result to the exact manifest,
  source state, threshold disposition, or distinct run/attempt identity.
- IntrinsicEngine already has strong task, layering, documentation, method,
  benchmark, ARA, sanitizer, and promoted-Vulkan gates. This task extends
  those authorities; it must not duplicate them in a parallel framework.
- Primary ownership is process tooling and policy:
  `tools/agents/`, `tools/benchmark/`, `tests/regression/tooling/`,
  `tasks/templates/`, `tasks/evidence/`, `docs/agent/`,
  `docs/benchmarking/`, and the existing strict docs/workflow CI.
- The task is intentionally multi-slice. Promote it to `tasks/active/` before
  implementation and retire it only after every slice and the bootstrap
  evidence report for `PROC-028` itself are complete.

## Workflow profiles

- `micro`: single-slice mechanical work using the micro template. It may omit
  a full report only with an explicit machine-readable not-applicable reason.
- `standard`: every other non-trivial task. Requires a generated completion
  evidence report and command receipts.
- `high-risk`: architecture/layer-policy changes, public contract changes,
  strict CI/policy gates, releases, or other scopes explicitly marked for
  independent review. Adds a durable handoff and revision-bound terminal
  review verdict.
- `claim-grade`: any task or run intended to support a research, performance,
  parity, or capability claim. Adds a frozen protocol, sealed provenance,
  append-only run/cell records, a portable result bundle, and an independent
  audit receipt.
- `protected`: claim-grade work using sealed held-out data or a scientifically
  single-use attempt. Adds result-free prospective authorization and
  append-only attempt consumption.
- Profiles are cumulative. The implementation must define an unambiguous
  prospective migration/effective-date rule and must not infer claim
  eligibility from a report's mere presence.

## Slice plan

- **Slice A — Workflow profile and task evidence spine.** Add the versioned
  profile/report schemas, command receipts, generator, strict validator,
  template fields, migration rule, adversarial fixtures, and CI wiring.
- **Slice B — Benchmark result repair.** Make JSON and numeric validation
  strict, bind results to manifests/source/gates, separate stable benchmark
  identity from run/attempt identity, migrate fixtures, and replace textual
  JSON extraction.
- **Slice C — High-risk review and durable handoff.** Add cooperative role
  labels, fixed-revision review, provisional self-review, terminal verdicts,
  bounded revision/escalation history, human decisions, known traps, and
  untested-surface recording.
- **Slice D — Claim-grade protocol, run custody, and audited bundle.** Add
  ready/init/freeze semantics, data/source/config/environment seals,
  non-overwrite canonical runs, resumable cell journals, honest negative
  results, portable bundles, and machine-readable independent audit receipts.
- **Slice E — Protected prospective authorization.** Add digest-bound,
  result-free review and authorization plus atomic started/failed/completed
  attempt consumption, gated only by the `protected` profile.
- **Slice F — Concurrent ownership and final enforcement.** Establish
  worktree-first ownership, atomic task/optional-path claims, overlap/staleness
  diagnostics, final strict CI enforcement, documentation synchronization, and
  `PROC-028`'s own complete evidence/review record.

## Right-sizing

- **Element:** a cross-cutting evidence lifecycle could become a second task
  engine, orchestration service, artifact database, or agent framework.
- **Simpler alternative:** use schema-versioned plain YAML/JSON/JSONL records,
  small free-function Python CLIs, Git history, the existing task tree, and the
  existing ARA/benchmark validators. Extend current validators where ownership
  already exists; do not add interfaces, registries, services, or plugin seams.
- **Element:** concurrency protection could become a central lock server.
- **Simpler alternative:** default to one writer per worktree and use atomic
  local records under the repository's Git common directory for task and
  optional path claims. The tracked task metadata mirrors ownership for review;
  no background process is required.
- **Blast radius:** task front-matter/templates and generated session brief;
  process/method/benchmark documentation; agent and benchmark validators;
  regression fixtures; `ci-docs` and touched-scope structural routing. Engine
  source and layer edges remain untouched.
- **Reintroduction trigger:** an external service or richer artifact store is
  justified only when a second repository or hosted producer must coordinate
  live state that Git plus bounded artifacts cannot represent. An always-on
  shared-worktree path registry is justified only by a recorded collision that
  survives the worktree-first policy.

## Required changes

- [x] Define and document one schema-versioned workflow-profile contract for
  `micro`, `standard`, `high-risk`, `claim-grade`, and `protected`, including
  exact triggers, cumulative requirements, claim eligibility, and prospective
  migration behavior.
- [x] Extend the full and micro task templates and open-task validation with
  the minimum machine-readable fields for profile, evidence applicability,
  owner, branch, claim time, and explicit skip reason. Preserve compatibility
  for untouched historical tasks.
- [x] Add `tasks/evidence/README.md` and the canonical
  `tasks/evidence/<TASK-ID>/report.yaml` shape required by `N123` through
  `N125`.
- [x] Generate completion reports from task metadata, diff/source identity,
  command receipts, and referenced artifacts rather than trusting hand-written
  pass/fail prose.
- [x] Record task ID, schema version, touched layers/modules, extension seam,
  future-change plan, acceptance-criterion dispositions, exact commands and
  exit results, changed public contracts, diagnostics/previews, benchmark or
  parity evidence, docs/task synchronization, residual risks, skipped checks,
  artifact paths/hashes, and self-review answers.
- [x] Validate reports against the task, current source/diff, required
  repository policy, command receipts, referenced paths/hashes, and
  claim/benchmark requirements wherever those facts are machine-observable.
  Fail closed when a required assertion cannot be established.
- [x] Distinguish blocking findings from explicitly scoped warnings. A skipped
  required check without a reason, an unaddressed acceptance criterion, a
  failed required command, or a missing claim proof blocks retirement.
- [x] Wire the evidence validator into the existing strict task/docs workflow.
  Any temporary warning-mode rollout must name `PROC-028` and must be removed
  before this task retires.
- [x] Make benchmark JSON loading reject duplicate object keys and non-standard
  constants; recursively reject every non-finite numeric value and avoid
  treating booleans as numeric metrics.
- [x] Cross-bind each benchmark result to its exact manifest, method, dataset,
  declared metrics, thresholds, backend, resolved parameters, warmup policy,
  and source/config identity. Compute or validate threshold disposition rather
  than trusting a reported status string.
- [x] Preserve stable `benchmark_id` as the definition/history join key while
  adding distinct append-only `run_id` and `attempt_id` identities. Define and
  test retry, supersession, and aggregation semantics without overwriting
  failed attempts.
- [x] Make uncommitted or `"local-dev"` source state explicitly
  non-claim-eligible unless a complete source snapshot/diff identity is
  captured by an approved policy. Official claim runs default to an exact
  clean commit.
- [x] Replace `tools/benchmark/check_perf_regression.sh`'s grep/sed JSON
  extraction with schema-aware parsing through the canonical benchmark
  validation/comparison path.
- [x] Migrate checked-in benchmark examples, baselines, runners, and tests to
  the new versioned result contract without renaming published
  `benchmark_id`s.
- [x] Add a durable, append-only task handoff for `high-risk` and higher
  profiles with exact revision, changed surface, evidence, assumptions, failed
  hypotheses, known traps, untested surfaces, human decisions, disagreements,
  and next action.
- [x] Bind high-risk review entries to proposer/driver label, distinct reviewer
  label, reviewed commit or content digest, self-review state, revision count,
  and terminal `accepted`, `revision-requested`, `rejected`, `inconclusive`, or
  `superseded` disposition.
- [x] Keep self-review explicitly provisional and enforce bounded revision and
  escalation handling. The schema must state that labels do not authenticate
  identity.
- [x] Define the claim-grade frozen protocol fields: question, hypothesis,
  claim boundary, evidence phase, claim eligibility, datasets and byte hashes,
  disjoint splits, seeds, input policy, matched comparators/budgets, primary
  metrics, statistical unit/tests, killing gates, screening/confirmation
  policy, resources, exact command, expected artifacts, and blockers.
- [x] Add ready/init/freeze semantics: scratch work cannot become claim
  evidence; official initialization binds task/protocol/data/source/config/
  environment identities; initialized protocols are immutable; changed
  protocols require a new run identity; canonical run roots are non-overwriting.
- [x] Add resumable append-only records with stable cell keys and explicit
  started/completed/failed/missing states. Preserve errors, exclusions,
  negative results, and abandoned attempts; forbid post-result retuning of
  frozen gates or confirmation choices.
- [x] Generate a portable result bundle containing resolved configuration,
  provenance, raw tidy rows, metrics, diagnostics, relative resolvable links,
  previews/readbacks when visual, exact replay/view commands, and smoke
  receipts. Large artifacts remain external and are referenced by path/hash.
- [x] Add an independent machine-readable audit receipt that identifies the
  auditor label and audited revision, recomputes declared summary values and
  gates from raw records, checks bundle links/artifacts, and records a terminal
  disposition without silently authorizing a claim.
- [x] For the `protected` profile only, require result-free prospective review
  bound to exact protocol and implementation digests, declared review method
  and boundary, adequate source coverage, and zero protected interactions or
  private draws.
- [x] Separate prospective review from launch authorization. Any relevant
  implementation/protocol digest change invalidates both and requires a fresh
  review; a reviewer may refuse or report no blocker without self-authorizing
  the run.
- [x] For scientifically single-use work, atomically consume one attempt as
  `started`, `failed`, or `completed`; all three terminal histories remain
  visible and no retry is allowed under the same attempt identity.
- [x] Document and enforce one writer per worktree by default. Parallel coding
  agents use separate branches/worktrees and establish task ownership before
  substantive edits.
- [x] Add a small atomic claim CLI using the Git common directory for task
  identity and optional explicit paths. It must reject overlapping live
  claims, diagnose stale/abandoned claims, support bounded release/recovery,
  and mirror owner/branch/worktree/claim time into tracked task metadata.
- [x] Keep path claims conditional: if the final policy forbids shared-worktree
  parallel writes entirely, implement only the minimum cross-worktree overlap
  protection and record why an always-on path registry is not applicable.

## Tests

- [x] Add adversarial task-evidence tests for missing fields, duplicate keys,
  task-ID mismatch, unaddressed acceptance criteria, missing/failed command
  receipts, artifact hash mismatch, unjustified skips, absent required review,
  stale reviewed revision, self-review presented as independent, and invalid
  terminal verdict/status pairs.
- [x] Extend `Test_BenchmarkResultValidator.py` with duplicate-key, `NaN`,
  positive/negative infinity, boolean-as-number, nested invalid metric,
  manifest mismatch, undeclared metric, threshold/status mismatch, source
  identity, duplicate run/attempt, and repeated-stable-benchmark-ID cases.
- [x] Add regression coverage for schema-aware performance comparison and
  remove tests that depend on first-textual-key grep behavior.
- [x] Add claim-grade protocol tests for dirty official source, changed task or
  seal after initialization, overwrite attempts, changed protocol under one
  run ID, reused cell keys, missing/error visibility, retuned gates, invalid
  relative links, inconsistent report numbers, missing preview/readback, and
  absent replay/view smoke receipt.
- [x] Add protected-profile tests for stale digests, nonzero protected access,
  review/authorization conflation, machine rehearsal presented as independent
  review, second attempt creation, and retry after each of
  `started`/`failed`/`completed`.
- [x] Add temporary-repository/worktree tests for atomic claim acquisition,
  overlapping task/path claims, separate non-overlapping claims, stale claim
  recovery, wrong-owner release, and one-writer shared-worktree enforcement.
- [x] Prove `micro` and routine `standard` tasks do not inherit high-risk,
  claim-grade, or protected ceremony.
- [x] Run the existing strict task, docs, link, skill-mirror, ARA, method,
  benchmark-manifest, benchmark-result, layering, test-layout, and root-hygiene
  checks after final integration.
- [x] Configure `ci`, build `IntrinsicTests`, and run the complete default
  CPU-supported selector before retirement, even though this task changes no
  engine source.

## Docs

- [x] Update `AGENTS.md` only for the final always-on profile, evidence,
  review, experiment, and concurrency invariants; keep implementation details
  in canonical `docs/agent/*` and `docs/benchmarking/*` documents.
- [x] Update `docs/agent/task-format.md`, `docs/agent/prompt/prompt.md`,
  `docs/agent/review-checklist.md`, `docs/agent/benchmark-workflow.md`,
  `docs/agent/benchmark-review-checklist.md`, and
  `docs/agent/ara-evidence-policy.md` where their current procedures change.
- [x] Update `docs/benchmarking/benchmark-manifest-schema.md`,
  `docs/benchmarking/result-json-schema.md`,
  `docs/benchmarking/dataset-policy.md`,
  `docs/benchmarking/report-template.md`, and relevant indexes/READMEs.
- [x] Update task templates, `tools/agents/README.md`,
  `tools/benchmark/README.md`, process indexes, PR template, and CI/workflow
  documentation to match the implemented commands and strict gates.
- [x] Re-run `python3 tools/agents/sync_skills.py --write` after canonical
  `docs/agent/*` or task-template changes; never edit generated reference
  mirrors directly.
- [x] Resolve or crystallize ARA observations `O71` through `O75` only when
  implementation artifacts and validation evidence justify their final form.

## Acceptance criteria

- [x] All nine `N290` gaps are closed by one coherent, versioned profile model:
  generated task evidence; strict benchmark results; revision-bound
  independent review; durable handoff; frozen claim-grade protocols; immutable
  run custody; protected prospective authorization; portable audited bundles;
  and enforceable concurrent ownership.
- [x] Every non-trivial task in the prospective enforcement scope either has a
  valid generated report or is blocked from retirement; every micro exemption
  has an explicit validated reason.
- [ ] `PROC-028` itself has a valid generated `report.yaml`, complete command
  receipts, final fixed-revision independent review, terminal verdict, and no
  unresolved blocking evidence finding.
- [x] Existing task, skill, method, benchmark, ARA, docs, and CI authorities
  remain canonical; no parallel workflow state engine or donor framework is
  introduced.
- [x] The benchmark validator rejects duplicate keys and all non-finite metric
  values, binds results to exact manifests and source state, and retains
  multiple append-only runs for one stable `benchmark_id`.
- [x] One small existing benchmark smoke is migrated end to end through the
  strict result, bundle, replay/view-receipt, and independent-audit path without
  making a new performance claim.
- [x] A repository-contained synthetic protected fixture proves review,
  authorization, digest invalidation, and one-shot consumption without
  accessing real protected evidence.
- [x] Worktree/task claims prevent overlapping live ownership before
  substantive edits; routine single-agent work remains one short claim step
  and requires no daemon or external service.
- [x] Historical retired/archive tasks are not mass-backfilled, and the
  migration rule is deterministic for every open/new task.
- [x] All new validators have direct negative regression fixtures and run
  strict in the appropriate required workflow before retirement.
- [x] Documentation describes implemented current behavior, generated skill
  mirrors are current, and task/session indexes contain no stale state.

## Verification

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/sync_skills.py --check
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/agents/validate_method_manifests.py --root methods --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root benchmarks --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/agents/generate_session_brief.py --check

# Add focused Python regression commands for each implemented workflow tool.

cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes

- Engine source, public C++ module, layer dependency, rendering recipe, runtime
  composition, or backend behavior changes.
- Weakening or removing an existing strict validator to make the new workflow
  pass.
- Hand-authored required command success without a generated receipt, or a
  completion report that can override observed failures.
- Treating cooperative role labels as authenticated identity.
- Renaming stable published `benchmark_id`s or overwriting prior failed,
  negative, missing, or inconclusive attempts.
- Promoting scratch, dirty, retuned, or prospectively unauthorized work into a
  claim-bearing result.
- Applying protected one-shot controls to ordinary tests, routine benchmarks,
  or standard task execution.
- A service, registry, database, daemon, plugin interface, or external
  coordination dependency where plain files, Git, and existing validators
  suffice.
- A repository-wide historical evidence backfill.
- Mechanical file moves mixed with semantic policy or validator changes.
