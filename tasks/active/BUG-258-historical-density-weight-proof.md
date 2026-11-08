---
id: BUG-258
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: historical evidence citation correction; structural checks
contract_schema: 1
contracts: []
contract_review: historical evidence path repair does not change source, scientific claim, workflow or method contracts
---
# BUG-258 — Historical density-weight proof cites a retired test path

## Goal
Repair C88's existing broken test citation without changing the historical scientific claim.
Discovered by the final ARCH-023 strict check on a path already absent on pre-task main
`dbe5312f32e66a69407cf37c39df3575250ead22`. The operator authorized the historical citation correction on 2026-11-08.

## Acceptance criteria
- [ ] Bind the retired test citation to its verified historical source, without changing C88's statement or status.
- [ ] Strict ARA claim validation passes; no checker exception is added.

## Verification
```bash
git cat-file -e 36a7b5062d9be3239d667efd9e41c4d4028ba249:tests/contract/runtime/Test.DensityWeightOperations.cpp
python3 tools/agents/check_ara_claims.py --root . --strict
```

## Evidence
On 2026-11-08 the strict validator reported one error: C88 cites
`tests/contract/runtime/Test.DensityWeightOperations.cpp`, which does not exist.
The same citation is present in pre-task main. The exact historical object above exists;
the commit already binds C88's other historical evidence. Current plugin tests are not
substituted for the historical scientific proof. The discovery is recorded in the ARCH-023 PR.
