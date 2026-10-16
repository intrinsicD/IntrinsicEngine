---
id: BUG-243
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive gate triage; reproduction is the strict link-check output
contract_schema: 1
contracts: []
contract_review: Existing documentation-checker input filtering and source-link parsing; no subsystem or task-schema contract changes are proposed.
---
# BUG-243 — Local strict doc-link check scans temporary review evidence

## Goal
- Restore a meaningful strict local documentation-link check without editing or deleting evidence owned by the active METHOD-077 task.

## Context
- Required tracking under AGENTS.md §10; found during the operator-directed GLB import fix on 2026-10-13.
- `python3 tools/docs/check_doc_links.py --root . --strict` exits 1 on source links ending in `:592`, `:317`, etc. in `ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/reviews/`. These files were not changed by the import work.
- Full output: `tasks/evidence/BUG-243/doc-links-strict.log`. The checker scans local temporary evidence and treats the source-line suffix as part of the filename. This is a local gate failure; no causal claim about the red hosted CI runs is made.

## Acceptance criteria
- [ ] Define and test how temporary evidence and supported source-line links participate in the documentation gate.
- [x] The strict check passes on maintained documentation without weakening missing-target checks or changing live research evidence.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Status (2026-10-16)
- `000eedca8` (BUG-241) restored the strict check by storing verbatim review transcripts as `.txt`;
  the storage rule is in `docs/agent/ara-evidence-policy.md`. Independent review: criterion 2 MET,
  criterion 1 PARTIAL — the rule is defined, but no checker test exists, `:line` links are still read as
  literal file names, and untracked local `*.md` evidence is still scanned. Open decision: add a
  checker test and/or `:line` support, or narrow criterion 1 to the storage rule.
