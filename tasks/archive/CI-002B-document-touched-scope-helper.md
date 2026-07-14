# CI-002B — Document touched-scope verification helper entry points

## Goal
- Cross-link the touched-scope verification helper from the repository docs and review entry points where developers and agents look for test/CI guidance.

## Non-goals
- Do not change `tools/ci/touched_scope.py` behavior.
- Do not wire the helper into GitHub Actions.
- Do not weaken or replace the canonical full CPU-supported gate.

## Context
- Owner: CI/tooling documentation.
- `CI-002` added `tools/ci/touched_scope.py` and documented it in `docs/build-troubleshooting.md`.
- This task only synchronizes discoverability across existing documentation entry points.

## Required changes
- [x] Add touched-scope helper references to top-level developer docs.
- [x] Add touched-scope helper references to test/verification strategy docs.
- [x] Add touched-scope helper references to agent/review entry points.

## Tests
- [x] Run docs link validation.
- [x] Run docs-sync validation for changed files.
- [x] Run task policy validation.

## Docs
- [x] Update all relevant docs in the same change.

## Acceptance criteria
- [x] Developers can discover `tools/ci/touched_scope.py` from README/docs index/test strategy docs.
- [x] Agents can discover the helper from verification/review workflow docs.
- [x] Docs continue to state that the helper is not a replacement for the full PR/merge gate.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/agents/check_task_policy.py --root . --strict
```

Completed verification:
- `python3 tools/ci/touched_scope.py --root . --changed-file README.md --changed-file docs/index.md --changed-file docs/agent/contract.md --changed-file docs/agent/prompt/prompt.md --changed-file docs/agent/review-checklist.md --changed-file docs/architecture/test-strategy.md --changed-file tests/README.md --changed-file .github/pull_request_template.md --changed-file tasks/active/CI-002B-document-touched-scope-helper.md --build-dir cmake-build-debug --print` — produced a docs/task/test-layout structural plan.
- `python3 tools/docs/check_doc_links.py --root .` — passed in warning mode with no broken relative links.
- `python3 tools/docs/check_docs_sync.py --root . --files README.md docs/index.md docs/agent/contract.md docs/agent/prompt/prompt.md docs/agent/review-checklist.md docs/architecture/test-strategy.md tests/README.md .github/pull_request_template.md tasks/active/CI-002B-document-touched-scope-helper.md` — passed.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed before retirement; re-run after retirement once completion metadata was added.

## Forbidden changes
- Changing CI workflow behavior.
- Changing touched-scope helper command selection semantics.
- Broad policy edits outside documenting the existing helper.

## Completion
- Completed: 2026-05-13.
- Status: done.
- Implementation commit: this local change (`CI-002B: document touched-scope verifier`).
