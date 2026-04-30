# HARDEN-033 — Add stale `src_new` reference checker

## Goal
- Enforce that active repository content does not reintroduce stale `src_new`/`src-new` references outside an explicit migration allowlist.

## Non-goals
- Do not migrate, reduce, or retire `src/legacy/`.
- Do not rewrite historical migration archives that intentionally preserve `src_new` references.
- Do not perform runtime/graphics semantic refactors.

## Context
- `tasks/active/0001-post-reorganization-hardening-tracker.md` marks `HARDEN-033` as not-started with owner `tools/repo/CI`.
- `docs/migration/src-new-reference-audit.md` already classifies remaining references as active-stale, migration-ok, or historical-ok.

## Required changes
- Add a repository checker script that scans tracked text files for stale `src_new` variants.
- Add an allowlist file containing migration/historical paths that are intentionally exempt.
- Wire the checker into CI docs/policy validation.
- Update `tools/repo/README.md` and hardening tracker status/evidence.

## Tests
- `python3 tools/repo/check_stale_src_new_references.py --root . --strict`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- `tools/repo/README.md`
- `tasks/active/0001-post-reorganization-hardening-tracker.md`

## Acceptance criteria
- Checker fails strict mode when non-allowlisted stale references are present.
- Checker passes strict mode on current repository state.
- CI docs workflow runs the checker.
- Hardening tracker marks `HARDEN-033` done with concrete evidence.

## Verification
```bash
python3 tools/repo/check_stale_src_new_references.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- Mixing broad cleanup/refactors unrelated to stale-reference policy.
- Editing historical records to hide migration history.

## Completion metadata
- Completion date: 2026-04-29.
- Commit reference: pending current workspace/PR.
- Follow-up: Keep `tools/repo/src_new_reference_allowlist.txt` synchronized when historical task records move.

