---
id: PROC-027
theme: H
depends_on: []
---
# PROC-027 — Audit the validator/tool fleet for rent-paying gates

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

## Required changes
- [ ] Produce an inventory table (tool, purpose, CI wiring, last finding,
  verdict keep/warn/retire) under `docs/reports/`.
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
