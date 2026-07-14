# HARDEN-074 — Make markdown link checking see inline-code labels

## Goal
- Fix `tools/docs/check_doc_links.py` so relative markdown links whose labels
  contain inline-code markup are validated instead of being skipped, then clean
  up the stale links exposed by the stricter parser.

## Non-goals
- Do not broaden this into external HTTP link checking.
- Do not add markdown header-anchor validation in this task.
- Do not change task state, task priority, or roadmap semantics except where a
  broken link target must be corrected.
- Do not relax existing build/cache/vendor exclusions in the link checker.

## Context
- Owning subsystem/layer: documentation tooling under `tools/docs/`, with
  regression coverage under `tests/regression/tooling/`.
- The repository quality assessment recorded that
  [`tools/docs/check_doc_links.py`](../../tools/docs/check_doc_links.py)
  strips inline-code spans before applying the markdown-link parser. Links such
  as ``[`GRAPHICS-077`](...)`` can disappear before validation.
- The same assessment reported stale `tasks/active` links in backlog prose that
  were not caught by `python3 tools/docs/check_doc_links.py --root . --strict`.
  Recompute the exact failing set during implementation rather than preserving
  the audit's one-off scan output as canonical.
- Related audit record:
  [`docs/reviews/2026-06-02-repository-quality-assessment.md`](../../docs/reviews/2026-06-02-repository-quality-assessment.md).

## Required changes
- [x] Refactor `tools/docs/check_doc_links.py` so markdown links are parsed
  before inline-code spans are removed, or otherwise preserve links whose labels
  contain inline-code markup.
- [x] Preserve the current exclusions for build trees, external caches,
  vendored dependencies, and generated artifact roots.
- [x] Add regression coverage in `tests/regression/tooling/` proving that a
  missing relative target fails strict mode when the link label is inline-code
  formatted.
- [x] Add regression coverage proving that ordinary relative links still pass
  and fail exactly as before.
- [x] Run the stricter checker on the repository and fix every newly reported
  broken relative link in markdown files.
- [x] Update `tools/docs/README.md` with the checker's current scope, including
  the fact that inline-code labels are validated.

## Tests
- [x] Add a Python regression test, for example
  `tests/regression/tooling/Test.CheckDocLinks.py`, that creates a temporary
  markdown fixture tree and runs the checker in strict mode.
- [x] The regression test must include one passing inline-code-label link and
  one failing inline-code-label link.
- [x] `python3 tests/regression/tooling/Test.CheckDocLinks.py` passes.
- [x] `python3 tools/docs/check_doc_links.py --root . --strict` passes after
  fixing current-tree stale links.

## Docs
- [x] Update `tools/docs/README.md` to document inline-code-label behavior.
- [x] If stale task links are fixed, update only the local prose needed to make
  the target factual; avoid rewriting unrelated roadmap sections.

## Acceptance criteria
- [x] A markdown link written as ``[`TASK-ID`](relative/path.md)`` is counted in
  the checker's "Checked relative links" total.
- [x] A broken inline-code-label relative link fails strict mode with a source
  path and target path in the diagnostic.
- [x] The repository strict link check passes after current stale links are
  fixed.
- [x] No external-link or anchor-link behavior changes are included.

## Verification
```bash
python3 tests/regression/tooling/Test.CheckDocLinks.py
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
```

## Forbidden changes
- Mixing this parser/tooling fix with production code changes.
- Treating inline-code link labels as plain text by deleting the inline-code
  formatting from docs just to make the checker pass.
- Suppressing or warning-only downgrading broken relative links that strict mode
  should catch.
- Adding broad skip directories that hide repository-owned markdown files.


## Completion
- Completed: 2026-06-02.
- Status: done.
- Commit reference: this commit (`HARDEN-074: validate inline-code markdown links`).
