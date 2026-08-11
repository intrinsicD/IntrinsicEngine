---
id: PROC-033
theme: H
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-proc033"
branch: "codex/proc-033-source-documentation"
worktree: "/tmp/intrinsic-proc033.esP0pr"
claimed_at: "2026-08-11T10:56:13Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation]
contract_review: "PROC-033 establishes repo.source-documentation through one canonical policy, catalog routing, an executable audit, and focused regression proof."
---
# PROC-033 — Source documentation contract and audit skill

## Status

- Completed and retired on 2026-08-11 at `Operational` maturity. The canonical
  policy, reusable audit skill, focused regression suite, and CI routing are in
  place; implementation commit `07c3f64b` reached `main` through merge commit
  `2f7fe97b`.
- The human reviewer accepted the merged implementation and explicitly
  authorized the deterministic retirement bookkeeping on 2026-08-11. The final
  report, handoff, and review record bind that reviewed closure surface.
- The report-only inventory found 436 objective errors (408 missing
  module/header synopses and 28 explicitly non-current README sections) plus
  review-only comment/organization hotspots. Cleanup remains bounded follow-up
  work rather than part of this policy task.
- Completion commit: this retirement commit.

## Goal

- Establish one repository-wide contract for concise, current-state source
  documentation and package it as a reusable skill that finds objective
  violations and focused human-review hotspots without encouraging boilerplate.

## Non-goals

- No bulk rewrite of existing C++ comments, module interfaces, headers, or
  README files in this task.
- No engine behavior, public API, module ownership, build, or formatting change.
- No repository-wide CI failure on pre-existing documentation debt.
- No attempt to judge comment necessity or prose quality mechanically.

## Context

- Owner: the agent-workflow documentation and tooling surface. The policy
  governs project-owned C++ sources and README files across all engine
  layers without changing their dependency graph.
- Existing source READMEs have accumulated task history and planned-state prose,
  while declaration comments often duplicate names. This makes current
  ownership and entry points harder to discover.
- A source synopsis is required for project-owned `.cppm` and header files, but
  comments on declarations are exceptional: they exist only when correctness,
  ownership, lifetime, ordering, numerical, or other non-obvious context cannot
  be expressed clearly in code.
- Right-sizing: use one canonical policy, one standard-library audit script, and
  one thin skill workflow. Do not introduce a generic lint framework, config
  language, baseline database, or auto-rewriter. Reconsider shared lint
  infrastructure only after multiple independent rule families need it.

## Required changes

- [x] Add the canonical source-documentation policy and summarize it in the
      repository contract, task workflow, docs-sync policy, and review checklist.
- [x] Register `repo.source-documentation` in the contract catalog with the
      policy and executable regression proof as its authority chain.
- [x] Create `intrinsicengine-source-documentation` with the repository skill
      initializer, a synchronized policy reference, and a concise audit workflow.
- [x] Implement a deterministic audit that separates objective errors from
      human-review findings and supports report-only repository inventories.
- [x] Add focused regression coverage for source synopses, current-state README
      rules, historical comments, stable output, and report-only operation.
- [x] Route the new policy and skill through the repository indexes and generated
      skill synchronization map.

## Tests

- [x] The audit regression suite passes from a clean temporary fixture.
- [x] Skill metadata and synchronized references validate.
- [x] Strict task, documentation-sync, link, root-hygiene, and touched-scope
      structural checks pass.
- [x] A full repository audit completes in report-only mode and produces a
      deterministic migration inventory without modifying source files.

## Docs

- [x] Add `docs/agent/source-documentation-policy.md` as canonical current-state
      guidance and link it from the relevant indexes and agent procedures.
- [x] Regenerate skill references and `tasks/SESSION-BRIEF.md`.
- [x] Keep cleanup history in task/evidence records rather than README files.

## Acceptance criteria

- [x] Agents have one discoverable rule for file synopses, declaration comments,
      implementation explanations, and README content.
- [x] The reusable skill can audit the whole repository or selected paths and
      clearly distinguishes enforceable errors from judgment-based review items.
- [x] Existing debt is visible as a migration inventory but is not silently
      auto-fixed, suppressed, or promoted into a repository-wide blocking gate.
- [x] The skill does not reward repetitive comments or manually maintained API
      inventories that make code less self-documenting.
- [x] High-risk workflow evidence and an independent fixed-surface review are
      recorded before retirement.

## Verification

```bash
python3 tests/regression/tooling/Test.SourceDocumentationAudit.py
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py --root . --summary --no-fail
python3 /home/alex/.codex/skills/.system/skill-creator/scripts/quick_validate.py tools/agents/skills/intrinsicengine-source-documentation
python3 tools/agents/sync_skills.py --check
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --head-ref HEAD --preset ci-fast --preset-build-dir build/ci-fast --build-dir build/ci-fast --print
```

The explicit 29-file touched-scope plan selected the structural route and no
C++ setup. Its complete command bundle passed on 2026-08-11, including the new
9-case audit regression, affected workflow-policy regressions, task/state
checks, documentation links, skill sync, and session-brief freshness. Strict
task, docs-sync, link, root-hygiene, and test-layout checks also passed. The
report-only whole-tree audit scanned 1,319 files and recorded 436 objective
errors plus 3,835 review findings without changing source files. The human
reviewer accepted the merged implementation and explicitly authorized the
retirement-only task/index/evidence updates on 2026-08-11.

## Forbidden changes

- Bulk-generating file or declaration comments merely to satisfy the audit.
- Encoding subjective comment necessity as an automatic blocking heuristic.
- Adding feature histories, completed-task narratives, or future plans to
  README files.
- Editing engine behavior or cleaning existing documentation debt beyond the
  policy/skill examples needed to prove this task.

## Right-sizing

- Pressure point: a documentation auditor could grow into a configurable lint
  platform with parsers, baselines, and auto-fixes.
- Smallest sufficient shape: a plain Python scanner with fixed repository rules,
  a human-readable/JSON report, and a skill that explains how to act on it.
- Blast radius: agent policy, task policy, docs indexes, one skill, and its
  regression test; no C++ or runtime surface changes.
- Reintroduction trigger: add shared lint abstractions only when a second
  independently owned validator demonstrably needs the same machinery.
