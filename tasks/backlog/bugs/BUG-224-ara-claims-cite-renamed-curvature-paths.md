---
id: BUG-224
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive recording of a pre-existing structural-check failure found during GEOM-081; no claim changes yet.
contract_schema: 1
contracts: []
contract_review: ARA proof-path maintenance under the existing ARA workflow; no engine or integration contract.
---
# BUG-224 — ARA claims cite renamed curvature paths

## Goal
- Make `python3 tools/agents/check_ara_claims.py --root . --strict` pass again without weakening the check.

## Context
Found on 2026-09-26 while verifying GEOM-081; the failure predates that work
(base `59abb4ef4`). Commit `c9011aff4` (2026-09-25) renamed the curvature
extrema/segmentation modules to scalar-field names, e.g.
`Geometry.HalfedgeMesh.CurvatureSegmentation.*` → `Geometry.HalfedgeMesh.Segmentation.*`,
`Test.CurvatureExtrema.cpp` → `Test.ScalarfieldExtrema.cpp` and
`Test.CurvatureSegmentation.cpp` → `Test.Segmentation.cpp`. Seven proof paths in
claims C38, C44, C45, C58 and C61 still cite the old names. Some old files
(`...CurvatureSegmentation.Features/Patches.*`) may have been split or merged, so
each path needs checking against the renamed content, not a blind substitution.

## Acceptance criteria
- [ ] Each stale proof path points at the file that now holds the cited evidence, or the claim records why the evidence moved.
- [ ] `check_ara_claims.py --strict` reports no errors.

## Verification
```bash
python3 tools/agents/check_ara_claims.py --root . --strict
```
