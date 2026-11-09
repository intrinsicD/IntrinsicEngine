---
id: BUG-260
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: CI failure tracking only; no implementation or gate change
contract_schema: 1
contracts: []
contract_review: Records an external bootstrap failure; no engine, workflow or dependency policy changes.
---
# BUG-260 — PR-fast stops at vcpkg tool download with HTTP 504

## Goal
Restore and verify PR-fast for ARCH-023 without weakening its checks.

## Acceptance criteria
- [ ] Determine whether the download failure is transient; if reproducible, diagnose the existing bootstrap path before changing it.
- [ ] A fresh PR-fast run completes its required build and tests successfully.

## Observed failure
[PR #1046](https://github.com/intrinsicD/IntrinsicEngine/pull/1046), head
`1de9ea41563a564d7df4bfc17f71271c44bfbb6a`:
[run 42471788869](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/42471788869/job/128704022875)
stopped in Bootstrap vcpkg on 2026-11-08. The log records four HTTP 504 responses while
downloading vcpkg-glibc, then bootstrap exit 1. Build and C++ tests did not run; subsequent
missing timing/artifact errors follow that early exit. This run cannot establish a method-code
regression or a successful final-head build. No retry has yet been observed, and no workflow,
bootstrap script or dependency baseline was changed for this record.

## Verification
```bash
gh run view 42471788869 --json jobs,conclusion
gh api repos/intrinsicD/IntrinsicEngine/actions/jobs/128704022875/logs
# After a separately initiated retry, inspect the new attempt's build/test verdict.
```
