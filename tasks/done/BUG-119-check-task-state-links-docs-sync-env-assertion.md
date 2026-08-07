---
id: BUG-119
theme: none
depends_on: []
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this repair realigns a CI-policy regression assertion with the docs-sync step's existing env-based SHA routing and changes no engine, geometry, method, publication, configuration, runtime, UI, or reusable task-workflow contract. `ci-docs.yml` behavior is unchanged."
---
# BUG-119 — Test.CheckTaskStateLinks asserts an inline SHA expression the docs-sync step no longer uses

## Status

- Completed and retired on 2026-08-07. The test now reads the docs-sync step's
  `env:` mapping and its `run:` body separately: it pins all five event-payload
  bindings by name and value, and asserts that each of `pull_request` and
  `merge_group` guards both SHAs for emptiness before assigning them and that
  `check_docs_sync.py` runs with `--diff-mode`, both refs, and `--strict`.
- Fixing that surfaced a second stale literal of the same class in the same
  test: `Validate structural CI policy regressions` had grown from one script
  to three, while the test still asserted equality against the single-script
  string. The step's script list is now parsed and pinned, and every listed
  script is required to exist on disk. Without this the task could not close
  green, so it was fixed here rather than deferred.
- Seven mutation probes confirm the assertions still bite: removing the
  `PR_BASE_SHA` or `MERGE_GROUP_BASE_SHA` binding, repointing `PR_HEAD_SHA` at
  the wrong payload field, weakening the `pull_request` emptiness guard,
  dropping `--diff-mode` or `--strict`, and removing a policy regression script
  each fail the test. `ci-docs.yml` was restored unchanged after every probe.
- No change to `ci-docs.yml` behavior.
- Completion commit: this retirement commit.

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
- [x] Read the docs-sync step's `env:` mapping as well as its `run:` body in the test.
- [x] Assert the property that actually matters: for `pull_request` and `merge_group`, the step
      binds base/head from the event payload, guards both for emptiness, and invokes
      `check_docs_sync.py` with `--diff-mode` and `--strict`.

## Tests
- [x] `python3 tests/regression/tooling/Test.CheckTaskStateLinks.py` passes on a clean checkout.
- [x] The updated assertion fails if the `env:` binding for either event is removed.

## Docs
- [x] No doc change expected; note the correction in the test docstring if the intent is unclear.

## Acceptance criteria
- [x] The test is green and still fails when the workflow's SHA routing is genuinely broken.
- [x] No change to `ci-docs.yml` behavior.

## Verification
```bash
python3 tests/regression/tooling/Test.CheckTaskStateLinks.py
python3 tools/docs/check_docs_sync.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
