# Report-link validation repair

The first `report-doc-links` receipt ran while REVIEW-003 still lived under
`tasks/active/`, but the newly drafted final reports already linked its eventual
`tasks/done/` path. The checker correctly rejected those two not-yet-existing
targets. The task was then retired as part of the same report-only candidate,
and the required replacement receipt under `commands/` validates the final
paths. No source or architecture conclusion changed.

The first `final-task-policy` receipt then caught the required done-task
completion-commit field before the candidate was committed. The task now uses
the repository's accepted “this report and retirement commit” binding, and the
required replacement receipt validates the completed task.

The first `candidate-structural-gates` receipt caught that editing prose inside
seven grandfathered open task files would prospectively enroll those tasks in
the current workflow/contract schemas. Their historical task bodies were
restored byte-for-byte; only the generated/current backlog indexes describe the
now-satisfied dependency. The replacement candidate receipt validates that
bounded docs/task-state update.
