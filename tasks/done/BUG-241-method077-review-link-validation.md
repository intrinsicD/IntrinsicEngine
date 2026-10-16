---
id: BUG-241
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: pre-existing documentation validation failure observed during METHOD-078; no engine changes.
contract_schema: 1
contracts: []
contract_review: Documentation link validation and historical review records only.
---
# BUG-241 — METHOD-077 review links fail strict documentation validation

## Goal
Resolve the strict doc-link failure already present at `bbdf1af79`, without rewriting the
substance of frozen review verdicts. Observed 2026-10-11 during METHOD-078 verification.

## Evidence
`python3 tools/docs/check_doc_links.py --root . --strict` rejects file links with `:line`
suffixes in METHOD-077's `reviews/codex-d2.md`, `codex-d3.md`, and `codex-r2.md` under
`ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/`.
Example: the target `Geometry.Registration.CoherentPointDrift.cpp:592` is interpreted as a
literal filename. These files are unchanged by METHOD-078; their baseline bytes are available
with `git show bbdf1af79:<path>`. No PR exists for the current local diagnosis branch.

## Acceptance criteria
- [ ] Diagnose the intended repository representation of source-line links in archived review records.
- [ ] Make strict doc-link validation pass without suppressing unrelated broken links or altering verdicts.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
