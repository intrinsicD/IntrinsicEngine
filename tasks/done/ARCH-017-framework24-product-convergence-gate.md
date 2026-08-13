---
id: ARCH-017
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-root"
branch: "agent/framework24-product-convergence-goal"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-13T10:57:43Z"
contract_schema: 1
contracts: [repo.task-contract-discovery]
contract_review: "This task changes the repository mission and task-selection policy so product convergence against Framework24 becomes the immediate cross-layer gate. It introduces no engine API, data-domain, publication, backend, or config/UI contract; implementation gaps remain in separately owned tasks."
---
# ARCH-017 — Make Framework24 product convergence the immediate engine gate

## Status

- Completed and retired on 2026-08-13. IntrinsicEngine now treats a
  demonstrably better Framework24 replacement as its immediate P0 product
  objective, with six evidence-gated golden workflows and a source-backed
  registered-feature inventory. `REVIEW-004` owns the eventual product
  verdict; this policy retirement makes no parity or performance claim.
- Completion commit: this implementation/retirement commit.

## Goal

- Make “a demonstrably better Framework24 replacement” an authoritative,
  measurable repository objective, and pause new research expansion until a
  one-shot convergence audit accepts the product workflows.

## Non-goals

- No engine, renderer, geometry-method, importer, benchmark, or UI behavior
  changes in this policy slice.
- No claim that current IntrinsicEngine performance or feature parity already
  matches Framework24.
- No deletion, rollback, or architectural weakening of the C++23 module,
  Vulkan/RHI, layering, correctness, or evidence foundations.
- No speculative backend selector: backend names remain available only when a
  real implementation and its evidence exist.

## Context

- Owner: repository architecture and work-selection policy.
- The current contract describes a modular research engine but does not make
  Framework24 workflow replacement a completion condition. Agents therefore
  optimized the explicit local gates—layering, task closure, CPU contracts,
  and research slices—without a shared end-to-end product scorecard.
- Existing open tasks already own several measured product gaps. This slice
  must route them through one product gate instead of duplicating their
  implementation scopes.
- `REVIEW-004` will be the one-shot final audit. It may add narrowly scoped
  blockers when a golden workflow lacks accepted evidence; remediation tasks
  must never depend on `REVIEW-004`.

## Control surfaces

- Config: unchanged; the convergence contract requires future tunable behavior
  to remain available through the existing validated config lane.
- UI: unchanged in this slice; UI closure is measured by golden workflows.
- Agent/CLI: generic task selection must prefer Theme J while `REVIEW-004` is
  open and must not select new research expansion from Theme I.

## Backends

- Backend axis: policy only. Every method keeps a CPU reference as canonical
  truth; optimized/parallel CPU and Vulkan tokens require real implementations,
  requested/actual/fallback diagnostics, parity evidence, and measured value.

## Required changes

- [x] Add the Framework24 replacement objective and convergence stop rule to
      `AGENTS.md` and the expanded agent contract.
- [x] Add an authoritative product scorecard and source-backed registered-
      feature inventory with status semantics, golden workflows, evidence
      rules, and comparable-or-better acceptance gates.
- [x] Make Framework24 convergence Theme J/P0 in the backlog and generic task
      picker; explicitly pause Theme I research expansion until `REVIEW-004`
      retires.
- [x] Record the user-approved product decision in the ARA architecture ledger.
- [x] Open `REVIEW-004` as the one-shot final convergence audit and reuse
      existing product-gap tasks as its dependencies.
- [x] Open only the missing, evidence-backed import-performance and golden-
      workflow benchmark tasks needed to make the first critical path
      executable.
- [x] Regenerate the task session brief and synchronized skill mirrors.

## Tests

- [x] Pass strict task, task-state, ARA, documentation-link, and skill-sync
      validation.
- [x] Pass the task-contract validator regression suite affected by the
      work-selection policy change.

## Docs

- [x] Add `docs/product/` as the durable home for the convergence contract and
      link it from `docs/index.md`.
- [x] Keep scorecard rows evidence-status-based: an open gate is not a negative
      performance claim, and a supported capability cites an existing ARA claim
      or accepted completion receipt.

## Acceptance criteria

- [x] A new agent cannot reasonably select speculative research while the
      Framework24 convergence audit is open.
- [x] A contributor can identify the exact workflows, metrics, proof level,
      current gate owner, and final stop condition from one scorecard.
- [x] Known product gaps have one owner each; this task does not duplicate
      implementation scopes.
- [x] IntrinsicEngine’s modular Vulkan architecture and reliability gates are
      explicit non-regression constraints of convergence.
- [x] Independent fixed-surface review accepts the exact revision-bound policy
      and task-selection surface.

## Verification

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/sync_skills.py --check
python3 tests/regression/tooling/Test.ValidateTasks.py
python3 tools/agents/generate_session_brief.py --check
```

## Forbidden changes

- No production C++ or shader edits in this task.
- No unsupported parity, performance, visual-quality, or cross-device claim.
- No new service, registry, framework, release-state type, or CI gate.
- No weakening of method, benchmark, sanitizer, Vulkan, documentation, or
  task-evidence requirements to make the scorecard appear green.
