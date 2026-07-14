---
id: <TASK-ID>
theme: <theme letter from tasks/backlog/README.md, or `none`>
depends_on: []
template: micro
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
