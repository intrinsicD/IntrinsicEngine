---
id: BUG-170
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: >-
  Interactive documentation reconciliation of an already-resolved historical
  finding. Original sealed evidence remains unchanged in its Git revision;
  no new implementation or research claim is made.
owner: Codex
branch: codex/merge-all-branches-2026-09-05
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at:
contract_schema: 1
contracts: []
contract_review: >-
  Task identity and historical cross-reference repair only. No task schema,
  workflow policy, source surface, or reusable subsystem contract changes.
---
# BUG-170 — Preserve the historical root-hygiene closure without an ID collision

## Goal

Preserve the ADR-0028 branch's resolved root-hygiene finding while retaining
`BUG-163` as the canonical sculpt/curvature task already on main.

## Context

The ADR-0028 branch independently used `BUG-163` for strict root hygiene
rejecting the undocumented `src_new/` experiment. The owner removed that tree
in `17d9d29a` (already on main), and the branch retired its finding on
2026-09-02. Main subsequently used the same ID for the unrelated
[sculpt/curvature rendering task](BUG-163-sculpt-curvature-feature-patch-rendering.md).

The 2026-09-05 consolidation retains that existing canonical task unchanged
and assigns this historical root-hygiene finding `BUG-170`. The original
[task](https://github.com/intrinsicD/IntrinsicEngine/blob/e4edd20fc3ac30bada55c8ec48b6bef65e10615a/tasks/done/BUG-163-root-hygiene-strict-rejects-src-new.md),
[report](https://github.com/intrinsicD/IntrinsicEngine/blob/e4edd20fc3ac30bada55c8ec48b6bef65e10615a/tasks/evidence/BUG-163/report.yaml),
[seal](https://github.com/intrinsicD/IntrinsicEngine/blob/e4edd20fc3ac30bada55c8ec48b6bef65e10615a/tasks/evidence/BUG-163/seal.yaml),
and command receipts remain byte-for-byte in merged history at
`e4edd20fc3ac30bada55c8ec48b6bef65e10615a`. The seal continues to describe
revision `01988874c2ddbbc145c4229eb72819e1533828e0` and its original paths.
They are not relabeled as new evidence or installed beneath the unrelated
canonical `tasks/evidence/BUG-163/` namespace.

## Acceptance criteria

- [x] Existing sculpt/curvature `BUG-163` task and evidence remain unchanged.
- [x] Original root-hygiene task, report, seal, and receipts remain available
      at the incoming merge parent; this note identifies their exact revision.
- [x] Strict root hygiene and task/evidence validators pass on the combined tree.

## Verification

```bash
python3 tools/repo/check_root_hygiene.py --root . --strict
python3 tests/regression/tooling/Test.RootHygiene.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
git cat-file -e e4edd20fc3ac30bada55c8ec48b6bef65e10615a:tasks/evidence/BUG-163/seal.yaml
```

## Completion

- Completed: 2026-09-05.
- PR: [#1034](https://github.com/intrinsicD/IntrinsicEngine/pull/1034).
- Commit: incoming parent `e4edd20fc3ac30bada55c8ec48b6bef65e10615a`;
  identity reconciliation recorded by this merge.

Completed 2026-09-05 in the ADR-0028 consolidation merge (incoming PR #1034,
parent `e4edd20fc3ac30bada55c8ec48b6bef65e10615a`). This closes the identity
reconciliation; the original root-hygiene defect was fixed by `17d9d29a`.
