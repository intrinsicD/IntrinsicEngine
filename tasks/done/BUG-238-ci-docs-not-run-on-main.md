---
id: BUG-238
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive fix; evidence is the diff, the tooling tests and the focused runtime suites
contract_schema: 1
contracts: []
contract_review: CI trigger, two tooling-test expectations and a default argument replaced by an overload; no catalog contract covers CI routing or test expectations, and the overload keeps every caller's behavior.
---
# BUG-238 — `ci-docs` does not run on `main`, and three of its checks went red unnoticed

## Goal
- `ci-docs` runs on every push to `main`, and its tooling tests pass on `main`.

## Context
- `ci-docs.yml` triggered only on `pull_request`, `merge_group` and `workflow_dispatch`; work
  lands on `main` by direct push, so its last run was 2026-09-23 (see BUG-237).
- Running its tooling tests on a clean worktree of `8a83bc328` (2026-10-08) showed three failures
  that had reached `main` that way:
  - `Test.WorkflowConcurrency.py`: eight multi-worker CPU tests added 2026-10-01/02
    (`c5357a84d`, `25a381ec3`, `62955173f` and neighbours) start 2–3 scheduler workers but had no
    `PROCESSORS` budget in `tests/CMakeLists.txt`.
  - `Test.TouchedScope.py`: the module rename `c9011aff4` (2026-09-25) replaced the target name in
    place, but `touched_scope.py` lists targets sorted, so the expectation's order was stale.
  - `Test.CheckCompilerHazards.py`: `0f4926355` (2026-10-02) gave
    `JobFailure.hpp` `Finalize(const std::optional<Result>& = std::nullopt)`, the BUG-223 pattern
    the checker forbids.
- `Test.RootHygiene.py` fails only in the local checkout (untracked root entries); it passes on a
  clean worktree and is not part of this bug.

## Acceptance criteria
- [x] `ci-docs` runs on push to `main`; its docs-sync step diffs the pushed range
      (`github.event.before`..`after`) and fails closed without a valid `before` SHA or when
      `before` is not an ancestor of `after` (a rewinding force push would otherwise diff empty).
- [x] The three tooling tests pass; the routing test asserts the push trigger and covers the push
      route, and each of these checks fails without its workflow change.
- [x] The eight budgets are applied by CTest (`PROCESSORS` 2 or 3).
- [x] `Finalize()` keeps its behavior through a forwarding overload; affected runtime suites pass.

## Verification
```bash
for t in tests/regression/tooling/Test.*.py; do python3 "$t"; done   # the ci-docs set
python3 tools/ci/check_workflow_names.py --root .github/workflows
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -j8 -R '^(DensityWeight|DensityWeightConfig|DensityWeightOperations|PointSamplingOperations|QueuedEditorJobContract|QueuedEditorJobDriftGuard|ResidentPointSampling|RuntimeReuseDriftGuard|PointScalarTransaction[A-Za-z]*|RuntimeJobService|ClusteringModule)\.'
# 2026-10-08: 93/93 passed
```

## Completion
Completed 2026-10-08. Commit: `ea3c467403d0cfbaa68750c2868bd92081498719` (fix) and
`607f985cd` (review fixes).

## Review
- 2026-10-08, Claude → Codex (`codex exec`, requested `gpt-6-astra` effort `xhigh`, read-only
  sandbox), thread `01a11b43-ecbf-78c3-8e63-088f2b6887d9`.
- Round 1 on `ea3c46740` (sha256 of `git show` starts `c0bb886a45f89517`): "**revise required**",
  two findings: "[P2] Force pushes can silently skip changes" and "[P2] The original
  missing-trigger bug remains untested"; "No additional ponytail complexity findings or weakened
  existing gates." Both fixed in `607f985cd`.
- Round 2 on `ea3c46740..607f985cd` (diff sha256 starts `87f8714b574c951b`): "**approve** …
  No remaining correctness or ponytail findings."
- Recursion protection: instruction-only (other MCP servers were not disabled).
