---
id: BUG-177
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive record of an unrelated local structural-gate failure observed during METHOD-044; no fix attempted."
contract_schema: 1
contracts: []
contract_review: "Triage of existing root-hygiene classification and local metadata ownership; no new reusable contract or engine API change is proposed by this note."
---
# BUG-177 — Root hygiene rejects local agent metadata

## Goal
- Determine the ownership/lifecycle of the untracked `.agents/` directory and reconcile it with repository root-hygiene policy without weakening the unknown-entry gate or deleting session metadata blindly.

## Evidence
- During METHOD-044 final checks, `python3 tools/repo/check_root_hygiene.py --root . --strict` exited 1 with `.agents/` as the sole unexpected entry. `git ls-files .agents` emitted nothing. The atlas task did not create or edit that directory and left it untouched.
- [Recorded observation](../../../ara/evidence/diagnostics/method044/root-hygiene-observation.json). Atlas-specific tests, task/ARA/manifest checks and documentation links pass; this is a separate local-workshop issue, not an atlas failure.

## Acceptance criteria
- [ ] Identify whether `.agents/` is expected session tooling state or an unintended local artifact, with a reproduction.
- [ ] Apply the appropriately scoped metadata-location/classification correction; any allowlist change must preserve rejection of arbitrary unknown entries and receive a regression test.
- [ ] Run the strict root gate successfully without deleting live or operator-owned data as a shortcut.

## Verification
```bash
python3 tools/repo/check_root_hygiene.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
