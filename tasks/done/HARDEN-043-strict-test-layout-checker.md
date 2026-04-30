# HARDEN-043 — Add strict test layout checker

## Goal
- Add a strict repository checker that enforces the post-HARDEN-041/HARDEN-042 taxonomy test source layout.

## Non-goals
- No migration or retirement work under `src/legacy/`.
- No semantic test refactors.
- No broad CMake/test policy changes beyond wiring this checker.

## Context
- HARDEN-040 audited remaining non-taxonomic test directories and HARDEN-041/HARDEN-042 completed source relocation and wrapper removal.
- HARDEN-043 closes the gap by adding an automated strict checker so regressions are blocked in CI.

## Required changes
- Add `tools/repo/check_test_layout.py` to enforce allowed test source roots and block legacy wrapper test source directories.
- Wire the checker into `.github/workflows/ci-docs.yml` strict validation.
- Document checker usage in `tools/repo/README.md`.
- Update `tasks/done/0001-post-reorganization-hardening-tracker.md` status/evidence for HARDEN-043.

## Tests
- Run `python3 tools/repo/check_test_layout.py --root . --strict`.
- Run strict task/doc checks after task and tracker updates.

## Docs
- `tools/repo/README.md` checker inventory update.
- `tasks/done/0001-post-reorganization-hardening-tracker.md` status board and evidence log update.

## Acceptance criteria
- [x] Strict test layout checker exists and validates the final taxonomy layout.
- [x] CI docs workflow runs the checker in strict mode.
- [x] Repo tooling docs include the new checker.
- [x] Hardening tracker marks HARDEN-043 done with command evidence.

## Verification
```bash
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Reintroducing wrapper test source ownership (`tests/Asset`, `tests/Core`, `tests/ECS`, `tests/Graphics`, `tests/Runtime`).

## Completion metadata
- Completion date: 2026-04-29.
- Commit reference: pending current workspace/PR.
- Follow-up: Keep `tools/repo/check_test_layout.py --root . --strict` in CI docs validation.

