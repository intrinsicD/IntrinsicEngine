---
id: CI-003
theme: none
depends_on: []
---
# CI-003 — Front-load structural validators and add cancel-in-progress concurrency to PR workflows

## Goal
- Make layering/task-policy/doc-link violations fail a PR in under ~60s instead of after the full build+test, and stop superseded pushes from stacking duplicate full-build waves — with zero change to what is checked.

## Non-goals
- Do not change any CTest label expression, build target, or the set of validators that run.
- Do not add `concurrency` to `nightly-deep.yml` (not PR-triggered) or weaken the push-to-`main` gate on `ci-linux-clang.yml`.
- Do not touch `touched_scope.py` or introduce selective test running (owned by CI-008).

## Context
- Owner: CI/tooling; touches `.github/workflows/*.yml` only.
- Today `pr-fast.yml` runs `check_task_policy.py`, `check_doc_links.py`, and `check_layering.py` (lines 54-64) *after* the ~952-1381s build and the ctest phase, so a layering violation — which measures 0-1s as a step — is reported ~19-28 min after push. `ci-linux-clang.yml` runs `check_layering.py` (lines 81-82) after its full build for the same reason; it already proves the pre-build validator pattern with `check_task_policy.py` at lines 58-59.
- `ci-docs.yml` (12 pure-Python validation steps, lines 22-60) completes in 11-52s, demonstrating buildless validation is fast and safe to run first. `check_layering.py`, `check_task_policy.py`, and `check_doc_links.py` are regex-over-source with no build-tree dependency.
- No workflow defines a `concurrency:` group (grep confirms zero), so the three rapid pushes observed on 2026-07-08 each spawned an independent ~133-min job wave; earlier waves keep compiling dead commits ahead of the current head's signal.

## Required changes
- [ ] In `pr-fast.yml`, move the `Install Python validation deps` + three validator steps (task policy, doc links, layering) to run immediately after `Install system dependencies`, before `Configure (ci preset)` and `Build IntrinsicTests` — keeping them after apt so preinstalled `python3`/`pip` ordering is not relied upon.
- [ ] In `ci-linux-clang.yml`, move `Validate layering (strict mode)` up next to the existing pre-build `Enforce task policy` step so a layering break fails before the full build.
- [ ] Add a `concurrency:` block to `pr-fast.yml`, `ci-linux-clang.yml`, `ci-sanitizers.yml`, `ci-vulkan.yml`, and `ci-bench-smoke.yml` using group `${{ github.workflow }}-${{ github.event.pull_request.number || github.sha }}` and `cancel-in-progress: ${{ github.event_name == 'pull_request' }}` — the `pull_request.number || sha` form gives PR-level cancellation while every push to `main` keeps its own run (no per-commit main-coverage loss).

## Tests
- [ ] `python3 tools/ci/check_workflow_names.py` (or the workflow-name validator invoked by `ci-docs.yml`) passes after edits.
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict`, `python3 tools/docs/check_doc_links.py --root .`, and `python3 tools/repo/check_layering.py --root src --strict` still pass (the validators themselves are unchanged).
- [ ] On the PR that lands this, confirm from the Actions run that the validator steps report before Configure/Build and that a second push cancels the first PR-triggered run.

## Docs
- [ ] Update the workflow documentation per `docs/agent/docs-sync-policy.md` §"CI/workflow changes" (backed by `AGENTS.md` §9) to record the new step order and the concurrency policy, including the explicit `nightly-deep.yml` exclusion.

## Acceptance criteria
- [ ] A PR whose only defect is a layering or task-policy violation fails within ~1 min of push (no build performed).
- [ ] The executed check set is a permutation of today's — the workflow diff removes no step and changes no command.
- [ ] Pushing a new commit to an open PR cancels the superseded in-progress PR runs; push-to-`main` runs are never cancelled by another main push.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/ci/check_workflow_names.py --root . || true
# Then observe the Actions run on the landing PR: validator steps precede Configure/Build;
# a follow-up push shows the prior PR run transition to "cancelled".
```

## Forbidden changes
- Removing, renaming, or weakening any validator or test step.
- Adding cancel-in-progress semantics to non-PR triggers or to `main` pushes.
- Bundling ccache, selective testing, or ci-vulkan changes into this task.
