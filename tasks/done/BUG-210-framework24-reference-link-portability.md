---
id: BUG-210
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive documentation repair with retained CI failure and pinned target verification
contract_schema: 1
contracts: []
contract_review: Documentation link portability only; no engine, dependency, architecture or verification-policy change.
---
# BUG-210 — Make optional Framework24 source references portable

## Goal

Fix ten pre-existing links in the normal-estimation and spatial-consumer docs
that resolve only when `experimental/framework24/` is present locally. The
strict documentation job for [PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044)
failed on a clean GitHub checkout, although the local check passed. Both docs
are unchanged from the PR's existing parent branch before this repair.

## Acceptance criteria

- [x] Replace the optional-checkout links with immutable upstream source URLs;
  verify every target against the reference repository's GitHub tree.
- [x] Run the unchanged strict link checker against a tracked-files-only export
  without the optional checkout, and retain its output.
- [x] Retain the CI failure and PR reference without importing reference code or
  weakening the checker.

## Verification

```bash
python3 tools/docs/check_doc_links.py --root <tracked-files-only-export> --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

The nine distinct source blobs are pinned to Framework24 revision
`81c54ad4294280fc034d39e46eafc1a29d598b81`.
[Target verification](../evidence/BUG-210/verified-framework24-targets.json) and
the [CI failure](../evidence/BUG-210/ci-doc-links-failure.log) preserve the evidence.


## Completion

Retired 2026-09-23 at the documentation-maintenance endpoint.
PR/commit: [PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044),
the enclosing portable-reference repair commit. The unchanged strict checker
passes in a tracked-files-only export with no optional Framework24 checkout;
[output](../evidence/BUG-210/tracked-export-doc-check.log) is retained. All nine
upstream blobs were verified at the pinned revision. No code was imported and
no check was skipped or weakened. Remote CI reruns with the repair.
