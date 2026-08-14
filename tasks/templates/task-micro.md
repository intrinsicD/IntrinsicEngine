---
id: <TASK-ID>
theme: <theme letter from tasks/backlog/README.md, or `none`>
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: <why a full completion report is disproportionate>
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: <why no catalog contract applies>
---
# <TASK-ID> — <Task title>

## Goal
- 

## Acceptance criteria
- [ ] <acceptance criterion>

## Verification
```bash
# Add concrete commands for this task.
```

<!-- Optional sections when they earn their lines: ## Context, ## Slice plan,
     ## Log (dated decisions/defaults chosen during interactive work). -->

<!--
Micro template: for single-slice mechanical work only (small fixes, doc/link
sweeps, config toggles, test-only additions). `template: micro` in the
front-matter relaxes validate_tasks.py to these three sections. NOT allowed
for work that changes dependency boundaries, module ownership, public module
surfaces, methods/benchmarks, or anything with an ambiguous maturity
stop-state — those use tasks/templates/task.md (or the method/bug/review
variants) with the full nine sections. Retirement rules are unchanged:
checkboxes closed, completion date, commit/PR reference, retirement-log entry.
-->
