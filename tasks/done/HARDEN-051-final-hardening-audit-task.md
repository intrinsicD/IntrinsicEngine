# HARDEN-051 — Add final post-reorganization hardening audit task

## Goal
Create the final hardening audit task definition that captures exact closure evidence requirements for ending the post-RORG hardening phase.

## Non-goals
- Do not retire, migrate, or shrink `src/legacy/`.
- Do not change runtime/graphics/geometry behavior.
- Do not modify CI workflow semantics.

## Context
The hardening tracker originally marked HARDEN-051 as `not-started` and required a final audit artifact, now archived at `tasks/done/final-post-reorganization-hardening-audit.md`. This task scoped only the creation of that audit task definition so a follow-up execution step could record pass/fail closure evidence without mixing unrelated changes.

## Required changes
- Add this active task file under `tasks/active/`.
- Update `tasks/done/0001-post-reorganization-hardening-tracker.md` to point HARDEN-051 at this task file and record creation evidence.

## Tests
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- Keep the HARDEN-001 tracker status board and evidence log synchronized with this task creation.

## Acceptance criteria
- [x] Active task file exists for HARDEN-051.
- [x] Hardening tracker references HARDEN-051 task-file creation evidence.
- [x] Strict task policy validation passes.
- [x] Strict doc link validation passes.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- No source-code behavioral changes.
- No test deletion or relabeling.
- No legacy retirement or migration work.

## Completion metadata
- Completion date: 2026-04-29.
- Commit reference: pending current workspace/PR.
- Follow-up: The executable audit artifact is archived at `tasks/done/final-post-reorganization-hardening-audit.md`; GPU/runtime failure triage is resolved and archived at `tasks/done/HARDEN-053-gpu-runtime-opt-in-failure-triage.md`.

