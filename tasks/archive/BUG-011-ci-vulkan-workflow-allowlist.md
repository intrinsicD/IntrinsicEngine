# BUG-011 — `docs-validation` rejects `ci-vulkan.yml` as an unexpected workflow file

## Goal
- Restore `docs-validation` (the `ci-docs` workflow row) to green by adding `ci-vulkan.yml` to `tools/ci/check_workflow_names.py`'s allowlist so the workflow naming validator stops flagging the GRAPHICS-080-introduced workflow as unexpected.

## Non-goals
- No change to the workflow itself (`name: ci-vulkan`, `on: pull_request|workflow_dispatch`, build/test steps).
- No change to the `ci` preset CPU gate behaviour.
- No promotion of `ci-vulkan.yml` to `REQUIRED_WORKFLOW_FILES`; mirroring the `nightly-deep.yml` precedent it stays opt-in (allowed but not required), so the workflow naming check tolerates removal on branches that predate the GRAPHICS-080 slice.
- No GRAPHICS-080 retirement-state edit was made in this bug task; GRAPHICS-080 retirement remained owned by its task record and later closed after the `gpu;vulkan` visible-triangle and full gate evidence landed.

## Context
- Status: done.
- Owner/agent: Claude on `claude/setup-agentic-workflow-PtvL5`.
- Owner/layer: `tools/ci` (workflow-policy validator).
- Symptom: every PR run of the `ci-docs` workflow row has been red since GRAPHICS-080 slice 1 landed. Most recent occurrence: PR #854 (HARDEN-065 slice 2 merge), job `docs-validation` (run id `25992490864`), exit code 1 at the "Validate workflow file naming policy" step.
- Repro:
  ```bash
  python3 tools/ci/check_workflow_names.py --root .github/workflows
  # ERROR: Unexpected workflow files: ci-vulkan.yml
  ```
- Root cause: `GRAPHICS-080` slice 1 added `.github/workflows/ci-vulkan.yml` (commits `e0de97f` "GRAPHICS-080 Add ci-vulkan preset and flip reference config to promoted Vulkan" and `07b3116` "GRAPHICS-080 Narrow ci-vulkan ctest filter to skip slow runtime aggregates") but did not update `tools/ci/check_workflow_names.py::ALLOWED_WORKFLOW_FILES`. The validator's strict allowlist therefore rejects the file. Because the `ci-docs` workflow row invokes the validator (`python3 tools/ci/check_workflow_names.py --root .github/workflows`) under non-strict mode, the `Unexpected workflow files` error fires and exits 1.
- Impact: `docs-validation` was the single docs-only structural-policy gate enforcing task/doc/manifest hygiene at PR time. With it red, agents had to mentally diff "real" docs-validation regressions against this preexisting failure, and the gate provided no protective signal on PRs landed during the window (`GRAPHICS-080` slice 1 onward, including HARDEN-065 slices 1 and 2, GRAPHICS-033D follow-ups, etc.).

## Required changes
- [x] Add `"ci-vulkan.yml"` to `ALLOWED_WORKFLOW_FILES` in `tools/ci/check_workflow_names.py`, placed between `"ci-bench-smoke.yml"` and `"nightly-deep.yml"` to keep the canonical-set ordering (`pr-fast`, `ci-linux-clang`, `ci-sanitizers`, `ci-docs`, `ci-bench-smoke`, opt-in tail).
- [x] Do not add `"ci-vulkan.yml"` to `REQUIRED_WORKFLOW_FILES`; the opt-in Vulkan smoke mirrors the `nightly-deep.yml` precedent for "allowed but not required".
- [x] Add `ci-vulkan.yml` to the canonical workflow list in `docs/migration/target-repo-layout.md` so the documented `.github/workflows/` layout matches the validator's allowlist.

## Tests
- [x] Reproduce the failure on `main` before the fix: `python3 tools/ci/check_workflow_names.py --root .github/workflows` exits 1 with `ERROR: Unexpected workflow files: ci-vulkan.yml`.
- [x] Re-run the same command after the fix; expect `Workflow naming check passed: ...`.
- [x] Re-run in strict mode (`--strict`) to confirm the canonical-set enforcement still passes. The README note that strict mode "reserves enforcement for the full canonical workflow set" remains accurate; the canonical set now includes `ci-vulkan.yml`.
- [x] Re-run the full set of `ci-docs` steps locally to confirm no other doc-validation regression masked by the workflow-naming failure: `check_doc_links --strict`, `check_task_policy --strict`, `check_codex_config --strict`, `check_layering_allowlist_quality --strict`, `check_test_layout --strict`, `validate_method_manifests --strict`, `validate_benchmark_manifests --strict`, `check_pr_contract --mode ci`, `generate_module_inventory` (with `git diff --exit-code`).

## Docs
- [x] Update `docs/migration/target-repo-layout.md` to include `ci-vulkan.yml` in the documented `.github/workflows/` listing.
- [x] Update `tasks/backlog/bugs/index.md` to record `BUG-011` under "Verified / Closed" with the fix summary and link to this task file.

## Acceptance criteria
- [x] `python3 tools/ci/check_workflow_names.py --root .github/workflows` passes on the current tree without `--strict`.
- [x] `python3 tools/ci/check_workflow_names.py --root .github/workflows --strict` also passes (the canonical-set assertion still holds).
- [x] All other `ci-docs` workflow steps continue to pass locally.
- [x] The fix does not change the workflow file itself; only the validator's allowlist and one docs reference change.
- [x] `docs/api/generated/module_inventory.md` regenerates byte-identical (no module surface change).

## Verification
```bash
python3 tools/ci/check_workflow_names.py --root .github/workflows
python3 tools/ci/check_workflow_names.py --root .github/workflows --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_codex_config.py --root . --strict
python3 tools/repo/check_layering_allowlist_quality.py --root . --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_method_manifests.py --root methods --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/repo/check_pr_contract.py --root . --mode ci
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
git diff --exit-code docs/api/generated/module_inventory.md
```

## Forbidden changes
- Deleting or modifying `.github/workflows/ci-vulkan.yml` (that workflow is owned by GRAPHICS-080).
- Promoting `ci-vulkan.yml` to `REQUIRED_WORKFLOW_FILES` (would force every branch that predates GRAPHICS-080 to fail until rebased).
- Renaming the workflow or its top-level `name:` field (would re-introduce the validator failure under the filename-stem rule).
- Mixing this allowlist fix with new workflow content, preset edits, or GRAPHICS-080 retirement.

## Completion
- Completed: 2026-05-17.
- Branch: `claude/setup-agentic-workflow-PtvL5`.
- Commit reference: pending (this session's single-slice commit on the branch above).
- Verification: every command in the `## Verification` block ran clean in this session; full list captured in the PR description.
