---
id: PROC-027
theme: H
depends_on: []
---
# PROC-027 — Audit the validator/tool fleet for rent-paying gates

## Status
- In progress on 2026-07-17; owner: Codex; branch: `main` (local).
- Next verification: finish the source-complete inventory and prove every
  retire verdict has no remaining CI, touched-scope, documentation, or
  open-task caller.

## Goal
- Inventory the Python tools under `tools/` (~56 scripts), determine which
  validators have actually produced findings recently, and retire or downgrade
  gates that never fire, so the process apparatus follows the same
  justified-complexity rule as the code
  (`intrinsicengine-right-sizing` keep-list).

## Non-goals
- No weakening of validators with recent findings or load-bearing CI roles
  (layering, test layout, docs-sync, doc links, task policy, state links,
  skill mirrors — all fired or gated real changes in 2026-07).
- No rewrite of validator internals; this is keep/warn/retire triage only.

## Context
- 2026-07-14 process review: the apparatus grew organically; each element
  needs an evidencing incident the way discipline skills cite retired bugs.
- Evidence sources: `ci-docs.yml`/workflow logs, git history of findings,
  grep for validator names across retired tasks.
- Owner: `tools/` fleet, CI workflow wiring, `docs/agent/*` references.
- Primary evidence window: 2026-06-09 through 2026-07-17, with history back to
  a tool's introduction when the primary window has no finding.
- Inventory scope: every tracked Python file under `tools/`, including
  AgentKit implementation/template modules and the research-ideation skill
  validator. Non-Python executable wrappers are listed separately so no
  validator wiring is hidden by the Python-focused goal.
- Evidence distinguishes real repository findings from synthetic regression
  fixtures, false positives/tool defects, and load-bearing producer roles.

## Slice plan
- **Slice A.** Record the source-complete inventory, evidence model, CI wiring,
  last finding or load-bearing role, and keep/warn/retire verdict for every
  in-scope tool.
- **Slice B.** Apply only supported retire/warn verdicts, remove every caller
  and stale reference, then run the strict structural bundle and retire the
  task.

## Right-sizing
- Element: `tools/repo/check_pr_contract.py` is a strict CI checker whose CI
  mode only validates static pull-request-template headings and whose local
  mode prints advisory prompts while returning success.
- Simpler alternative: retire the checker and its CI/touched-scope/documented
  invocations; keep the pull-request template itself as the human review
  surface.
- Blast radius: `ci-docs`, touched-scope planning, `AGENTS.md`, repository
  tooling docs, and verification blocks in open CI tasks. No engine source or
  layer edge changes.
- Reintroduction trigger: add a fail-closed check only if a future hosted
  workflow can validate submitted pull-request body content or a recorded
  template-drift incident demonstrates a unique machine-checkable invariant.

## Required changes
- [ ] Produce a source-complete inventory table (tool, purpose, CI wiring,
  last finding/load-bearing evidence, verdict keep/warn/retire) under
  `docs/reports/`.
- [ ] Apply the verdicts: remove retired tools + their CI wiring and doc
  references; downgrade warn-tier tools to warning mode where CI runs them
  strict without evidence.

## Tests
- [ ] Structural checks still pass strict after removals
  (`check_task_policy`, `check_doc_links`, `check_docs_sync`,
  `check_layering`, `check_test_layout`, skill-mirror check).

## Docs
- [ ] `tools/*/README.md` entries updated for every removed/downgraded tool;
  docs-sync rules updated if a rule references a removed tool.

## Acceptance criteria
- [ ] Every tool under `tools/` has a recorded verdict with evidence.
- [ ] No CI job invokes a tool classified retire.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root .
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Removing a validator that produced a finding within the evidence window.
- Engine code changes.
