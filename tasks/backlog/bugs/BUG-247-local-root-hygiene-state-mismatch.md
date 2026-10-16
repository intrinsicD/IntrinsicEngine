---
id: BUG-247
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: environmental failure recorded during interactive HARDEN-091 verification; no implementation in this slice
contract_schema: 1
contracts: []
contract_review: Root ownership and local tool state do not change an engine API or integration contract.
---
# BUG-247 — Local root hygiene rejects existing tool state

## Goal
Track the environmental verification gap discovered while completing HARDEN-091,
without changing unrelated local directories or weakening the root policy.

## Acceptance criteria
- [ ] Resolve ownership of the existing `.aws/` and `.serena/` directories with the operator before changing policy or local state.
- [ ] The agreed local setup passes strict root hygiene and its existing regression tests without broad exclusions.

## Evidence and limits
On 2026-10-17 at `a13948cc5`, with the two intended formatter roots explicitly
registered, strict root hygiene reports only `.aws/` and `.serena/` as unexpected.
`Test.RootHygiene.py` passes 12 of 13 methods; its repository-root method fails
both checker subtests on those same directories. An isolated snapshot of all
maintained tracked/new files passes all 13 methods. This is a local setup issue;
no hosted-CI failure or engine behavior defect is inferred. Both directories are
preserved. Raw evidence: `tasks/evidence/BUG-247/root-hygiene.log`.

## Verification
```bash
python3 tools/repo/check_root_hygiene.py --root . --strict
python3 tests/regression/tooling/Test.RootHygiene.py
```
