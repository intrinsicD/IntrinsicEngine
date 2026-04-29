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

Most workflows already follow this policy. Follow-up HARDEN-050B found `ci-sanitizers.yml` still used
`-L "unit|contract|integration"` without the canonical exclusion/timeout policy, which left sanitizer CI inconsistent with documented CPU-supported gating.

## Required changes
- Update `.github/workflows/pr-fast.yml` to set `--timeout 60` on the unit/contract test command.
- Update `.github/workflows/ci-sanitizers.yml` selected CPU test command to include `-LE "gpu|vulkan|slow|flaky-quarantine"` and `--timeout 60`.
- Update `tasks/active/0001-post-reorganization-hardening-tracker.md` status and evidence entries for HARDEN-050.

## Tests
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- Keep hardening tracker status/evidence synchronized.

## Acceptance criteria
- [x] `pr-fast.yml` unit/contract CTest step includes `--timeout 60`.
- [x] `ci-sanitizers.yml` selected CPU CTest step excludes `gpu|vulkan|slow|flaky-quarantine` and sets `--timeout 60`.
- [x] Hardening tracker reflects HARDEN-050 completion with evidence.
- [x] Strict task/doc validators pass.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
