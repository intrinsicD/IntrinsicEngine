# HARDEN-011 — Add layering allowlist quality checker

## Goal
- Enforce structural quality rules for `tools/repo/layering_allowlist.yaml` so temporary exceptions stay scoped, documented, and reviewable.

## Non-goals
- Do not migrate, reduce, or retire `src/legacy/`.
- Do not change production runtime/graphics behavior.
- Do not alter dependency-layer semantics beyond quality validation.

## Context
- `tasks/done/0001-post-reorganization-hardening-tracker.md` marks `HARDEN-011` as the next allowlist-hardening step after `HARDEN-010`.
- `tools/repo/check_layering.py` validates live dependency edges, but it does not yet validate allowlist-entry hygiene (duplicates, broad globs, missing metadata) as a dedicated policy gate.

## Required changes
- Add a strict-capable checker for `tools/repo/layering_allowlist.yaml` quality.
- Validate required metadata fields (`from`, `to`, `file_glob`, `task`, `expires`, `reason`) and non-empty values.
- Reject broad legacy wildcard globs (`src/legacy/**`) and duplicate `(from,to,file_glob)` entries.
- Wire the checker into CI docs/policy validation.
- Update `tools/repo/README.md` and hardening tracker status/evidence.

## Tests
- `python3 tools/repo/check_layering_allowlist_quality.py --root . --strict`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- `tools/repo/README.md`
- `tasks/done/0001-post-reorganization-hardening-tracker.md`

## Acceptance criteria
- Checker fails strict mode when required fields are missing, broad globs are present, or duplicate entries exist.
- Checker passes strict mode on current repository state.
- CI docs workflow runs the checker.
- Hardening tracker marks `HARDEN-011` done with concrete evidence.

## Verification
```bash
python3 tools/repo/check_layering_allowlist_quality.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- Broad cleanup/refactors unrelated to allowlist-quality policy enforcement.
- Any `src/legacy` migration/retirement work.
- Converting temporary exceptions into undocumented permanent policy.

## Completion metadata
- Completion date: 2026-04-29.
- Commit reference: pending current workspace/PR.
- Follow-up: Continue tracking temporary layering exceptions through their owning tasks and expiry metadata.

