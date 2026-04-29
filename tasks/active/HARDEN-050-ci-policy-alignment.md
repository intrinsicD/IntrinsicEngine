# HARDEN-050 — Align CI workflows with final supported test policy

## Goal
Ensure active CI workflows use the canonical CPU-supported CTest gate consistently.

## Non-goals
- Do not change test source code or labels.
- Do not alter `src/legacy/`.
- Do not add new feature behavior.

## Context
HARDEN-005/HARDEN-006 established the canonical CPU-supported gate:

`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`

Most workflows already follow this policy. `pr-fast.yml` still ran a subset gate without an explicit timeout, which leaves behavior inconsistent with documented CI policy.

## Required changes
- Update `.github/workflows/pr-fast.yml` to set `--timeout 60` on the unit/contract test command.
- Update `tasks/active/0001-post-reorganization-hardening-tracker.md` status and evidence entries for HARDEN-050.

## Tests
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- Keep hardening tracker status/evidence synchronized.

## Acceptance criteria
- [x] `pr-fast.yml` unit/contract CTest step includes `--timeout 60`.
- [x] Hardening tracker reflects HARDEN-050 completion with evidence.
- [x] Strict task/doc validators pass.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
