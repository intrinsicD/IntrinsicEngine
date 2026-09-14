---
id: BUG-177
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive reproduction and repair of the local root classification; source diff and regression tests provide evidence."
contract_schema: 1
contracts: []
contract_review: "An exact optional directory is classified under the existing root-policy contract; unknown entries, required roots and failure behavior remain enforced. No engine API or layer contract changes."
---
# BUG-177 — Root hygiene rejects local agent metadata

## Goal
- Determine the ownership/lifecycle of the untracked `.agents/` directory and reconcile it with repository root-hygiene policy without weakening the unknown-entry gate or deleting session metadata blindly.

## Evidence
- During METHOD-044 final checks, `python3 tools/repo/check_root_hygiene.py --root . --strict` exited 1 with `.agents/` as the sole unexpected entry. `git ls-files .agents` emitted nothing. The atlas task did not create or edit that directory and left it untouched.
- [Recorded observation](../../../ara/evidence/diagnostics/method044/root-hygiene-observation.json). Atlas-specific tests, task/ARA/manifest checks and documentation links pass; this is a separate local-workshop issue, not an atlas failure.

## Acceptance criteria
- [x] Identify whether `.agents/` is expected session tooling state or an unintended local artifact, with a reproduction.
- [x] Apply the appropriately scoped metadata-location/classification correction; any allowlist change must preserve rejection of arbitrary unknown entries and receive a regression test.
- [x] Run the strict root gate successfully without deleting live or operator-owned data as a shortcut.

## Verification
```bash
python3 tools/repo/check_root_hygiene.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tests/regression/tooling/Test.RootHygiene.py
```

## Resolution — 2026-09-13

The current session exposes `.agents/` as a read-only local metadata directory.
It is empty, untracked and mode `0555`; no repository source lives there. The
original strict check reproduced with that directory as its only unexpected
entry. No contents, permissions or location were changed.

The canonical policy now classifies the exact optional directory `.agents/`
as local agent/tool state. It is not a required tracked root, a wildcard, a Git
ignore bypass or permission to delete metadata. The new regression uses the
canonical policy and both checker entrypoints: absence and directory pass,
while a file named `.agents` and a lookalike `.agents-extra/` still fail.
It failed before the policy edit, then all 13 root-hygiene tests passed along
with the actual strict repository gate. Existing tests still reject arbitrary
source directories, missing required roots and global-ignore bypasses.

Evidence is archived with RUNTIME-237 at
`build/analysis/runtime237-render-diagnostics-2026-09-13/`: metadata observation,
original gate/test failures and passing reruns. Independent Claude review and
the combined source verification are recorded in RUNTIME-237. The correction
is implemented, independently reviewed and verified on the combined source,
pending integration.
