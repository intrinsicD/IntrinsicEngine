# Bugs Backlog

Reproducible correctness bugs, flaky tests, and test-harness defects. Open
items live in [`index.md`](index.md); completed/closed items remain in the
"Verified / Closed" section there for reference.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Convergence

- This category normally corresponds to **Theme G — Active bugs** in the
  convergence map. `BUG-158..160` are assigned to the temporary Theme J
  product gate because they directly block the import golden workflow.
- Bug fixes that touch multiple layers must respect the same dependency
  anchors as feature work and should still ship as small, scoped patches per
  [`docs/agent/review.md`](../../../docs/agent/review.md).
- Promote a bug to a structured task file (using
  [`tasks/templates/bug-task.md`](../../templates/bug-task.md)) when it
  warrants its own commit boundary or verification.
