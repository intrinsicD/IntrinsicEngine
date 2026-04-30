# HARDEN-010 — Replace broad legacy layering allowlist with path-specific exceptions

## Goal
- Replace the migration-wide `src/legacy/**` layering allowlist with explicit path-scoped exceptions that document which legacy subtrees still require temporary cross-layer imports.

## Non-goals
- Do not migrate, reduce, or retire `src/legacy/`.
- Do not change production runtime/graphics behavior.
- Do not add new allowlist exception categories outside legacy path narrowing.

## Context
- `tasks/done/0001-post-reorganization-hardening-tracker.md` tracks `HARDEN-010` as the prerequisite for allowlist-quality enforcement in `HARDEN-011`.
- Current `tools/repo/layering_allowlist.yaml` uses broad `src/legacy/**` exceptions for every legacy->promoted layer edge.

## Required changes
- Replace each broad `src/legacy/**` exception with path-specific `file_glob` scopes anchored to concrete legacy subdirectories.
- Keep existing task/expiry metadata explicit and temporary.
- Update `tools/repo/README.md` and hardening tracker status/evidence to reflect the narrowed policy.

## Tests
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- `tools/repo/README.md`
- `tasks/done/0001-post-reorganization-hardening-tracker.md`

## Acceptance criteria
- No `src/legacy/**` wildcard remains in `tools/repo/layering_allowlist.yaml`.
- All temporary exceptions are tied to concrete legacy paths and keep task/expiry notes.
- Strict layering check still passes.
- Hardening tracker marks `HARDEN-010` done with dated command evidence.

## Verification
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- Broad semantic refactors unrelated to allowlist scoping.
- Editing allowlist metadata to hide temporary nature or task ownership.
- Any `src/legacy` migration/retirement work.

## Completion metadata
- Completion date: 2026-04-29.
- Commit reference: pending current workspace/PR.
- Follow-up: HARDEN-011 added strict allowlist-quality enforcement.

