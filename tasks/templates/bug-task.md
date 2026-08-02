---
id: <BUG-ID>
theme: <theme letter from tasks/backlog/README.md, or `none`>
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
contract_review: <why no catalog contract applies; remove when contracts are declared>
---
# <BUG-ID> — <Bug title>

## Goal
- 

## Non-goals
- 

## Context
- Symptom:
- Expected behavior:
- Impact:

## Required changes
- [ ] <fix or diagnostic change>

## Tests
- [ ] Add regression coverage under `tests/regression/` when appropriate.

## Docs
- [ ] Update relevant architecture/migration/task docs if behavior or policy changes.

## Acceptance criteria
- [ ] Repro is documented and reliably covered by automated test(s).
- [ ] Fix does not introduce layering violations.

## Verification
```bash
# Add concrete bug reproduction and validation commands.
```

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
