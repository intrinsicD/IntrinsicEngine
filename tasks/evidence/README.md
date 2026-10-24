# Task evidence

This is temporary working storage. Follow [task retirement](../../docs/agent/task-format.md#retiring-a-task)
for extraction, review and removal; new raw evidence stays ignored. Existing
open-task files and shared inputs remain until their last consumer completes.

Enrolled non-trivial tasks collect completion evidence at:

```text
tasks/evidence/<TASK-ID>/
├── report.yaml
├── commands/
│   ├── <label>.json
│   ├── <label>.stdout.log
│   └── <label>.stderr.log
├── supersessions.jsonl    # optional; failed required receipt → later pass
├── handoff.jsonl          # high-risk and higher
├── reviews.jsonl          # high-risk and higher
└── experiment/            # claim-grade and protected
```

`report.yaml` schema version 1 contains:

- task/profile/status/generator identity and explicit claim eligibility;
- base/head/branch/worktree/dirty source identity, changed paths, file hashes,
  and one aggregate content digest;
- touched layers/modules, extension seam, and future-change plan;
- exact task acceptance criteria and dispositions;
- command receipt paths and exit evidence, with `superseded_by` on a failed
  receipt reconciled through `supersessions.jsonl` (see
  [Superseding a failed required receipt](../../docs/agent/workflow-evidence.md#superseding-a-failed-required-receipt));
- changed public contracts, diagnostics/previews, benchmark/parity evidence,
  docs synchronization, residual risks, and justified skipped checks;
- referenced artifact paths/hashes and self-review answers;
- profile-specific handoff/review/experiment roots.

Generate and validate these files with
`tools/agents/workflow_evidence.py`; do not hand-author a passing result.
Command and review labels are cooperative metadata, not authenticated
identities. Large outputs remain outside Git and are referenced by path and
SHA-256.

Untouched historical tasks are governed by the prospective migration in
`docs/agent/workflow-evidence.md` and are not backfilled.
