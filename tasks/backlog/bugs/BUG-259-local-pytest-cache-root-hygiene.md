---
id: BUG-259
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: environmental root-hygiene failure recorded during ARCH-023 closure; local cache preserved
contract_schema: 1
contracts: []
contract_review: Local tool-state ownership does not change an engine API or integration contract; no policy change is proposed.
---
# BUG-259 — Local pytest cache fails repository-root hygiene

## Goal
Resolve the local `.pytest_cache/` root-policy mismatch without changing unrelated local state
or weakening the repository check. This is separate from ARCH-023's method implementation.

## Acceptance criteria
- [ ] Establish ownership and the intended location of the existing cache before changing it or the root policy.
- [ ] The agreed local setup passes strict root hygiene and all 13 existing regression methods.

## Evidence and limits
During ARCH-023 closure on 2026-11-08, `Test.RootHygiene.py` failed the two checker subtests
of its repository-root method: `.pytest_cache/` is the sole unexpected entry. The directory
and its empty node-ID cache have timestamps from 2026-10-07. Neither was changed or removed.
The other 12 methods passed. An isolated snapshot of maintained tree
`4ef92eb0f0241d28672dfea8afd1bf0b90f41e27`, before the closure narrative, passes all
13 methods without changing the policy; the main checkout remains locally blocked by the cache.
No hosted-CI failure or engine defect is inferred.
Raw failure and filesystem metadata remain in ignored `tasks/evidence/BUG-259/`, owned only
by this open task; ARCH-023's cleanup must preserve them. The PR references this local gap.

## Verification
```bash
python3 tools/repo/check_root_hygiene.py --root . --strict
python3 tests/regression/tooling/Test.RootHygiene.py
```
