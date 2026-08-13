---
id: BUG-161
theme: J
depends_on: [ARCH-017]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-root"
branch: "agent/framework24-product-convergence-goal"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-13T11:19:39Z"
contract_schema: 1
contracts: [repo.task-contract-discovery]
contract_review: "The clean-checkout repair itself changes no reusable engine contract. Its review repair reconciles duplicate task ownership under the task-discovery contract: BUG-157 is the sole open Clang/GLM owner, BUG-121 is retired as a duplicate, and the uncommitted BUG-162 draft is removed."
---
# BUG-161 — Clean checkouts cannot validate the Framework24 convergence policy

## Status

- Completed and retired on 2026-08-13. Hosted documentation validation passed
  end-to-end, and full CPU passed checkout plus strict task policy before a
  separate pre-existing Clang/GLM module compile defect stopped the build;
  `BUG-157` remains the sole owner of that downstream blocker.
- Implementation commit: `c283de25`.

## Goal

- Make the Framework24 convergence policy pass the same strict documentation
  and task-policy validation in a clean hosted checkout as it does locally.

## Non-goals

- No engine, method, renderer, UI, benchmark, or Framework24 implementation
  change.
- No weakening, skipping, or quarantining of a required gate.
- No broad CI refactor or unrelated workflow cleanup.
- No repair of the downstream pre-existing Clang/GLM test-module failure;
  `BUG-157` owns that separately reproduced defect.

## Context

- Symptom: PR 1030's documentation gate rejects a relative link into the
  ignored local `experimental/framework24` checkout, and its full-CPU job
  cannot resolve the pinned historical task-contract revision from the
  default shallow checkout.
- Expected behavior: repository documentation must not require an untracked
  comparison checkout, and every job that runs strict task-policy validation
  must fetch the Git history needed by that validator.
- Impact: the policy-only convergence change is locally valid but cannot pass
  required hosted merge gates.

## Required changes

- [x] Render the Framework24 comparison-source path as a non-link repository
  locator, retaining its exact revision binding without claiming the ignored
  tree is present in a clean checkout.
- [x] Give the full-CPU job complete history before it invokes strict task
  policy validation.
- [x] Freeze the full-CPU checkout/history prerequisite in the workflow
  regression suite.

## Tests

- [x] Reproduce and clear the strict documentation-link failure.
- [x] Add a workflow regression asserting that the full-CPU task-policy job
  checks out complete history.
- [x] Pass the focused workflow regression and strict structural validators.

## Docs

- [x] Keep the feature inventory's source/revision statement factual in both
  local comparison and clean hosted checkouts.
- [x] Regenerate the session brief after retiring this bug.

## Acceptance criteria

- [x] `check_doc_links.py --strict` passes without the ignored Framework24
  tree.
- [x] The full-CPU workflow has the history required to resolve
  `contract_legacy_tasks.json`'s pinned source revision.
- [x] PR 1030's documentation-link and strict task-policy stages rerun
  successfully; the later unrelated compile blocker is tracked only by
  `BUG-157`.

## Verification

```bash
python3 tools/docs/check_doc_links.py --root . --strict
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode \
  --base-ref origin/main --head-ref HEAD --strict
```

## Forbidden changes

- Bypassing or downgrading a red merge gate.
- Depending on ignored/untracked comparison-tree content in repository link
  validation.
- Fetching less history than the strict task-policy validator requires.
- Changing product behavior while repairing CI portability.
