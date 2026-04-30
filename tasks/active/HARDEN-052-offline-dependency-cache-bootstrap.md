# HARDEN-052 — Bootstrap offline dependency cache for CI preset

## Goal
Unblock the CI preset offline configure requirement by populating `external/cache/*-src` in a reproducible way and documenting the workflow.

## Non-goals
- Runtime/graphics feature work.
- Broad build-system redesign.

## Required changes
- Add/verify documented bootstrap steps for dependency cache population.
- Validate offline configure success after cache bootstrap.
- Record build/ctest evidence needed to close HARDEN-051.

## Tests
- `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`

## Docs
- Update `tasks/active/0001-post-reorganization-hardening-tracker.md` with bootstrap and gate results.
- Update `tasks/active/final-post-reorganization-hardening-audit.md` when blocker is resolved.

## Acceptance criteria
- [ ] Offline dependency cache bootstrap steps are documented and reproducible.
- [ ] `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` succeeds in a clean workspace after bootstrap.
- [ ] Follow-on build/ctest evidence is recorded for HARDEN-051 closure.

## Verification
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`
