---
id: BUG-119
theme: none
depends_on: []
---
# BUG-119 — Test.CheckTaskStateLinks asserts an inline SHA expression the docs-sync step no longer uses

## Goal
- Restore `tests/regression/tooling/Test.CheckTaskStateLinks.py` to a green, meaningful assertion
  about how `ci-docs.yml` resolves the docs-sync diff range.

## Non-goals
- Changing how `ci-docs.yml` resolves base/head SHAs. The workflow is correct; the test is stale.
- Weakening the assertion to a substring that would pass regardless of routing correctness.

## Context
- Observed while adding the ARA claim-ledger validator. The failure reproduces on a clean checkout
  with no local modifications, so it is pre-existing and unrelated to that change.
- `Test.CheckTaskStateLinks.test_ci_docs_enforces_task_state_and_docs_sync_strictly` asserts the
  docs-sync step's `run:` body contains the literal `github.event.pull_request.base.sha`.
- The step now passes those values through step-level `env:` (`PR_BASE_SHA`, `PR_HEAD_SHA`,
  `MERGE_GROUP_BASE_SHA`, `MERGE_GROUP_HEAD_SHA`) and the `run:` body references the shell
  variables. The `github.event.*` expressions still exist, in `env:` rather than `run:`.
- The routing itself is intact and fails closed: each event branch verifies both SHAs are non-empty
  before use, and `check_docs_sync.py` is still invoked with `--diff-mode ... --strict`.

Reproduction on a clean tree:

```
$ git stash push -u && python3 tests/regression/tooling/Test.CheckTaskStateLinks.py
AssertionError: 'github.event.pull_request.base.sha' not found in 'base_ref=origin/main ...'
Ran 4 tests — FAILED (failures=1)
```

## Required changes
- [ ] Read the docs-sync step's `env:` mapping as well as its `run:` body in the test.
- [ ] Assert the property that actually matters: for `pull_request` and `merge_group`, the step
      binds base/head from the event payload, guards both for emptiness, and invokes
      `check_docs_sync.py` with `--diff-mode` and `--strict`.

## Tests
- [ ] `python3 tests/regression/tooling/Test.CheckTaskStateLinks.py` passes on a clean checkout.
- [ ] The updated assertion fails if the `env:` binding for either event is removed.

## Docs
- [ ] No doc change expected; note the correction in the test docstring if the intent is unclear.

## Acceptance criteria
- [ ] The test is green and still fails when the workflow's SHA routing is genuinely broken.
- [ ] No change to `ci-docs.yml` behavior.

## Verification
```bash
python3 tests/regression/tooling/Test.CheckTaskStateLinks.py
python3 tools/docs/check_docs_sync.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
