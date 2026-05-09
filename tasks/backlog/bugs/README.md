# Bugs Backlog

Reproducible correctness bugs, flaky tests, and test-harness defects. Open
items live in [`index.md`](index.md); completed/closed items remain in the
"Verified / Closed" section there for reference.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Convergence

- This category corresponds to **Theme G — Active bugs** in the convergence
  map.
- Bug fixes that touch multiple layers must respect the same dependency
  anchors as feature work and should still ship as small, scoped patches per
  [`docs/agent/review-checklist.md`](../../../docs/agent/review-checklist.md).
- Promote a bug to a structured task file (using
  [`tasks/templates/bug-task.md`](../../templates/bug-task.md)) when it
  warrants its own commit boundary or verification.
