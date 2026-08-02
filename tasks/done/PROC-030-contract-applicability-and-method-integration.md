---
id: PROC-030
theme: H
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex"
branch: "process/contract-applicability"
worktree: "/tmp/intrinsic-proc030.lxl95I"
claimed_at: "2026-08-02T12:07:17Z"
contract_schema: 1
contracts: [repo.task-contract-discovery]
maturity_target: Operational
---
# PROC-030 — Contract applicability and method-integration workflow

## Status

- Completed on 2026-08-02 at `Operational`; owner: Codex.
- Commit reference: the retirement commit containing this task on
  `process/contract-applicability`.
- The contract catalog, prospective task enrollment, method integration
  matrix, synchronized authoring/review workflow, and strict validator are
  operational; the final report and independent fixed-surface review live in
  `tasks/evidence/PROC-030/`.
- The bounded audit found one foundational ECS source drift and five method
  integration gaps. `HARDEN-087`, `RUNTIME-206..210`, and `UI-038..042` own
  the follow-up work; this task changes no engine behavior.

## Goal

- Make reusable engine contracts discoverable at task-authoring time, explicit
  in claimed task records, and enforceable through canonical documentation,
  prospective validation, review evidence, and executable proofs; inventory
  current method integrations and seed bounded refactor tasks for every
  confirmed violation.

## Non-goals

- No engine, method, runtime, ECS, graphics, or Sandbox behavior changes.
- No repair of the violating method integrations in this slice.
- No duplication of subsystem contract prose into `AGENTS.md`, task files, or
  skills.
- No retroactive rewrite of retired or archived task history.
- No blanket prohibition on exact provenance queries such as
  `GeometrySources::ActiveDomain()` where provenance is the actual question.

## Context

- The LOP runtime/UI integration narrowed an already-canonical geometry source
  capability contract because its tasks, method workflow, and tests did not
  enumerate mesh, graph, and point-cloud source applicability end to end.
- `AGENTS.md` is the repository-wide authority, canonical architecture docs own
  subsystem semantics, and skills are routing/procedure summaries rather than
  independent contract authorities.
- The existing task template has only a free-form `Context` prompt, while the
  method template covers scientific correctness without requiring ECS source,
  runtime binding, config, UI discovery, publication, and per-domain test
  disposition.
- This is a high-risk process change because it edits repository contract and
  strict task-policy surfaces. Independent fixed-surface review is required.

## Control surfaces

- Config: N/A; this is repository workflow metadata.
- UI: N/A.
- Agent/CLI: task templates, task authoring/review skills, strict task
  validation, and workflow evidence.

## Right-sizing

- **Element:** reusable contract discovery could become a new documentation
  graph, service, registry framework, or semantic source analyzer.
- **Simpler alternative:** one plain YAML routing catalog, prospective task
  metadata, extensions to the existing validator and templates, and focused
  regression tests. Canonical prose and behavior tests remain where they are.
- **Blast radius:** `AGENTS.md`, `docs/architecture/*`, `docs/agent/*`, task
  templates/records, agent skill mirrors, and existing Python tooling tests;
  no engine modules or dependency edges.
- **Reintroduction trigger:** add path/symbol inference or a graph-backed
  contract recommender only after multiple measured omissions survive the
  explicit declaration and review gates.

## Maturity

- Target: `Operational` as a strict prospective task-authoring and completion
  gate with synchronized templates, skills, review policy, and tooling tests.
- Byte-identical pre-policy tasks remain baseline-grandfathered across every
  lifecycle until changed, moved, or consumed; all new/changed tasks enroll.
  No historical content backfill is owed.

## Required changes

- [x] Add a canonical, stable-ID contract catalog with owner, source,
  applicability triggers, and executable proof references.
- [x] Add one repository-wide `AGENTS.md` applicability/preservation rule
  without copying subsystem contract contents into the root contract.
- [x] Extend full task templates and task-format policy with prospective
  `contract_schema`, `contracts`, and justified-empty review metadata.
- [x] Add a required method engine-integration matrix covering least-structured
  input, compatible sources, runtime/config/UI bindings, publication policy,
  and end-to-end tests or named follow-ups.
- [x] Extend the method and task authoring/review skills through their canonical
  docs and synchronized mirrors.
- [x] Extend strict task validation to resolve contract IDs, grandfather
  byte-identical pre-policy tasks without backfilling history, and reject
  direct done/archive insertion or consumed-snapshot replay.
- [x] Inventory every existing engine-integrated method against the catalog and
  create one bounded backlog refactor task per confirmed violation.
- [x] Keep method refactor tasks dependency-correct, contract-enrolled, and
  explicit about METHOD + runtime/config + UI + ECS source coverage.

## Tests

- [x] Add tooling regressions for valid contract declarations, unknown IDs,
  missing prospective enrollment, empty declarations without justification,
  byte-identical legacy grandfathering, changed/moved legacy tasks, direct
  done/archive insertion, and consumed-snapshot replay.
- [x] Validate all task IDs, dependencies, contract references, task policy,
  workflow evidence, skill mirrors, documentation links, and ARA structure.
- [x] Confirm the method integration inventory against source, runtime/config
  bindings, UI registration, and existing integration tests rather than task
  titles alone.

## Docs

- [x] Update the canonical architecture index and method API contract.
- [x] Update task format, method workflow/review, repository review checklist,
  and session onboarding guidance.
- [x] Update task templates, process/method backlog indexes, and generated
  `tasks/SESSION-BRIEF.md`.
- [x] Re-run `python3 tools/agents/sync_skills.py --write` after canonical
  `docs/agent/*` edits.

## Acceptance criteria

- [x] A task author can find applicable contracts from stable catalog IDs
  without searching retired tasks or copying policy text.
- [x] Every new or materially changed task in any lifecycle declares known
  contracts or a non-empty justified-empty review; strict validation rejects
  omissions.
- [x] A consuming task cannot treat narrower task wording as an override of a
  canonical contract without updating that contract through review.
- [x] Method work records one end-to-end integration matrix and names owners for
  any deferred runtime/config/UI/publication/test surface.
- [x] Every confirmed existing method-integration violation has a unique,
  bounded, unambiguous backlog task with appropriate dependencies.
- [x] No method or engine behavior changes are mixed into this process slice.
- [x] The final high-risk evidence report is current and an independent review
  accepts the exact final revision/content digest.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tests/regression/tooling/Test.ValidateTasks.py
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/generate_session_brief.py --check
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/agents/check_ara_claims.py --root . --strict
git diff --check
```

## Forbidden changes

- No engine C++ source, public module surface, build graph, dependency, method
  implementation, benchmark result, runtime wiring, or UI behavior changes.
- No contract whose sole authority is a task, skill, generated file, or code
  comment.
- No task title/domain-name inference presented as evidence of actual method
  integration behavior.
- No weakening or warning-mode conversion of an existing strict gate.
- No broad semantic lint that rejects legitimate provenance-only domain uses.
