---
id: BUG-257
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: documentation link correction; structural checks
contract_schema: 1
contracts: []
contract_review: historical review link repair does not change source, workflow or method contracts
---
# BUG-257 — Main docs CI links to a retired neighborhood codec

## Goal
Repair the historical source reference in docs/reviews/2026-10-16-src-runtime-audit.md without
changing its findings. Discovered during ARCH-023 startup on unchanged main dbe5312f3.

## Acceptance criteria
- [ ] The historical reference resolves to the relevant source revision or current owner.
- [ ] Strict doc links pass; no checker exception is added.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root . --strict
```

## Evidence
Main [ci-docs run 42406828027](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/42406828027)
fails at markdown links. Reproduced 2026-11-07: review document links to
`src/runtime/Config/internal/Runtime.PointNeighborhoodConfigCodecs.cpp`, which no longer exists.
Temporary output: `tasks/evidence/ARCH-023/doc-links-baseline.log`. Include this task in ARCH-023's PR.
