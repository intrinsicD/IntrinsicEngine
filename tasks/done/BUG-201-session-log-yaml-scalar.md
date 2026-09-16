---
id: BUG-201
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive trace repair; the historical Git source reproduces the parse failure and direct YAML parsing verifies the correction.
contract_schema: 1
contracts: []
contract_review: Repair one prose scalar in a session trace; no engine, schema, research-claim or workflow contract changes.
---
# BUG-201 — Repair malformed session-log YAML scalar

## Goal
Restore parseable session bookkeeping without changing its recorded events.

## Context
Reading `ara/trace/pm_reasoning_log.yaml` at `6f23537b3` raises PyYAML
`ScannerError` at line 2726: an unquoted prose scalar contains a colon followed
by a space. This was introduced by a wording edit after the previous YAML check.

## Acceptance criteria
- [x] Replace the ambiguous punctuation without changing the event.
- [x] Parse all four trace files after their final text edits.

## Verification
```bash
python3 - <<'PYVERIFY'
from pathlib import Path
import yaml
for name in ["ara/trace/exploration_tree.yaml", "ara/trace/pm_reasoning_log.yaml",
             "ara/trace/sessions/2026-09-16_001.yaml", "ara/trace/sessions/session_index.yaml"]:
    yaml.safe_load(Path(name).read_text())
PYVERIFY
```

## Completion — 2026-09-16
Retired as a trace syntax repair. Commit reference: the enclosing curvature-admission commit.
Historical failing source is `6f23537b3:ara/trace/pm_reasoning_log.yaml`; final
trace files parse successfully. No engine maturity change or deferred work.
